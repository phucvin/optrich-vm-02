#pragma once

#include "optrich/bytecode.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/types.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace optrich {

class ModuleInstance;

using HostFunc = std::function<std::vector<Value>(MemoryStore&, const std::vector<Value>&)>;

struct HostFuncEntry {
    FuncSignature sig;
    HostFunc      fn;
};

struct FuncTable {
    std::vector<FuncRef> elements;
};

// An instantiated module bound to a shared MemoryStore.
class ModuleInstance {
public:
    // Standalone constructor (for modules with no imports).
    ModuleInstance(CompiledModule module, MemoryStore& store);

    // Linked constructor (used by Runtime/Linker for multi-module instantiation).
    ModuleInstance(CompiledModule module, MemoryStore& store,
                   std::vector<FuncRef> imported_funcs,
                   std::vector<std::shared_ptr<FuncTable>> imported_tables);

    const CompiledModule& module() const { return module_; }
    MemoryStore&          store() { return store_; }
    const MemoryStore&    store() const { return store_; }

    std::vector<Value>&       globals() { return globals_; }
    const std::vector<Value>& globals() const { return globals_; }

    const std::vector<FatHandle>& string_handles() const { return stringHandles_; }

    const FuncRef& func_ref(uint32_t unified_idx) const { return funcs_[unified_idx]; }
    FuncTable&     table(uint32_t unified_idx) { return *tables_[unified_idx]; }
    const FuncTable& table(uint32_t unified_idx) const { return *tables_[unified_idx]; }
    std::shared_ptr<FuncTable> table_ptr(uint32_t unified_idx) const { return tables_[unified_idx]; }

    // Look up exported function (returns resolved FuncRef) or table.
    uint32_t find_export(std::string_view name) const;
    FuncRef  find_export_func(std::string_view name) const;
    std::shared_ptr<FuncTable> find_export_table(std::string_view name) const;

    // Gather all GC handle roots owned by this instance (globals + string constants).
    void collect_roots(std::vector<HandleId>& out) const;

private:
    void init_storage_and_elems();

    CompiledModule                          module_;
    MemoryStore&                            store_;
    std::vector<Value>                      globals_;
    std::vector<FatHandle>                  stringHandles_;
    std::vector<FuncRef>                    funcs_;  // unified [imports..., locals...]
    std::vector<std::shared_ptr<FuncTable>> tables_; // unified [imports..., locals...]
};

// Fast flat-stack computed-goto bytecode interpreter supporting direct cross-module calls,
// cross-module call_indirect, and precise GC root scanning.
class VM {
public:
    explicit VM(size_t max_stack_values = 65536, size_t max_call_depth = 1024);

    // Invoke an exported function by name on `inst`.
    std::vector<Value> invoke(ModuleInstance& inst, std::string_view export_name,
                              const std::vector<Value>& args = {});

    // Invoke a function by unified index on `inst`.
    std::vector<Value> invoke_index(ModuleInstance& inst, uint32_t func_idx,
                                    const std::vector<Value>& args = {});

    // Collect all live handle roots currently on the operand stack & active call frames.
    void collect_roots(std::vector<HandleId>& out) const;

private:
    struct CallFrame {
        ModuleInstance*     inst;
        const CompiledFunc* func;
        const Instr*        pc;
        size_t              localsBase;
        size_t              stackBase;
    };

    size_t                 maxStack_;
    size_t                 maxCallDepth_;
    size_t                 activeSp_ = 0; // high-water mark of live stack slots for GC
    std::vector<Value>     stack_;
    std::vector<CallFrame> frames_;
};

} // namespace optrich
