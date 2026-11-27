#include "optrich/compiler.hpp"
#include "optrich/runtime.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace optrich;

namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open file '" + path + "'");
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void print_usage() {
    std::cerr
        << "Usage: optrich-vm [options] <main.wat> [export_func] [args...]\n"
        << "Options:\n"
        << "  --link <name>=<file.wat>   Link a dependency module before <main.wat> (repeatable)\n"
        << "  --gc-threshold <N>         Set automatic GC trigger threshold (default: 4096, 0=off)\n"
        << "  --gc-stats                 Run final GC and print MemoryStore / GC statistics\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::pair<std::string, std::string>> linked_mods;
    size_t gc_threshold = 4096;
    bool show_gc_stats = false;

    int argi = 1;
    while (argi < argc) {
        std::string arg = argv[argi];
        if (arg == "--link") {
            if (argi + 1 >= argc) {
                print_usage();
                return 1;
            }
            std::string spec = argv[++argi];
            size_t eq = spec.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
                std::cerr << "Error: --link expects <name>=<file.wat>, got '" << spec << "'\n";
                return 1;
            }
            linked_mods.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
            ++argi;
        } else if (arg == "--gc-threshold") {
            if (argi + 1 >= argc) {
                print_usage();
                return 1;
            }
            gc_threshold = static_cast<size_t>(std::stoull(argv[++argi]));
            ++argi;
        } else if (arg == "--gc-stats") {
            show_gc_stats = true;
            ++argi;
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            break;
        }
    }

    if (argi >= argc) {
        print_usage();
        return 1;
    }

    std::string main_path = argv[argi++];
    std::string func_name = (argi < argc) ? argv[argi++] : "main";

    try {
        Runtime rt(gc_threshold);

        for (const auto& [name, path] : linked_mods) {
            rt.instantiate_wat(name, read_text_file(path));
        }

        ModuleInstance& main_inst = rt.instantiate_wat("__main__", read_text_file(main_path));
        uint32_t fidx = main_inst.find_export(func_name);
        const FuncSignature& sig = main_inst.module().func_sig(fidx);

        std::vector<Value> args;
        size_t provided = static_cast<size_t>(argc - argi);
        if (provided != sig.params.size()) {
            std::cerr << "Error: function '" << func_name << "' expects " << sig.params.size()
                      << " argument(s), got " << provided << "\n";
            return 1;
        }
        for (size_t i = 0; i < sig.params.size(); ++i) {
            std::string arg_str = argv[argi + static_cast<int>(i)];
            switch (sig.params[i]) {
                case ValType::I32: args.push_back(Value::from_i32(std::stoi(arg_str))); break;
                case ValType::I64: args.push_back(Value::from_i64(std::stoll(arg_str))); break;
                case ValType::F32: args.push_back(Value::from_f32(std::stof(arg_str))); break;
                case ValType::F64: args.push_back(Value::from_f64(std::stod(arg_str))); break;
                default:
                    std::cerr << "Error: cannot pass handle argument from CLI\n";
                    return 1;
            }
        }

        auto results = rt.vm().invoke_index(main_inst, fidx, args);
        for (size_t i = 0; i < results.size(); ++i) {
            switch (sig.results[i]) {
                case ValType::I32: std::cout << results[i].i32 << "\n"; break;
                case ValType::I64: std::cout << results[i].i64 << "\n"; break;
                case ValType::F32: std::cout << results[i].f32 << "\n"; break;
                case ValType::F64: std::cout << results[i].f64 << "\n"; break;
                case ValType::Handle:
                case ValType::RwHandle:
                case ValType::RoHandle:
                    std::cout << "handle#" << results[i].handle.id
                              << " (bytes=" << results[i].handle.byteSize
                              << ", ro=" << (results[i].handle.is_readonly() ? "true" : "false")
                              << ")\n";
                    break;
            }
        }

        if (show_gc_stats) {
            size_t live_before = rt.store().live_block_count();
            GCStats final_gc = rt.collect_garbage();
            std::cout << "[gc] live_before=" << live_before
                      << " swept=" << final_gc.swept
                      << " live_after=" << final_gc.liveAfter
                      << " total_collections=" << final_gc.totalCollections
                      << " free_list=" << rt.store().free_list_size() << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
