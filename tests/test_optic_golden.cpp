#include "optrich/host_trace.hpp"
#include "optrich/runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace optrich;

namespace {

std::string read_file_str(const fs::path& p) {
    std::ifstream f(p);
    if (!f) {
        throw std::runtime_error("cannot open file '" + p.string() + "'");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string format_results(const FuncSignature& sig, const std::vector<Value>& results) {
    std::ostringstream out;
    for (size_t i = 0; i < results.size(); ++i) {
        out << "return: ";
        switch (sig.results[i]) {
            case ValType::I32: out << results[i].i32 << "\n"; break;
            case ValType::I64: out << results[i].i64 << "\n"; break;
            case ValType::F32: out << results[i].f32 << "\n"; break;
            case ValType::F64: out << format_f64_clean(results[i].f64) << "\n"; break;
            case ValType::RwHandle:
            case ValType::RoHandle:
                if (results[i].handle.is_null()) {
                    out << "null\n";
                } else {
                    out << "handle(ro=" << (results[i].handle.is_readonly() ? "true" : "false")
                        << ", bytes=" << results[i].handle.byteSize << ")\n";
                }
                break;
            case ValType::FuncRef:
                out << (results[i].funcref.is_null() ? "funcref(null)\n" : "funcref\n");
                break;
        }
    }
    return out.str();
}

struct GoldenCase {
    std::string                                    name;
    std::vector<std::pair<std::string, fs::path>>  linkedMods;
    fs::path                                       mainWat;
    fs::path                                       expectedTxt;
    bool                                           expectTrap = false;
};

std::vector<GoldenCase> discover_cases(const fs::path& root) {
    std::vector<GoldenCase> cases;
    if (!fs::exists(root)) return cases;

    std::vector<fs::directory_entry> entries;
    for (const auto& e : fs::directory_iterator(root)) {
        entries.push_back(e);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });

    const std::string trap_suffix = ".trap.guessed.wat";
    const std::string suffix      = ".guessed.wat";
    for (const auto& e : entries) {
        if (e.is_regular_file()) {
            std::string fname = e.path().filename().string();
            if (fname.size() > trap_suffix.size() &&
                fname.compare(fname.size() - trap_suffix.size(), trap_suffix.size(), trap_suffix) == 0) {
                std::string stem = fname.substr(0, fname.size() - trap_suffix.size());
                fs::path expected = root / (stem + ".trap.expected.txt");
                if (fs::exists(expected)) {
                    cases.push_back(GoldenCase{stem + " [trap]", {}, e.path(), expected, true});
                }
            } else if (fname.size() > suffix.size() &&
                       fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
                std::string stem = fname.substr(0, fname.size() - suffix.size());
                fs::path expected = root / (stem + ".expected.txt");
                if (fs::exists(expected)) {
                    cases.push_back(GoldenCase{stem, {}, e.path(), expected, false});
                }
            }
        } else if (e.is_directory()) {
            fs::path expected = e.path() / "expected.txt";
            fs::path manifest = e.path() / "modules.txt";
            if (!fs::exists(expected) || !fs::exists(manifest)) continue;

            GoldenCase gc;
            gc.name = e.path().filename().string();
            gc.expectedTxt = expected;

            // modules.txt lists `<name>=<file.guessed.wat>` lines; the last entry is main.
            auto lines = split_lines(read_file_str(manifest));
            std::vector<std::pair<std::string, fs::path>> mods;
            for (const auto& line : lines) {
                if (line.empty() || line[0] == '#') continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) {
                    throw std::runtime_error("invalid modules.txt line in " + gc.name + ": " + line);
                }
                mods.emplace_back(line.substr(0, eq), e.path() / line.substr(eq + 1));
            }
            if (mods.empty()) continue;
            gc.mainWat = mods.back().second;
            mods.pop_back();
            gc.linkedMods = std::move(mods);
            cases.push_back(std::move(gc));
        }
    }
    return cases;
}

bool run_case(const GoldenCase& c) {
    Runtime rt(0); // Deterministic GC: triggered explicitly via host::trace_gc
    std::ostringstream trace_out;
    register_host_trace(rt, trace_out);

    for (const auto& [mod_name, wat_path] : c.linkedMods) {
        rt.instantiate_wat(mod_name, read_file_str(wat_path));
    }

    ModuleInstance& main_inst = rt.instantiate_wat("__main__", read_file_str(c.mainWat));
    uint32_t fidx = main_inst.find_export("main");
    const FuncSignature& sig = main_inst.module().func_sig(fidx);

    std::string actual;
    if (c.expectTrap) {
        bool trapped = false;
        try {
            (void)rt.vm().invoke_index(main_inst, fidx, {});
        } catch (const Trap& t) {
            trapped = true;
            actual = trace_out.str() + "trap: " + t.what() + "\n";
        }
        if (!trapped) {
            std::cerr << "  [FAIL] " << c.name << ": expected runtime trap, but main returned normally\n";
            return false;
        }
    } else {
        auto results = rt.vm().invoke_index(main_inst, fidx, {});
        actual = trace_out.str() + format_results(sig, results);
    }
    std::string expected = read_file_str(c.expectedTxt);

    auto actual_lines = split_lines(actual);
    auto expected_lines = split_lines(expected);

    if (actual_lines == expected_lines) {
        std::cout << "  [PASS] " << c.name << " (" << actual_lines.size() << " lines verified)\n";
        return true;
    }

    std::cerr << "  [FAIL] " << c.name << " golden output mismatch:\n";
    size_t n = std::max(actual_lines.size(), expected_lines.size());
    for (size_t i = 0; i < n; ++i) {
        std::string exp_l = (i < expected_lines.size()) ? expected_lines[i] : "<EOF>";
        std::string act_l = (i < actual_lines.size()) ? actual_lines[i] : "<EOF>";
        if (exp_l != act_l) {
            std::cerr << "    line " << (i + 1) << ":\n"
                      << "      expected: " << exp_l << "\n"
                      << "      actual:   " << act_l << "\n";
        }
    }
    return false;
}

} // namespace

int main() {
    fs::path root = "examples/optic";
    try {
        auto cases = discover_cases(root);
        if (cases.empty()) {
            std::cout << "No Optic golden cases found in " << root << ".\n";
            return 0;
        }
        int passed = 0;
        int failed = 0;
        for (const auto& c : cases) {
            try {
                if (run_case(c)) {
                    ++passed;
                } else {
                    ++failed;
                }
            } catch (const std::exception& e) {
                std::cerr << "  [FAIL] " << c.name << ": " << e.what() << "\n";
                ++failed;
            }
        }
        std::cout << "Optic Golden Summary: " << passed << " passed, " << failed << " failed.\n";
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
