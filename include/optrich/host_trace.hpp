#pragma once

#include "optrich/runtime.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace optrich {

inline std::string read_handle_string(const MemoryStore& store, const FatHandle& h) {
    if (h.is_null()) return "<null>";
    uint32_t len = store.byte_len(h.id);
    std::string out(len, '\0');
    if (len > 0) {
        store.read_bytes(h.id, 0, out.data(), len);
    }
    return out;
}

inline std::string format_f64_clean(double v) {
    std::ostringstream oss;
    oss << std::setprecision(12) << v;
    return oss.str();
}

// Register standard `host::trace_*` functions on `rt`, writing formatted lines to `out`.
inline void register_host_trace(Runtime& rt, std::ostream& out) {
    rt.register_host_func(
        "host", "trace_i32",
        FuncSignature{{ValType::RoHandle, ValType::I32}, {}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            out << read_handle_string(store, args[0].handle) << ": " << args[1].i32 << "\n";
            return {};
        });

    rt.register_host_func(
        "host", "trace_i64",
        FuncSignature{{ValType::RoHandle, ValType::I64}, {}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            out << read_handle_string(store, args[0].handle) << ": " << args[1].i64 << "\n";
            return {};
        });

    rt.register_host_func(
        "host", "trace_f64",
        FuncSignature{{ValType::RoHandle, ValType::F64}, {}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            out << read_handle_string(store, args[0].handle) << ": " << format_f64_clean(args[1].f64) << "\n";
            return {};
        });

    rt.register_host_func(
        "host", "trace_str",
        FuncSignature{{ValType::RoHandle, ValType::RoHandle}, {}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            out << read_handle_string(store, args[0].handle) << ": \""
                << read_handle_string(store, args[1].handle) << "\"\n";
            return {};
        });

    rt.register_host_func(
        "host", "trace_meta",
        FuncSignature{{ValType::RoHandle, ValType::Handle}, {}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            std::string label = read_handle_string(store, args[0].handle);
            const FatHandle& h = args[1].handle;
            if (h.is_null()) {
                out << label << ": null\n";
            } else {
                out << label << ": ro=" << (store.is_readonly(h.id) ? "true" : "false")
                    << " bytes=" << store.byte_len(h.id)
                    << " refs=" << store.ref_len(h.id) << "\n";
            }
            return {};
        });

    rt.register_host_func(
        "host", "trace_gc",
        FuncSignature{{ValType::RoHandle}, {}},
        [&rt, &out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            std::string label = read_handle_string(store, args[0].handle);
            GCStats stats = rt.collect_garbage();
            out << label << ": live=" << stats.liveAfter << " swept=" << stats.swept << "\n";
            return {};
        });
}

} // namespace optrich
