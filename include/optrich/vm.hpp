#pragma once

#include "optrich/bytecode.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/types.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace optrich {

// An instantiated module bound to a shared MemoryStore.
class ModuleInstance {
public:
    ModuleInstance(CompiledModule module, MemoryStore& store);

    const CompiledModule& module() const { return module_; }
    MemoryStore&          store() { return store_; }
    const MemoryStore&    store() const { return store_; }

    std::vector<Value>&       globals() { return globals_; }
    const std::vector<Value>& globals() const { return globals_; }

    const std::vector<FatHandle>& string_handles() const { return stringHandles_; }

    // Look up exported function index (throws Trap if not found).
    uint32_t find_export(std::string_view name) const;

private:
    CompiledModule         module_;
    MemoryStore&           store_;
    std::vector<Value>     globals_;
    std::vector<FatHandle> stringHandles_;
};

// Fast flat-stack computed-goto bytecode interpreter.
class VM {
public:
    explicit VM(size_t max_stack_values = 65536, size_t max_call_depth = 1024);

    // Invoke an exported function by name.
    std::vector<Value> invoke(ModuleInstance& inst, std::string_view export_name,
                              const std::vector<Value>& args = {});

    // Invoke a function by compiled index.
    std::vector<Value> invoke_index(ModuleInstance& inst, uint32_t func_idx,
                                    const std::vector<Value>& args = {});

private:
    struct CallFrame {
        const CompiledFunc* func;
        const Instr*        pc;
        size_t              localsBase; // index into stack_ where frame's locals begin
        size_t              stackBase;  // index into stack_ where frame's operand stack begins
    };

    size_t                 maxStack_;
    size_t                 maxCallDepth_;
    std::vector<Value>     stack_;
    std::vector<CallFrame> frames_;
};

} // namespace optrich
