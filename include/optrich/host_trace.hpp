#pragma once

#include "optrich/runtime.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <memory>
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

    // Capability-safe host I/O helpers for Iteration 7+ golden tests:
    // - io_open_reader(seed: ro_handle) -> ro_handle (opaque unforgeable stream handle)
    // - io_read(stream: ro_handle, dst: rw_handle) -> i32 (bytes read, 0 on EOF)
    // - io_write(label: ro_handle, data: ro_handle) -> i32 (logs and returns bytes written)
    struct HostReaderState {
        struct Stream {
            std::string data;
            size_t      pos = 0;
        };
        std::vector<Stream> streams;
    };
    auto io_state = std::make_shared<HostReaderState>();

    rt.register_host_func(
        "host", "io_open_reader",
        FuncSignature{{ValType::RoHandle}, {ValType::RoHandle}},
        [io_state](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            std::string data = read_handle_string(store, args[0].handle);
            uint32_t sid = static_cast<uint32_t>(io_state->streams.size());
            io_state->streams.push_back(HostReaderState::Stream{std::move(data), 0});
            std::string id_bytes(4, '\0');
            std::memcpy(id_bytes.data(), &sid, 4);
            FatHandle h = store.alloc_readonly_bytes(id_bytes);
            return {Value::from_handle(h)};
        });

    rt.register_host_func(
        "host", "io_read",
        FuncSignature{{ValType::RoHandle, ValType::RwHandle}, {ValType::I32}},
        [io_state](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            uint32_t sid = 0;
            store.read_bytes(args[0].handle.id, 0, &sid, 4);
            if (sid >= io_state->streams.size()) {
                throw Trap("host::io_read invalid stream handle");
            }
            auto& st = io_state->streams[sid];
            uint32_t cap = store.byte_len(args[1].handle.id);
            size_t avail = (st.pos < st.data.size()) ? (st.data.size() - st.pos) : 0;
            uint32_t n = static_cast<uint32_t>(std::min<size_t>(cap, avail));
            if (n > 0) {
                store.write_bytes(args[1].handle.id, 0, st.data.data() + st.pos, n);
                st.pos += n;
            }
            return {Value::from_i32(static_cast<int32_t>(n))};
        });

    rt.register_host_func(
        "host", "io_write",
        FuncSignature{{ValType::RoHandle, ValType::RoHandle}, {ValType::I32}},
        [&out](MemoryStore& store, const std::vector<Value>& args) -> std::vector<Value> {
            std::string label = read_handle_string(store, args[0].handle);
            std::string data  = read_handle_string(store, args[1].handle);
            out << label << ": \"" << data << "\"\n";
            return {Value::from_i32(static_cast<int32_t>(data.size()))};
        });
}

} // namespace optrich
