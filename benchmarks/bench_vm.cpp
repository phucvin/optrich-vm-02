#include "optrich/compiler.hpp"
#include "optrich/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace optrich;

namespace {

using Clock = std::chrono::steady_clock;

std::string read_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open benchmark file '" + path + "'");
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Native C++ (-O2) baselines for interpreter-vs-native comparison.
__attribute__((noinline)) int32_t native_fib(int32_t n) {
    if (n < 2) return n;
    return native_fib(n - 1) + native_fib(n - 2);
}

__attribute__((noinline)) int64_t native_sum_loop(int32_t n) {
    int64_t acc = 0;
    for (int32_t i = 0; i < n; ++i) {
        uint32_t v = static_cast<uint32_t>(i * 3 + 1) ^ (static_cast<uint32_t>(i) >> 2);
        acc += static_cast<int64_t>(v);
    }
    return acc;
}

__attribute__((noinline)) int64_t native_array_sum(int32_t len, int32_t iters) {
    std::vector<int32_t> buf(static_cast<size_t>(len));
    for (int32_t i = 0; i < len; ++i) buf[static_cast<size_t>(i)] = i;
    int64_t total = 0;
    for (int32_t it = 0; it < iters; ++it) {
        for (int32_t i = 0; i < len; ++i) {
            total += static_cast<uint32_t>(buf[static_cast<size_t>(i)]);
        }
    }
    return total;
}

struct BenchSpec {
    std::string        name;
    std::string        wat_path;
    std::string        func_name;
    std::vector<Value> args;
    int64_t            expected;
    int                default_runs;
    size_t             gc_threshold;
    std::string        unit_label;
    double             unit_count; // e.g. operations, calls, or MB for throughput
    std::function<int64_t()> native_fn; // optional native baseline
};

int64_t extract_int_result(const std::vector<Value>& res, ValType type) {
    if (res.empty()) {
        throw std::runtime_error("benchmark function returned no value");
    }
    switch (type) {
        case ValType::I32: return static_cast<int64_t>(res[0].i32);
        case ValType::I64: return res[0].i64;
        default: throw std::runtime_error("unexpected non-integer benchmark result");
    }
}

std::string format_duration(double ms) {
    std::ostringstream os;
    os << std::fixed;
    if (ms >= 1000.0) {
        os << std::setprecision(3) << (ms / 1000.0) << " s";
    } else if (ms >= 1.0) {
        os << std::setprecision(2) << ms << " ms";
    } else {
        os << std::setprecision(1) << (ms * 1000.0) << " us";
    }
    return os.str();
}

std::string format_rate(double count, double ms, const std::string& unit) {
    if (count <= 0.0 || ms <= 0.0 || unit.empty()) return "-";
    double per_sec = count / (ms / 1000.0);
    std::ostringstream os;
    os << std::fixed;
    if (per_sec >= 1e9) {
        os << std::setprecision(2) << (per_sec / 1e9) << " G" << unit << "/s";
    } else if (per_sec >= 1e6) {
        os << std::setprecision(2) << (per_sec / 1e6) << " M" << unit << "/s";
    } else if (per_sec >= 1e3) {
        os << std::setprecision(2) << (per_sec / 1e3) << " K" << unit << "/s";
    } else {
        os << std::setprecision(1) << per_sec << " " << unit << "/s";
    }
    return os.str();
}

void print_usage() {
    std::cerr
        << "Usage: bench_vm [options]\n"
        << "Options:\n"
        << "  --filter <substr>   Run only benchmarks whose name contains <substr>\n"
        << "  --fib <N>           Set N for fib(N) benchmark (default: 40)\n"
        << "  --runs <N>          Override number of timed runs per benchmark\n"
        << "  --no-native         Skip native C++ (-O2) reference baselines\n"
        << "  --list              List available benchmarks and exit\n";
}

// Compute fib(n) and exact recursive call count in O(n) for verification.
std::pair<int64_t, double> fib_expected(int32_t n) {
    if (n <= 0) return {0, 1.0};
    if (n == 1) return {1, 1.0};
    int64_t a = 0, b = 1;
    double ca = 1.0, cb = 1.0;
    for (int32_t i = 2; i <= n; ++i) {
        int64_t next = a + b;
        double cnext = ca + cb + 1.0;
        a = b; b = next;
        ca = cb; cb = cnext;
    }
    return {b, cb};
}

} // namespace

