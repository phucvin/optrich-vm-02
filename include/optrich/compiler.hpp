#pragma once

#include "optrich/bytecode.hpp"
#include "optrich/parser.hpp"

#include <string_view>

namespace optrich {

// Compile and validate a parsed WAT `(module ...)` S-expression into
// pre-resolved bytecode (all $names lowered to integer indices, branch targets
// precomputed, types & static handle capabilities validated).
CompiledModule compile_module(const SExpr& root);

// Convenience helper: parse + compile WAT source text.
CompiledModule compile_wat(std::string_view source);

} // namespace optrich
