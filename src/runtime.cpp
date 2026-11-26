#include "optrich/runtime.hpp"

namespace optrich {

namespace {

std::string make_import_key(std::string_view mod, std::string_view field) {
    std::string k;
    k.reserve(mod.size() + 1 + field.size());
    k.append(mod);
    k.push_back('\0');
    k.append(field);
    return k;
}

} // namespace

Runtime::Runtime(size_t gc_threshold) {
    store_.set_gc_threshold(gc_threshold);
    store_.set_gc_callback([this]() { (void)this->collect_garbage(); });
}

void Runtime::register_host_func(std::string mod_name, std::string field_name,
                                 FuncSignature sig, HostFunc fn) {
    auto entry = std::make_unique<HostFuncEntry>(HostFuncEntry{std::move(sig), std::move(fn)});
    const HostFuncEntry* ptr = entry.get();
    hostEntries_.push_back(std::move(entry));
    hostLookup_[make_import_key(mod_name, field_name)] = ptr;
}

ModuleInstance& Runtime::instantiate(std::string name, CompiledModule mod) {
    // Link function imports
    std::vector<FuncRef> imported_funcs;
    imported_funcs.reserve(mod.funcImports.size());
    for (const auto& imp : mod.funcImports) {
        // 1. Check already-instantiated Wasm modules first
        auto mit = instances_.find(imp.moduleName);
        if (mit != instances_.end()) {
            ModuleInstance* target_mod = mit->second.get();
            FuncRef ref = target_mod->find_export_func(imp.fieldName);
            const FuncSignature& actual_sig =
                ref.is_host() ? ref.host->sig : ref.instance->module().funcs[ref.localFuncIdx].sig;
            if (actual_sig != imp.sig) {
                throw LinkError("import signature mismatch for '" + imp.moduleName + "." +
                                imp.fieldName + "'");
            }
            imported_funcs.push_back(ref);
            continue;
        }
        // 2. Check registered host functions
        auto hit = hostLookup_.find(make_import_key(imp.moduleName, imp.fieldName));
        if (hit != hostLookup_.end()) {
            if (hit->second->sig != imp.sig) {
                throw LinkError("host import signature mismatch for '" + imp.moduleName + "." +
                                imp.fieldName + "'");
            }
            imported_funcs.push_back(FuncRef{nullptr, 0, hit->second});
            continue;
        }
        throw LinkError("unresolved function import '" + imp.moduleName + "." + imp.fieldName + "'");
    }

    // Link table imports
    std::vector<std::shared_ptr<FuncTable>> imported_tables;
    imported_tables.reserve(mod.tableImports.size());
    for (const auto& timp : mod.tableImports) {
        auto mit = instances_.find(timp.moduleName);
        if (mit == instances_.end()) {
            throw LinkError("unresolved table import module '" + timp.moduleName + "'");
        }
        auto tbl = mit->second->find_export_table(timp.fieldName);
        if (tbl->elements.size() < timp.initialSize) {
            throw LinkError("imported table '" + timp.moduleName + "." + timp.fieldName +
                            "' is smaller than required initial size");
        }
        imported_tables.push_back(std::move(tbl));
    }

    auto inst = std::make_unique<ModuleInstance>(std::move(mod), store_,
                                                 std::move(imported_funcs),
                                                 std::move(imported_tables));
    ModuleInstance& ref = *inst;
    instances_[std::move(name)] = std::move(inst);
    return ref;
}

ModuleInstance& Runtime::instantiate_wat(std::string name, std::string_view wat_source) {
    return instantiate(std::move(name), compile_wat(wat_source));
}

ModuleInstance& Runtime::get_module(std::string_view name) {
    auto it = instances_.find(std::string(name));
    if (it == instances_.end()) {
        throw Trap("module not found: '" + std::string(name) + "'");
    }
    return *it->second;
}

const ModuleInstance& Runtime::get_module(std::string_view name) const {
    auto it = instances_.find(std::string(name));
    if (it == instances_.end()) {
        throw Trap("module not found: '" + std::string(name) + "'");
    }
    return *it->second;
}

std::vector<Value> Runtime::invoke(std::string_view mod_name, std::string_view export_name,
                                   const std::vector<Value>& args) {
    return vm_.invoke(get_module(mod_name), export_name, args);
}

GCStats Runtime::collect_garbage() {
    std::vector<HandleId> roots;
    vm_.collect_roots(roots);
    for (const auto& [name, inst] : instances_) {
        inst->collect_roots(roots);
    }
    return store_.collect_garbage(roots);
}

} // namespace optrich