int main(int argc, char** argv) {
    std::string filter;
    int32_t fib_n = 40;
    int override_runs = 0;
    bool run_native = true;
    bool list_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (arg == "--fib" && i + 1 < argc) {
            fib_n = std::stoi(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            override_runs = std::stoi(argv[++i]);
        } else if (arg == "--no-native") {
            run_native = false;
        } else if (arg == "--list") {
            list_only = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage();
            return 1;
        }
    }

    auto [fib_val, fib_calls] = fib_expected(fib_n);
    int64_t expected_sum_loop = native_sum_loop(10000000);
    int64_t expected_array_sum = native_array_sum(65536, 50);

    // Expected sum for alloc_raw_bytes(20000): sum_{i=0}^{19999} (i & 255)
    int64_t expected_raw_bytes = 0;
    for (int i = 0; i < 20000; ++i) expected_raw_bytes += (i & 255);

    // Expected sum for alloc_ref_chain(100, 1000): 100 * (999 * 1000 / 2) = 49,950,000
    int64_t expected_ref_chain = 100LL * (999LL * 1000LL / 2LL);

    std::vector<BenchSpec> specs = {
        {
            "fib(" + std::to_string(fib_n) + ")",
            "benchmarks/fib.wat", "fib", {Value::from_i32(fib_n)},
            fib_val, (fib_n >= 38 ? 1 : 3), 4096,
            "calls", fib_calls,
            [fib_n]() -> int64_t { return native_fib(fib_n); }
        },
        {
            "sum_loop(10M)",
            "benchmarks/arith_loop.wat", "sum_loop", {Value::from_i32(10000000)},
            expected_sum_loop, 3, 4096,
            "iters", 10e6,
            []() -> int64_t { return native_sum_loop(10000000); }
        },
        {
            "sieve(100K)",
            "benchmarks/arith_loop.wat", "sieve", {Value::from_i32(100000)},
            9592, 5, 4096,
            "primes", 9592.0,
            nullptr
        },
        {
            "array_sum(64K x 50)",
            "benchmarks/array_ops.wat", "array_sum", {Value::from_i32(65536), Value::from_i32(50)},
            expected_array_sum, 3, 4096,
            "reads", 65536.0 * 50.0,
            []() -> int64_t { return native_array_sum(65536, 50); }
        },
        {
            "span_zero_copy(64KB x 20K)",
            "benchmarks/span_vs_copy.wat", "zero_copy_pass", {Value::from_i32(65536), Value::from_i32(20000)},
            20000LL * 21LL, 3, 4096,
            "passes", 20000.0,
            nullptr
        },
        {
            "span_copy_based(64KB x 20K)",
            "benchmarks/span_vs_copy.wat", "copy_based_pass", {Value::from_i32(65536), Value::from_i32(20000)},
            20000LL * 21LL, 3, 4096,
            "passes", 20000.0,
            nullptr
        },
        {
            "dispatch_direct(5M)",
            "benchmarks/dispatch.wat", "direct_calls", {Value::from_i32(5000000)},
            5000000, 3, 4096,
            "calls", 5e6,
            nullptr
        },
        {
            "dispatch_indirect(5M)",
            "benchmarks/dispatch.wat", "indirect_calls", {Value::from_i32(5000000)},
            5000000, 3, 4096,
            "calls", 5e6,
            nullptr
        },
        {
            "dispatch_call_ref(5M)",
            "benchmarks/dispatch.wat", "vtable_call_ref", {Value::from_i32(5000000)},
            5000000, 3, 4096,
            "calls", 5e6,
            nullptr
        },
        {
            "gc_raw_bytes(20K x 4KB)",
            "benchmarks/gc_stress.wat", "alloc_raw_bytes", {Value::from_i32(20000)},
            expected_raw_bytes, 3, 1024,
            "allocs", 20000.0,
            nullptr
        },
        {
            "gc_ref_chain(100 x 1K)",
            "benchmarks/gc_stress.wat", "alloc_ref_chain", {Value::from_i32(100), Value::from_i32(1000)},
            expected_ref_chain, 3, 2048,
            "nodes", 100000.0,
            nullptr
        },
    };

    if (list_only) {
        for (const auto& s : specs) {
            std::cout << "  " << s.name << "\n";
        }
        return 0;
    }

    std::cout << "=== Optrich VM 02 Interpreter Benchmark Suite ===\n\n";
    std::cout << std::left
              << std::setw(28) << "Benchmark"
              << std::right
              << std::setw(12) << "VM Best"
              << std::setw(12) << "VM Avg"
              << std::setw(18) << "Throughput"
              << std::setw(12) << "Native -O2"
              << std::setw(10) << "Ratio"
              << "  Status\n";
    std::cout << std::string(98, '-') << "\n";

    double zero_copy_ms = 0.0;
    double copy_based_ms = 0.0;

    for (const auto& spec : specs) {
        if (!filter.empty() && spec.name.find(filter) == std::string::npos) {
            continue;
        }

        int runs = (override_runs > 0) ? override_runs : spec.default_runs;
        std::string src = read_text_file(spec.wat_path);

        Runtime rt(spec.gc_threshold);
        ModuleInstance& inst = rt.instantiate_wat("__bench__", src);
        uint32_t fidx = inst.find_export(spec.func_name);
        const FuncSignature& sig = inst.module().func_sig(fidx);

        std::vector<double> times_ms;
        times_ms.reserve(static_cast<size_t>(runs));
        int64_t got = 0;

        for (int r = 0; r < runs; ++r) {
            auto t0 = Clock::now();
            auto res = rt.vm().invoke_index(inst, fidx, spec.args);
            auto t1 = Clock::now();
            got = extract_int_result(res, sig.results[0]);
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            times_ms.push_back(ms);
        }

        if (got != spec.expected) {
            std::cerr << "\nERROR: benchmark '" << spec.name << "' returned "
                      << got << ", expected " << spec.expected << "\n";
            return 1;
        }

        double best_ms = *std::min_element(times_ms.begin(), times_ms.end());
        double avg_ms = 0.0;
        for (double t : times_ms) avg_ms += t;
        avg_ms /= static_cast<double>(times_ms.size());

        if (spec.func_name == "zero_copy_pass") zero_copy_ms = best_ms;
        if (spec.func_name == "copy_based_pass") copy_based_ms = best_ms;

        std::string native_str = "-";
        std::string ratio_str = "-";
        if (run_native && spec.native_fn) {
            double native_best = 1e30;
            volatile int64_t sink = 0;
            for (int r = 0; r < std::max(runs, 3); ++r) {
                auto t0 = Clock::now();
                sink = spec.native_fn();
                auto t1 = Clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                if (ms < native_best) native_best = ms;
            }
            if (sink != spec.expected) {
                std::cerr << "\nERROR: native baseline '" << spec.name << "' mismatch\n";
                return 1;
            }
            native_str = format_duration(native_best);
            if (native_best > 0.0) {
                std::ostringstream ros;
                ros << std::fixed << std::setprecision(1) << (best_ms / native_best) << "x";
                ratio_str = ros.str();
            }
        }

        std::cout << std::left << std::setw(28) << spec.name
                  << std::right << std::setw(12) << format_duration(best_ms)
                  << std::setw(12) << format_duration(avg_ms)
                  << std::setw(18) << format_rate(spec.unit_count, best_ms, spec.unit_label)
                  << std::setw(12) << native_str
                  << std::setw(10) << ratio_str
                  << "  OK\n" << std::flush;
    }

    if (zero_copy_ms > 0.0 && copy_based_ms > 0.0) {
        std::cout << "\nZero-copy span speedup vs copy-based passing: "
                  << std::fixed << std::setprecision(1)
                  << (copy_based_ms / zero_copy_ms) << "x faster\n";
    }

    return 0;
}
