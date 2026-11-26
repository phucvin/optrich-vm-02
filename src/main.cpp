#include "optrich/compiler.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/vm.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace optrich;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: optrich-vm <file.wat> [export_func] [args...]\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "Error: cannot open file '" << argv[1] << "'\n";
        return 1;
    }
    std::ostringstream ss;
    ss << file.rdbuf();

    std::string func_name = (argc >= 3) ? argv[2] : "main";

    try {
        CompiledModule mod = compile_wat(ss.str());
        MemoryStore store;
        ModuleInstance inst(std::move(mod), store);

        uint32_t fidx = inst.find_export(func_name);
        const FuncSignature& sig = inst.module().funcs[fidx].sig;

        std::vector<Value> args;
        if (static_cast<size_t>(argc > 3 ? argc - 3 : 0) != sig.params.size()) {
            std::cerr << "Error: function '" << func_name << "' expects " << sig.params.size()
                      << " argument(s), got " << (argc > 3 ? argc - 3 : 0) << "\n";
            return 1;
        }
        for (size_t i = 0; i < sig.params.size(); ++i) {
            std::string arg_str = argv[3 + i];
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

        VM vm;
        auto results = vm.invoke_index(inst, fidx, args);
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
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
