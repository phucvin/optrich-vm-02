#pragma once

#include "optrich/compiler.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/vm.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace optrich {

// Unified multi-module Runtime + Linker hosting multiple ModuleInstances
// over a single shared MemoryStore and precise cross-module GC.
class Runtime {
public:
    explicit Runtime(size_t gc_threshold = 4096);

    MemoryStore&       store() { return store_; }
    const MemoryStore& store() const { return store_; }
    VM&                vm() { return vm_; }

    // Register a host function available for modules to import as `(import mod field ...)`.
    void register_host_func(std::string mod_name, std::string field_name,
                            FuncSignature sig, HostFunc fn);

    // Link and instantiate a compiled module under `name`.
    ModuleInstance& instantiate(std::string name, CompiledModule mod);

    // Parse, compile, link, and instantiate WAT source under `name`.
    ModuleInstance& instantiate_wat(std::string name, std::string_view wat_source);

    // Look up an already-instantiated module by name.
    ModuleInstance& get_module(std::string_view name);
    const ModuleInstance& get_module(std::string_view name) const;

    // Invoke an exported function on a hosted module.
    std::vector<Value> invoke(std::string_view mod_name, std::string_view export_name,
                              const std::vector<Value>& args = {});

    // Run precise mark-and-sweep GC across ALL hosted modules, active VM call frames,
    // operand stack, globals, string constants, and host-pinned handles.
    GCStats collect_garbage();

private:
    MemoryStore store_;
    VM          vm_;

    std::unordered_map<std::string, std::unique_ptr<ModuleInstance>> instances_;
    std::vector<std::unique_ptr<HostFuncEntry>>                      hostEntries_;
    std::unordered_map<std::string, const HostFuncEntry*>            hostLookup_;
};

} // namespace optrich
