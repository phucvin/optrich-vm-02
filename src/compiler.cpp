#include "optrich/compiler.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace optrich {

namespace {

std::optional<ValType> parse_val_type(std::string_view s) {
    if (s == "i32")       return ValType::I32;
    if (s == "i64")       return ValType::I64;
    if (s == "f32")       return ValType::F32;
    if (s == "f64")       return ValType::F64;
    if (s == "handle")    return ValType::Handle;
    if (s == "rw_handle") return ValType::RwHandle;
    if (s == "ro_handle") return ValType::RoHandle;
    return std::nullopt;
}

ValType expect_val_type(const SExpr& node) {
    if (!node.is_atom()) {
        throw CompileError("expected value type atom at line " + std::to_string(node.line));
    }
    auto t = parse_val_type(node.text);
    if (!t) {
        throw CompileError("unknown value type '" + node.text + "' at line " + std::to_string(node.line));
    }
    return *t;
}

int32_t parse_i32_literal(const std::string& s, size_t line) {
    try {
        size_t idx = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            uint64_t v = std::stoull(s, &idx, 16);
            if (idx != s.size() || v > 0xFFFFFFFFULL) throw std::out_of_range("i32");
            return static_cast<int32_t>(static_cast<uint32_t>(v));
        }
        if (s.size() > 3 && s[0] == '-' && s[1] == '0' && (s[2] == 'x' || s[2] == 'X')) {
            int64_t v = std::stoll(s, &idx, 16);
            if (idx != s.size()) throw std::out_of_range("i32");
            return static_cast<int32_t>(v);
        }
        int64_t v = std::stoll(s, &idx, 10);
        if (idx != s.size()) throw std::out_of_range("i32");
        if (v < std::numeric_limits<int32_t>::min() || v > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::out_of_range("i32");
        }
        return static_cast<int32_t>(v);
    } catch (...) {
        throw CompileError("invalid i32 literal '" + s + "' at line " + std::to_string(line));
    }
}

int64_t parse_i64_literal(const std::string& s, size_t line) {
    try {
        size_t idx = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            uint64_t v = std::stoull(s, &idx, 16);
            if (idx != s.size()) throw std::out_of_range("i64");
            return static_cast<int64_t>(v);
        }
        if (s.size() > 3 && s[0] == '-' && s[1] == '0' && (s[2] == 'x' || s[2] == 'X')) {
            int64_t v = std::stoll(s, &idx, 16);
            if (idx != s.size()) throw std::out_of_range("i64");
            return v;
        }
        if (!s.empty() && s[0] != '-') {
            uint64_t v = std::stoull(s, &idx, 10);
            if (idx != s.size()) throw std::out_of_range("i64");
            return static_cast<int64_t>(v);
        }
        int64_t v = std::stoll(s, &idx, 10);
        if (idx != s.size()) throw std::out_of_range("i64");
        return v;
    } catch (...) {
        throw CompileError("invalid i64 literal '" + s + "' at line " + std::to_string(line));
    }
}

float parse_f32_literal(const std::string& s, size_t line) {
    if (s == "inf" || s == "+inf") return std::numeric_limits<float>::infinity();
    if (s == "-inf") return -std::numeric_limits<float>::infinity();
    if (s == "nan" || s == "+nan") return std::numeric_limits<float>::quiet_NaN();
    if (s == "-nan") return -std::numeric_limits<float>::quiet_NaN();
    try {
        size_t idx = 0;
        float v = std::stof(s, &idx);
        if (idx != s.size()) throw std::out_of_range("f32");
        return v;
    } catch (...) {
        throw CompileError("invalid f32 literal '" + s + "' at line " + std::to_string(line));
    }
}

double parse_f64_literal(const std::string& s, size_t line) {
    if (s == "inf" || s == "+inf") return std::numeric_limits<double>::infinity();
    if (s == "-inf") return -std::numeric_limits<double>::infinity();
    if (s == "nan" || s == "+nan") return std::numeric_limits<double>::quiet_NaN();
    if (s == "-nan") return -std::numeric_limits<double>::quiet_NaN();
    try {
        size_t idx = 0;
        double v = std::stod(s, &idx);
        if (idx != s.size()) throw std::out_of_range("f64");
        return v;
    } catch (...) {
        throw CompileError("invalid f64 literal '" + s + "' at line " + std::to_string(line));
    }
}

bool is_ident(const SExpr& node) {
    return node.is_atom() && !node.text.empty() && node.text[0] == '$';
}

uint32_t parse_u32_index(const std::string& s, size_t line) {
    try {
        size_t idx = 0;
        unsigned long v = std::stoul(s, &idx, 10);
        if (idx != s.size()) throw std::out_of_range("u32");
        return static_cast<uint32_t>(v);
    } catch (...) {
        throw CompileError("expected integer index, got '" + s + "' at line " + std::to_string(line));
    }
}

struct ModuleContext {
    CompiledModule mod;
    std::unordered_map<std::string, uint32_t> typeNames;
    std::unordered_map<std::string, uint32_t> funcNames;
    std::unordered_map<std::string, uint32_t> globalNames;
    std::unordered_map<std::string, uint32_t> stringNames;

    uint32_t resolve_type(const SExpr& node) const {
        if (is_ident(node)) {
            auto it = typeNames.find(node.text);
            if (it == typeNames.end()) {
                throw CompileError("unknown type '" + node.text + "' at line " + std::to_string(node.line));
            }
            return it->second;
        }
        uint32_t idx = parse_u32_index(node.text, node.line);
        if (idx >= mod.types.size()) {
            throw CompileError("type index out of range at line " + std::to_string(node.line));
        }
        return idx;
    }

    uint32_t resolve_func(const SExpr& node) const {
        if (is_ident(node)) {
            auto it = funcNames.find(node.text);
            if (it == funcNames.end()) {
                throw CompileError("unknown function '" + node.text + "' at line " + std::to_string(node.line));
            }
            return it->second;
        }
        uint32_t idx = parse_u32_index(node.text, node.line);
        if (idx >= mod.funcs.size()) {
            throw CompileError("function index out of range at line " + std::to_string(node.line));
        }
        return idx;
    }

    uint32_t resolve_global(const SExpr& node) const {
        if (is_ident(node)) {
            auto it = globalNames.find(node.text);
            if (it == globalNames.end()) {
                throw CompileError("unknown global '" + node.text + "' at line " + std::to_string(node.line));
            }
            return it->second;
        }
        uint32_t idx = parse_u32_index(node.text, node.line);
        if (idx >= mod.globals.size()) {
            throw CompileError("global index out of range at line " + std::to_string(node.line));
        }
        return idx;
    }

    uint32_t resolve_or_add_string(const SExpr& node) {
        if (node.is_string()) {
            uint32_t idx = static_cast<uint32_t>(mod.strings.size());
            mod.strings.push_back(node.text);
            return idx;
        }
        if (is_ident(node)) {
            auto it = stringNames.find(node.text);
            if (it == stringNames.end()) {
                throw CompileError("unknown string '" + node.text + "' at line " + std::to_string(node.line));
            }
            return it->second;
        }
        uint32_t idx = parse_u32_index(node.text, node.line);
        if (idx >= mod.strings.size()) {
            throw CompileError("string index out of range at line " + std::to_string(node.line));
        }
        return idx;
    }
};

class FuncCompiler {
public:
    FuncCompiler(ModuleContext& mctx, CompiledFunc& fn,
                 std::unordered_map<std::string, uint32_t> local_names)
        : mctx_(mctx), fn_(fn), localNames_(std::move(local_names)) {}

    void compile_body(const std::vector<SExpr>& nodes, size_t start_idx) {
        // Push function-level control frame
        CtrlFrame root;
        root.kind = CtrlKind::Function;
        root.resultTypes = fn_.sig.results;
        root.stackHeight = 0;
        root.startPc = 0;
        ctrlStack_.push_back(std::move(root));

        compile_sequence(nodes, start_idx, nodes.size());

        // Must have only the function frame left
        if (ctrlStack_.size() != 1) {
            throw CompileError("unclosed block/loop/if in function '" + fn_.name + "'");
        }

        // Validate function results
        CtrlFrame& f = ctrlStack_.back();
        validate_frame_exit(f, 0);

        uint32_t end_pc = static_cast<uint32_t>(fn_.code.size());
        patch_frame_ends(f, end_pc);
        fn_.code.push_back(Instr::make(OpCode::Return));
        ctrlStack_.pop_back();
    }

private:
    enum class CtrlKind { Function, Block, Loop, If };

    struct BrTablePatch {
        uint32_t tableIdx;
        int32_t  entryIdx; // -1 for defaultTarget
    };

    struct CtrlFrame {
        CtrlKind             kind = CtrlKind::Block;
        std::string          label;
        std::vector<ValType> resultTypes;
        size_t               stackHeight = 0;
        uint32_t             startPc     = 0;
        bool                 polymorphic = false;
        bool                 hasElse     = false;
        uint32_t             ifJumpPc    = 0; // BrIfZero to else/end
        uint32_t             elseJumpPc  = 0; // Br from end of then to end
        std::vector<uint32_t>     endPatches;
        std::vector<BrTablePatch> brTablePatches;
    };

    // Sentinel for polymorphic stack values (produced in unreachable code).
    static constexpr ValType kUnknownType = static_cast<ValType>(255);

    void push_type(ValType t) {
        typeStack_.push_back(t);
    }

    ValType pop_any(size_t line) {
        CtrlFrame& f = ctrlStack_.back();
        if (typeStack_.size() == f.stackHeight) {
            if (f.polymorphic) return kUnknownType;
            throw CompileError("stack underflow at line " + std::to_string(line));
        }
        ValType t = typeStack_.back();
        typeStack_.pop_back();
        return t;
    }

    ValType pop_expected(ValType expected, size_t line) {
        ValType actual = pop_any(line);
        if (actual == kUnknownType || expected == kUnknownType) return expected;
        if (!is_type_compatible(actual, expected)) {
            throw CompileError(std::string("type mismatch: expected ") + val_type_name(expected) +
                               ", got " + val_type_name(actual) + " at line " + std::to_string(line));
        }
        return actual;
    }

    ValType pop_handle(size_t line, bool require_writable = false) {
        ValType actual = pop_any(line);
        if (actual == kUnknownType) return ValType::Handle;
        if (!is_handle_type(actual)) {
            throw CompileError(std::string("type mismatch: expected handle, got ") +
                               val_type_name(actual) + " at line " + std::to_string(line));
        }
        if (require_writable && actual == ValType::RoHandle) {
            throw CompileError("cannot write to statically read-only handle (ro_handle) at line " +
                               std::to_string(line));
        }
        return actual;
    }

    void set_polymorphic() {
        CtrlFrame& f = ctrlStack_.back();
        typeStack_.resize(f.stackHeight);
        f.polymorphic = true;
    }

    void validate_frame_exit(const CtrlFrame& f, size_t line) {
        if (!f.polymorphic) {
            size_t expected_height = f.stackHeight + f.resultTypes.size();
            if (typeStack_.size() != expected_height) {
                throw CompileError("stack height mismatch at block end (expected " +
                                   std::to_string(f.resultTypes.size()) + " values, got " +
                                   std::to_string(typeStack_.size() - f.stackHeight) +
                                   ") at line " + std::to_string(line));
            }
            for (size_t i = 0; i < f.resultTypes.size(); ++i) {
                ValType got = typeStack_[f.stackHeight + i];
                if (got != kUnknownType && !is_type_compatible(got, f.resultTypes[i])) {
                    throw CompileError(std::string("block result type mismatch: expected ") +
                                       val_type_name(f.resultTypes[i]) + ", got " +
                                       val_type_name(got) + " at line " + std::to_string(line));
                }
            }
        } else {
            // In unreachable tail, stack must be compatible if anything was pushed
            size_t extra = typeStack_.size() - f.stackHeight;
            if (extra > f.resultTypes.size()) {
                throw CompileError("extra values on stack at block end at line " + std::to_string(line));
            }
            for (size_t i = 0; i < extra; ++i) {
                ValType got = typeStack_[f.stackHeight + i];
                ValType exp = f.resultTypes[f.resultTypes.size() - extra + i];
                if (got != kUnknownType && !is_type_compatible(got, exp)) {
                    throw CompileError("block result type mismatch at line " + std::to_string(line));
                }
            }
        }
    }

    void patch_frame_ends(const CtrlFrame& f, uint32_t end_pc) {
        for (uint32_t pc : f.endPatches) {
            fn_.code[pc].imm.u32 = end_pc;
        }
        for (const auto& bp : f.brTablePatches) {
            BrTable& bt = fn_.brTables[bp.tableIdx];
            if (bp.entryIdx < 0) {
                bt.defaultTarget.targetPc = end_pc;
            } else {
                bt.targets[static_cast<size_t>(bp.entryIdx)].targetPc = end_pc;
            }
        }
    }

    uint32_t resolve_local(const SExpr& node) const {
        if (is_ident(node)) {
            auto it = localNames_.find(node.text);
            if (it == localNames_.end()) {
                throw CompileError("unknown local '" + node.text + "' at line " + std::to_string(node.line));
            }
            return it->second;
        }
        uint32_t idx = parse_u32_index(node.text, node.line);
        if (idx >= fn_.locals.size()) {
            throw CompileError("local index out of range at line " + std::to_string(node.line));
        }
        return idx;
    }

    // Resolve branch label ($name or numeric depth 0 = innermost).
    // Returns index into ctrlStack_.
    size_t resolve_label(const SExpr& node) const {
        if (is_ident(node)) {
            for (size_t i = ctrlStack_.size(); i > 0; --i) {
                if (ctrlStack_[i - 1].label == node.text) {
                    return i - 1;
                }
            }
            throw CompileError("unknown label '" + node.text + "' at line " + std::to_string(node.line));
        }
        uint32_t depth = parse_u32_index(node.text, node.line);
        if (depth >= ctrlStack_.size()) {
            throw CompileError("branch depth " + std::to_string(depth) +
                               " out of range at line " + std::to_string(node.line));
        }
        return ctrlStack_.size() - 1 - depth;
    }

    // Check branch target values on stack and compute (arity, dropCount).
    std::pair<uint16_t, uint32_t> check_branch_stack(size_t target_idx, size_t line, bool pop_values) {
        const CtrlFrame& target = ctrlStack_[target_idx];
        const std::vector<ValType>& br_types =
            (target.kind == CtrlKind::Loop) ? emptyTypes_ : target.resultTypes;
        uint16_t arity = static_cast<uint16_t>(br_types.size());

        std::vector<ValType> popped;
        popped.reserve(arity);
        for (size_t i = arity; i > 0; --i) {
            popped.push_back(pop_expected(br_types[i - 1], line));
        }
        uint32_t drop_count = 0;
        if (!ctrlStack_.back().polymorphic) {
            drop_count = static_cast<uint32_t>(typeStack_.size() - target.stackHeight);
        }
        if (!pop_values) {
            // Restore values for br_if fallthrough
            for (size_t i = popped.size(); i > 0; --i) {
                push_type(popped[i - 1]);
            }
        }
        return {arity, drop_count};
    }

    // Compile a flat sequence of WAT instructions (which may include folded S-exprs).
    void compile_sequence(const std::vector<SExpr>& nodes, size_t start, size_t end) {
        size_t i = start;
        while (i < end) {
            const SExpr& node = nodes[i];
            if (node.is_list()) {
                compile_folded(node);
                ++i;
            } else if (node.is_atom()) {
                i = compile_flat_instr(nodes, i, end);
            } else {
                throw CompileError("unexpected string literal in instruction stream at line " +
                                   std::to_string(node.line));
            }
        }
    }

    // Compile a folded S-expression instruction, e.g.:
    //   (i32.add (local.get $x) (i32.const 1))
    //   (block $b (result i32) ...)
    //   (if (result i32) <cond> (then ...) (else ...))
    void compile_folded(const SExpr& expr) {
        if (expr.children.empty() || !expr.children[0].is_atom()) {
            throw CompileError("invalid folded instruction at line " + std::to_string(expr.line));
        }
        const std::string& op = expr.children[0].text;
        size_t line = expr.children[0].line;

        if (op == "block" || op == "loop") {
            size_t idx = 1;
            std::string label;
            if (idx < expr.children.size() && is_ident(expr.children[idx])) {
                label = expr.children[idx++].text;
            }
            std::vector<ValType> results;
            while (idx < expr.children.size() && expr.children[idx].is_list_with_head("result")) {
                for (size_t k = 1; k < expr.children[idx].children.size(); ++k) {
                    results.push_back(expect_val_type(expr.children[idx].children[k]));
                }
                ++idx;
            }
            enter_block(op == "loop" ? CtrlKind::Loop : CtrlKind::Block, label, std::move(results));
            compile_sequence(expr.children, idx, expr.children.size());
            exit_block(line);
            return;
        }

        if (op == "if") {
            size_t idx = 1;
            std::string label;
            if (idx < expr.children.size() && is_ident(expr.children[idx])) {
                label = expr.children[idx++].text;
            }
            std::vector<ValType> results;
            while (idx < expr.children.size() && expr.children[idx].is_list_with_head("result")) {
                for (size_t k = 1; k < expr.children[idx].children.size(); ++k) {
                    results.push_back(expect_val_type(expr.children[idx].children[k]));
                }
                ++idx;
            }
            // Any expressions before (then ...) are folded condition operands
            size_t then_idx = idx;
            while (then_idx < expr.children.size() && !expr.children[then_idx].is_list_with_head("then")) {
                ++then_idx;
            }
            compile_sequence(expr.children, idx, then_idx);
            enter_if(label, std::move(results), line);
            if (then_idx < expr.children.size()) {
                const SExpr& then_node = expr.children[then_idx];
                compile_sequence(then_node.children, 1, then_node.children.size());
                size_t next_idx = then_idx + 1;
                if (next_idx < expr.children.size() && expr.children[next_idx].is_list_with_head("else")) {
                    enter_else(expr.children[next_idx].line);
                    const SExpr& else_node = expr.children[next_idx];
                    compile_sequence(else_node.children, 1, else_node.children.size());
                    ++next_idx;
                }
                if (next_idx != expr.children.size()) {
                    throw CompileError("unexpected tokens after if/then/else at line " + std::to_string(line));
                }
            }
            exit_block(line);
            return;
        }

        // Standard folded instruction: immediates come immediately after opcode,
        // followed by folded operand sub-expressions (lists).
        size_t imm_count = immediate_count(op);
        if (op == "br_table") {
            // (br_table $l0 $l1 ... <index-expr>)
            size_t operand_start = expr.children.size();
            while (operand_start > 1 && expr.children[operand_start - 1].is_list()) {
                --operand_start;
            }
            // Compile folded operands first (pushed onto stack)
            compile_sequence(expr.children, operand_start, expr.children.size());
            // Emit br_table with labels [1 .. operand_start)
            emit_br_table(expr.children, 1, operand_start, line);
            return;
        }

        if (1 + imm_count > expr.children.size()) {
            throw CompileError("missing immediate for '" + op + "' at line " + std::to_string(line));
        }
        // Compile folded operand sub-expressions first (left-to-right)
        compile_sequence(expr.children, 1 + imm_count, expr.children.size());
        // Now emit the opcode with its immediates
        emit_opcode(op, expr.children, 1, line);
    }

    // Compile one flat instruction starting at nodes[i], returning the next index.
    size_t compile_flat_instr(const std::vector<SExpr>& nodes, size_t i, size_t end) {
        const std::string& op = nodes[i].text;
        size_t line = nodes[i].line;

        if (op == "block" || op == "loop") {
            size_t idx = i + 1;
            std::string label;
            if (idx < end && is_ident(nodes[idx])) {
                label = nodes[idx++].text;
            }
            std::vector<ValType> results;
            while (idx < end && nodes[idx].is_list_with_head("result")) {
                for (size_t k = 1; k < nodes[idx].children.size(); ++k) {
                    results.push_back(expect_val_type(nodes[idx].children[k]));
                }
                ++idx;
            }
            enter_block(op == "loop" ? CtrlKind::Loop : CtrlKind::Block, label, std::move(results));
            return idx;
        }

        if (op == "if") {
            size_t idx = i + 1;
            std::string label;
            if (idx < end && is_ident(nodes[idx])) {
                label = nodes[idx++].text;
            }
            std::vector<ValType> results;
            while (idx < end && nodes[idx].is_list_with_head("result")) {
                for (size_t k = 1; k < nodes[idx].children.size(); ++k) {
                    results.push_back(expect_val_type(nodes[idx].children[k]));
                }
                ++idx;
            }
            enter_if(label, std::move(results), line);
            return idx;
        }

        if (op == "else") {
            enter_else(line);
            return i + 1;
        }

        if (op == "end") {
            exit_block(line);
            return i + 1;
        }

        if (op == "br_table") {
            size_t idx = i + 1;
            while (idx < end && nodes[idx].is_atom() &&
                   (is_ident(nodes[idx]) || (!nodes[idx].text.empty() && std::isdigit(static_cast<unsigned char>(nodes[idx].text[0]))))) {
                ++idx;
            }
            emit_br_table(nodes, i + 1, idx, line);
            return idx;
        }

        size_t imm_count = immediate_count(op);
        if (i + 1 + imm_count > end) {
            throw CompileError("missing immediate for '" + op + "' at line " + std::to_string(line));
        }
        emit_opcode(op, nodes, i + 1, line);
        return i + 1 + imm_count;
    }

    void enter_block(CtrlKind kind, std::string label, std::vector<ValType> results) {
        CtrlFrame f;
        f.kind = kind;
        f.label = std::move(label);
        f.resultTypes = std::move(results);
        f.stackHeight = typeStack_.size();
        f.startPc = static_cast<uint32_t>(fn_.code.size());
        ctrlStack_.push_back(std::move(f));
    }

    void enter_if(std::string label, std::vector<ValType> results, size_t line) {
        pop_expected(ValType::I32, line);
        CtrlFrame f;
        f.kind = CtrlKind::If;
        f.label = std::move(label);
        f.resultTypes = std::move(results);
        f.stackHeight = typeStack_.size();
        f.startPc = static_cast<uint32_t>(fn_.code.size());
        f.ifJumpPc = static_cast<uint32_t>(fn_.code.size());
        fn_.code.push_back(Instr::make_branch(OpCode::BrIfZero, 0, 0, 0));
        ctrlStack_.push_back(std::move(f));
    }

    void enter_else(size_t line) {
        if (ctrlStack_.size() <= 1 || ctrlStack_.back().kind != CtrlKind::If) {
            throw CompileError("unexpected 'else' outside of 'if' at line " + std::to_string(line));
        }
        CtrlFrame& f = ctrlStack_.back();
        if (f.hasElse) {
            throw CompileError("duplicate 'else' in 'if' at line " + std::to_string(line));
        }
        validate_frame_exit(f, line);
        f.elseJumpPc = static_cast<uint32_t>(fn_.code.size());
        fn_.code.push_back(Instr::make_branch(OpCode::Br, 0, 0, 0));

        // Patch the if-false jump to start of else
        fn_.code[f.ifJumpPc].imm.u32 = static_cast<uint32_t>(fn_.code.size());
        f.hasElse = true;

        // Reset operand stack to block entry for the else branch
        typeStack_.resize(f.stackHeight);
        f.polymorphic = false;
    }

    void exit_block(size_t line) {
        if (ctrlStack_.size() <= 1) {
            throw CompileError("unexpected 'end' without matching block at line " + std::to_string(line));
        }
        CtrlFrame f = std::move(ctrlStack_.back());
        ctrlStack_.pop_back();

        validate_frame_exit(f, line);

        // If `if` has no `else`, it cannot produce result values
        if (f.kind == CtrlKind::If && !f.hasElse && !f.resultTypes.empty()) {
            throw CompileError("'if' with result type must have an 'else' branch at line " +
                               std::to_string(line));
        }

        uint32_t end_pc = static_cast<uint32_t>(fn_.code.size());
        if (f.kind == CtrlKind::If) {
            if (f.hasElse) {
                fn_.code[f.elseJumpPc].imm.u32 = end_pc;
            } else {
                fn_.code[f.ifJumpPc].imm.u32 = end_pc;
            }
        }
        patch_frame_ends(f, end_pc);

        // Normalize stack to frame entry + resultTypes
        typeStack_.resize(f.stackHeight);
        for (ValType t : f.resultTypes) {
            typeStack_.push_back(t);
        }
    }

    void emit_br_table(const std::vector<SExpr>& nodes, size_t start, size_t end, size_t line) {
        if (start >= end) {
            throw CompileError("br_table requires at least one default label at line " + std::to_string(line));
        }
        pop_expected(ValType::I32, line);

        uint32_t table_idx = static_cast<uint32_t>(fn_.brTables.size());
        BrTable bt;
        size_t count = end - start;
        bt.targets.resize(count - 1);

        // Validate all targets against the default target's arity
        for (size_t k = 0; k < count; ++k) {
            size_t target_idx = resolve_label(nodes[start + k]);
            auto [arity, drop_count] = check_branch_stack(target_idx, line, false);
            BrTableTarget entry;
            entry.arity = arity;
            entry.dropCount = drop_count;
            if (ctrlStack_[target_idx].kind == CtrlKind::Loop) {
                entry.targetPc = ctrlStack_[target_idx].startPc;
            } else {
                entry.targetPc = 0;
                ctrlStack_[target_idx].brTablePatches.push_back(
                    BrTablePatch{table_idx, (k + 1 == count) ? -1 : static_cast<int32_t>(k)});
            }
            if (k + 1 == count) {
                bt.defaultTarget = entry;
            } else {
                bt.targets[k] = entry;
            }
        }
        fn_.brTables.push_back(std::move(bt));
        fn_.code.push_back(Instr::make_u32(OpCode::BrTable, table_idx));
        set_polymorphic();
    }

    static size_t immediate_count(const std::string& op) {
        if (op == "local.get" || op == "local.set" || op == "local.tee" ||
            op == "global.get" || op == "global.set" ||
            op == "i32.const" || op == "i64.const" ||
            op == "f32.const" || op == "f64.const" ||
            op == "br" || op == "br_if" || op == "call" ||
            op == "mem.const_str") {
            return 1;
        }
        return 0;
    }

    void emit_opcode(const std::string& op, const std::vector<SExpr>& nodes, size_t imm_idx, size_t line) {
        // Control flow
        if (op == "nop") {
            fn_.code.push_back(Instr::make(OpCode::Nop));
            return;
        }
        if (op == "unreachable") {
            fn_.code.push_back(Instr::make(OpCode::Unreachable));
            set_polymorphic();
            return;
        }
        if (op == "br") {
            size_t target_idx = resolve_label(nodes[imm_idx]);
            auto [arity, drop_count] = check_branch_stack(target_idx, line, true);
            uint32_t pc = static_cast<uint32_t>(fn_.code.size());
            uint32_t target_pc = (ctrlStack_[target_idx].kind == CtrlKind::Loop)
                                     ? ctrlStack_[target_idx].startPc
                                     : 0;
            fn_.code.push_back(Instr::make_branch(OpCode::Br, target_pc, arity, drop_count));
            if (ctrlStack_[target_idx].kind != CtrlKind::Loop) {
                ctrlStack_[target_idx].endPatches.push_back(pc);
            }
            set_polymorphic();
            return;
        }
        if (op == "br_if") {
            pop_expected(ValType::I32, line);
            size_t target_idx = resolve_label(nodes[imm_idx]);
            auto [arity, drop_count] = check_branch_stack(target_idx, line, false);
            uint32_t pc = static_cast<uint32_t>(fn_.code.size());
            uint32_t target_pc = (ctrlStack_[target_idx].kind == CtrlKind::Loop)
                                     ? ctrlStack_[target_idx].startPc
                                     : 0;
            fn_.code.push_back(Instr::make_branch(OpCode::BrIf, target_pc, arity, drop_count));
            if (ctrlStack_[target_idx].kind != CtrlKind::Loop) {
                ctrlStack_[target_idx].endPatches.push_back(pc);
            }
            return;
        }
        if (op == "return") {
            for (size_t k = fn_.sig.results.size(); k > 0; --k) {
                pop_expected(fn_.sig.results[k - 1], line);
            }
            fn_.code.push_back(Instr::make(OpCode::Return));
            set_polymorphic();
            return;
        }
        if (op == "call") {
            uint32_t fidx = mctx_.resolve_func(nodes[imm_idx]);
            const FuncSignature& sig = mctx_.mod.funcs[fidx].sig;
            for (size_t k = sig.params.size(); k > 0; --k) {
                pop_expected(sig.params[k - 1], line);
            }
            for (ValType r : sig.results) {
                push_type(r);
            }
            fn_.code.push_back(Instr::make_u32(OpCode::Call, fidx));
            return;
        }

        // Parametric
        if (op == "drop") {
            pop_any(line);
            fn_.code.push_back(Instr::make(OpCode::Drop));
            return;
        }
        if (op == "select") {
            pop_expected(ValType::I32, line);
            ValType t2 = pop_any(line);
            ValType t1 = pop_any(line);
            if (t1 == kUnknownType) {
                push_type(t2);
            } else if (t2 == kUnknownType) {
                push_type(t1);
            } else if (t1 == t2) {
                push_type(t1);
            } else if (is_handle_type(t1) && is_handle_type(t2)) {
                push_type(ValType::Handle);
            } else {
                throw CompileError("select operand type mismatch at line " + std::to_string(line));
            }
            fn_.code.push_back(Instr::make(OpCode::Select));
            return;
        }

        // Locals & Globals
        if (op == "local.get") {
            uint32_t idx = resolve_local(nodes[imm_idx]);
            push_type(fn_.locals[idx]);
            fn_.code.push_back(Instr::make_u32(OpCode::LocalGet, idx));
            return;
        }
        if (op == "local.set") {
            uint32_t idx = resolve_local(nodes[imm_idx]);
            pop_expected(fn_.locals[idx], line);
            fn_.code.push_back(Instr::make_u32(OpCode::LocalSet, idx));
            return;
        }
        if (op == "local.tee") {
            uint32_t idx = resolve_local(nodes[imm_idx]);
            pop_expected(fn_.locals[idx], line);
            push_type(fn_.locals[idx]);
            fn_.code.push_back(Instr::make_u32(OpCode::LocalTee, idx));
            return;
        }
        if (op == "global.get") {
            uint32_t idx = mctx_.resolve_global(nodes[imm_idx]);
            push_type(mctx_.mod.globals[idx].type);
            fn_.code.push_back(Instr::make_u32(OpCode::GlobalGet, idx));
            return;
        }
        if (op == "global.set") {
            uint32_t idx = mctx_.resolve_global(nodes[imm_idx]);
            if (!mctx_.mod.globals[idx].isMut) {
                throw CompileError("cannot write to immutable global at line " + std::to_string(line));
            }
            pop_expected(mctx_.mod.globals[idx].type, line);
            fn_.code.push_back(Instr::make_u32(OpCode::GlobalSet, idx));
            return;
        }

        // Constants
        if (op == "i32.const") {
            int32_t v = parse_i32_literal(nodes[imm_idx].text, nodes[imm_idx].line);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make_i32(OpCode::I32Const, v));
            return;
        }
        if (op == "i64.const") {
            int64_t v = parse_i64_literal(nodes[imm_idx].text, nodes[imm_idx].line);
            push_type(ValType::I64);
            fn_.code.push_back(Instr::make_i64(OpCode::I64Const, v));
            return;
        }
        if (op == "f32.const") {
            float v = parse_f32_literal(nodes[imm_idx].text, nodes[imm_idx].line);
            push_type(ValType::F32);
            fn_.code.push_back(Instr::make_f32(OpCode::F32Const, v));
            return;
        }
        if (op == "f64.const") {
            double v = parse_f64_literal(nodes[imm_idx].text, nodes[imm_idx].line);
            push_type(ValType::F64);
            fn_.code.push_back(Instr::make_f64(OpCode::F64Const, v));
            return;
        }
        if (op == "handle.null") {
            push_type(ValType::Handle);
            fn_.code.push_back(Instr::make(OpCode::HandleNull));
            return;
        }
        if (op == "mem.const_str") {
            uint32_t sidx = mctx_.resolve_or_add_string(nodes[imm_idx]);
            push_type(ValType::RoHandle);
            fn_.code.push_back(Instr::make_u32(OpCode::MemConstStr, sidx));
            return;
        }

        // Native Split-Block Memory Opcodes (Phase 2)
        if (op == "mem.alloc") {
            pop_expected(ValType::I32, line); // num_refs
            pop_expected(ValType::I32, line); // num_bytes
            push_type(ValType::RwHandle);
            fn_.code.push_back(Instr::make(OpCode::MemAlloc));
            return;
        }
        if (op == "mem.span") {
            pop_expected(ValType::I32, line); // byte_len
            pop_expected(ValType::I32, line); // byte_off
            ValType ht = pop_handle(line, false);
            push_type(ht);
            fn_.code.push_back(Instr::make(OpCode::MemSpan));
            return;
        }
        if (op == "mem.ref_span") {
            pop_expected(ValType::I32, line); // ref_len
            pop_expected(ValType::I32, line); // ref_off
            ValType ht = pop_handle(line, false);
            push_type(ht);
            fn_.code.push_back(Instr::make(OpCode::MemRefSpan));
            return;
        }
        if (op == "mem.readonly") {
            pop_handle(line, false);
            push_type(ValType::RoHandle);
            fn_.code.push_back(Instr::make(OpCode::MemReadOnly));
            return;
        }
        if (op == "mem.byte_len" || op == "mem.len") {
            pop_handle(line, false);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemByteLen));
            return;
        }
        if (op == "mem.ref_len") {
            pop_handle(line, false);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemRefLen));
            return;
        }
        if (op == "mem.is_readonly") {
            pop_handle(line, false);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemIsReadOnly));
            return;
        }
        if (op == "mem.is_null") {
            pop_handle(line, false);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemIsNull));
            return;
        }
        if (op == "mem.eq") {
            pop_handle(line, false);
            pop_handle(line, false);
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemEq));
            return;
        }

        // Typed byte reads: (h: handle, off: i32) -> T
        auto emit_mem_read = [&](OpCode code, ValType res) {
            pop_expected(ValType::I32, line);
            pop_handle(line, false);
            push_type(res);
            fn_.code.push_back(Instr::make(code));
        };
        if (op == "mem.read_u8")  { emit_mem_read(OpCode::MemReadU8,  ValType::I32); return; }
        if (op == "mem.read_i8")  { emit_mem_read(OpCode::MemReadI8,  ValType::I32); return; }
        if (op == "mem.read_u16") { emit_mem_read(OpCode::MemReadU16, ValType::I32); return; }
        if (op == "mem.read_i16") { emit_mem_read(OpCode::MemReadI16, ValType::I32); return; }
        if (op == "mem.read_i32") { emit_mem_read(OpCode::MemReadI32, ValType::I32); return; }
        if (op == "mem.read_i64") { emit_mem_read(OpCode::MemReadI64, ValType::I64); return; }
        if (op == "mem.read_f32") { emit_mem_read(OpCode::MemReadF32, ValType::F32); return; }
        if (op == "mem.read_f64") { emit_mem_read(OpCode::MemReadF64, ValType::F64); return; }

        // Typed byte writes: (h: handle, off: i32, val: T) -> ()
        auto emit_mem_write = [&](OpCode code, ValType val_t) {
            pop_expected(val_t, line);
            pop_expected(ValType::I32, line);
            pop_handle(line, true); // statically rejects ro_handle!
            fn_.code.push_back(Instr::make(code));
        };
        if (op == "mem.write_u8" || op == "mem.write_i8")   { emit_mem_write(OpCode::MemWriteU8,  ValType::I32); return; }
        if (op == "mem.write_u16" || op == "mem.write_i16") { emit_mem_write(OpCode::MemWriteU16, ValType::I32); return; }
        if (op == "mem.write_i32") { emit_mem_write(OpCode::MemWriteI32, ValType::I32); return; }
        if (op == "mem.write_i64") { emit_mem_write(OpCode::MemWriteI64, ValType::I64); return; }
        if (op == "mem.write_f32") { emit_mem_write(OpCode::MemWriteF32, ValType::F32); return; }
        if (op == "mem.write_f64") { emit_mem_write(OpCode::MemWriteF64, ValType::F64); return; }

        // Reference slot ops
        if (op == "mem.read_ref") {
            pop_expected(ValType::I32, line);
            pop_handle(line, false);
            push_type(ValType::Handle);
            fn_.code.push_back(Instr::make(OpCode::MemReadRef));
            return;
        }
        if (op == "mem.write_ref") {
            pop_handle(line, false); // child
            pop_expected(ValType::I32, line); // slot_idx
            pop_handle(line, true);  // parent (must be writable)
            fn_.code.push_back(Instr::make(OpCode::MemWriteRef));
            return;
        }

        // Bulk memory ops
        if (op == "mem.copy") {
            pop_expected(ValType::I32, line); // len
            pop_expected(ValType::I32, line); // src_off
            pop_handle(line, false);          // src
            pop_expected(ValType::I32, line); // dst_off
            pop_handle(line, true);           // dst (must be writable)
            fn_.code.push_back(Instr::make(OpCode::MemCopy));
            return;
        }
        if (op == "mem.fill") {
            pop_expected(ValType::I32, line); // len
            pop_expected(ValType::I32, line); // val
            pop_expected(ValType::I32, line); // dst_off
            pop_handle(line, true);           // dst (must be writable)
            fn_.code.push_back(Instr::make(OpCode::MemFill));
            return;
        }
        if (op == "mem.cmp") {
            pop_expected(ValType::I32, line); // len
            pop_expected(ValType::I32, line); // b_off
            pop_handle(line, false);          // b
            pop_expected(ValType::I32, line); // a_off
            pop_handle(line, false);          // a
            push_type(ValType::I32);
            fn_.code.push_back(Instr::make(OpCode::MemCmp));
            return;
        }

        // Numeric & conversion ops lookup table
        struct OpSig {
            OpCode  code;
            uint8_t arity; // 1 or 2
            ValType inType;
            ValType outType;
        };
        static const std::unordered_map<std::string, OpSig> kNumericOps = {
            // i32
            {"i32.eqz",    {OpCode::I32Eqz,    1, ValType::I32, ValType::I32}},
            {"i32.eq",     {OpCode::I32Eq,     2, ValType::I32, ValType::I32}},
            {"i32.ne",     {OpCode::I32Ne,     2, ValType::I32, ValType::I32}},
            {"i32.lt_s",   {OpCode::I32LtS,    2, ValType::I32, ValType::I32}},
            {"i32.lt_u",   {OpCode::I32LtU,    2, ValType::I32, ValType::I32}},
            {"i32.gt_s",   {OpCode::I32GtS,    2, ValType::I32, ValType::I32}},
            {"i32.gt_u",   {OpCode::I32GtU,    2, ValType::I32, ValType::I32}},
            {"i32.le_s",   {OpCode::I32LeS,    2, ValType::I32, ValType::I32}},
            {"i32.le_u",   {OpCode::I32LeU,    2, ValType::I32, ValType::I32}},
            {"i32.ge_s",   {OpCode::I32GeS,    2, ValType::I32, ValType::I32}},
            {"i32.ge_u",   {OpCode::I32GeU,    2, ValType::I32, ValType::I32}},
            {"i32.clz",    {OpCode::I32Clz,    1, ValType::I32, ValType::I32}},
            {"i32.ctz",    {OpCode::I32Ctz,    1, ValType::I32, ValType::I32}},
            {"i32.popcnt", {OpCode::I32Popcnt, 1, ValType::I32, ValType::I32}},
            {"i32.add",    {OpCode::I32Add,    2, ValType::I32, ValType::I32}},
            {"i32.sub",    {OpCode::I32Sub,    2, ValType::I32, ValType::I32}},
            {"i32.mul",    {OpCode::I32Mul,    2, ValType::I32, ValType::I32}},
            {"i32.div_s",  {OpCode::I32DivS,   2, ValType::I32, ValType::I32}},
            {"i32.div_u",  {OpCode::I32DivU,   2, ValType::I32, ValType::I32}},
            {"i32.rem_s",  {OpCode::I32RemS,   2, ValType::I32, ValType::I32}},
            {"i32.rem_u",  {OpCode::I32RemU,   2, ValType::I32, ValType::I32}},
            {"i32.and",    {OpCode::I32And,    2, ValType::I32, ValType::I32}},
            {"i32.or",     {OpCode::I32Or,     2, ValType::I32, ValType::I32}},
            {"i32.xor",    {OpCode::I32Xor,    2, ValType::I32, ValType::I32}},
            {"i32.shl",    {OpCode::I32Shl,    2, ValType::I32, ValType::I32}},
            {"i32.shr_s",  {OpCode::I32ShrS,   2, ValType::I32, ValType::I32}},
            {"i32.shr_u",  {OpCode::I32ShrU,   2, ValType::I32, ValType::I32}},
            {"i32.rotl",   {OpCode::I32Rotl,   2, ValType::I32, ValType::I32}},
            {"i32.rotr",   {OpCode::I32Rotr,   2, ValType::I32, ValType::I32}},

            // i64
            {"i64.eqz",    {OpCode::I64Eqz,    1, ValType::I64, ValType::I32}},
            {"i64.eq",     {OpCode::I64Eq,     2, ValType::I64, ValType::I32}},
            {"i64.ne",     {OpCode::I64Ne,     2, ValType::I64, ValType::I32}},
            {"i64.lt_s",   {OpCode::I64LtS,    2, ValType::I64, ValType::I32}},
            {"i64.lt_u",   {OpCode::I64LtU,    2, ValType::I64, ValType::I32}},
            {"i64.gt_s",   {OpCode::I64GtS,    2, ValType::I64, ValType::I32}},
            {"i64.gt_u",   {OpCode::I64GtU,    2, ValType::I64, ValType::I32}},
            {"i64.le_s",   {OpCode::I64LeS,    2, ValType::I64, ValType::I32}},
            {"i64.le_u",   {OpCode::I64LeU,    2, ValType::I64, ValType::I32}},
            {"i64.ge_s",   {OpCode::I64GeS,    2, ValType::I64, ValType::I32}},
            {"i64.ge_u",   {OpCode::I64GeU,    2, ValType::I64, ValType::I32}},
            {"i64.clz",    {OpCode::I64Clz,    1, ValType::I64, ValType::I64}},
            {"i64.ctz",    {OpCode::I64Ctz,    1, ValType::I64, ValType::I64}},
            {"i64.popcnt", {OpCode::I64Popcnt, 1, ValType::I64, ValType::I64}},
            {"i64.add",    {OpCode::I64Add,    2, ValType::I64, ValType::I64}},
            {"i64.sub",    {OpCode::I64Sub,    2, ValType::I64, ValType::I64}},
            {"i64.mul",    {OpCode::I64Mul,    2, ValType::I64, ValType::I64}},
            {"i64.div_s",  {OpCode::I64DivS,   2, ValType::I64, ValType::I64}},
            {"i64.div_u",  {OpCode::I64DivU,   2, ValType::I64, ValType::I64}},
            {"i64.rem_s",  {OpCode::I64RemS,   2, ValType::I64, ValType::I64}},
            {"i64.rem_u",  {OpCode::I64RemU,   2, ValType::I64, ValType::I64}},
            {"i64.and",    {OpCode::I64And,    2, ValType::I64, ValType::I64}},
            {"i64.or",     {OpCode::I64Or,     2, ValType::I64, ValType::I64}},
            {"i64.xor",    {OpCode::I64Xor,    2, ValType::I64, ValType::I64}},
            {"i64.shl",    {OpCode::I64Shl,    2, ValType::I64, ValType::I64}},
            {"i64.shr_s",  {OpCode::I64ShrS,   2, ValType::I64, ValType::I64}},
            {"i64.shr_u",  {OpCode::I64ShrU,   2, ValType::I64, ValType::I64}},
            {"i64.rotl",   {OpCode::I64Rotl,   2, ValType::I64, ValType::I64}},
            {"i64.rotr",   {OpCode::I64Rotr,   2, ValType::I64, ValType::I64}},

            // f32
            {"f32.eq",       {OpCode::F32Eq,       2, ValType::F32, ValType::I32}},
            {"f32.ne",       {OpCode::F32Ne,       2, ValType::F32, ValType::I32}},
            {"f32.lt",       {OpCode::F32Lt,       2, ValType::F32, ValType::I32}},
            {"f32.gt",       {OpCode::F32Gt,       2, ValType::F32, ValType::I32}},
            {"f32.le",       {OpCode::F32Le,       2, ValType::F32, ValType::I32}},
            {"f32.ge",       {OpCode::F32Ge,       2, ValType::F32, ValType::I32}},
            {"f32.abs",      {OpCode::F32Abs,      1, ValType::F32, ValType::F32}},
            {"f32.neg",      {OpCode::F32Neg,      1, ValType::F32, ValType::F32}},
            {"f32.ceil",     {OpCode::F32Ceil,     1, ValType::F32, ValType::F32}},
            {"f32.floor",    {OpCode::F32Floor,    1, ValType::F32, ValType::F32}},
            {"f32.trunc",    {OpCode::F32Trunc,    1, ValType::F32, ValType::F32}},
            {"f32.nearest",  {OpCode::F32Nearest,  1, ValType::F32, ValType::F32}},
            {"f32.sqrt",     {OpCode::F32Sqrt,     1, ValType::F32, ValType::F32}},
            {"f32.add",      {OpCode::F32Add,      2, ValType::F32, ValType::F32}},
            {"f32.sub",      {OpCode::F32Sub,      2, ValType::F32, ValType::F32}},
            {"f32.mul",      {OpCode::F32Mul,      2, ValType::F32, ValType::F32}},
            {"f32.div",      {OpCode::F32Div,      2, ValType::F32, ValType::F32}},
            {"f32.min",      {OpCode::F32Min,      2, ValType::F32, ValType::F32}},
            {"f32.max",      {OpCode::F32Max,      2, ValType::F32, ValType::F32}},
            {"f32.copysign", {OpCode::F32Copysign, 2, ValType::F32, ValType::F32}},

            // f64
            {"f64.eq",       {OpCode::F64Eq,       2, ValType::F64, ValType::I32}},
            {"f64.ne",       {OpCode::F64Ne,       2, ValType::F64, ValType::I32}},
            {"f64.lt",       {OpCode::F64Lt,       2, ValType::F64, ValType::I32}},
            {"f64.gt",       {OpCode::F64Gt,       2, ValType::F64, ValType::I32}},
            {"f64.le",       {OpCode::F64Le,       2, ValType::F64, ValType::I32}},
            {"f64.ge",       {OpCode::F64Ge,       2, ValType::F64, ValType::I32}},
            {"f64.abs",      {OpCode::F64Abs,      1, ValType::F64, ValType::F64}},
            {"f64.neg",      {OpCode::F64Neg,      1, ValType::F64, ValType::F64}},
            {"f64.ceil",     {OpCode::F64Ceil,     1, ValType::F64, ValType::F64}},
            {"f64.floor",    {OpCode::F64Floor,    1, ValType::F64, ValType::F64}},
            {"f64.trunc",    {OpCode::F64Trunc,    1, ValType::F64, ValType::F64}},
            {"f64.nearest",  {OpCode::F64Nearest,  1, ValType::F64, ValType::F64}},
            {"f64.sqrt",     {OpCode::F64Sqrt,     1, ValType::F64, ValType::F64}},
            {"f64.add",      {OpCode::F64Add,      2, ValType::F64, ValType::F64}},
            {"f64.sub",      {OpCode::F64Sub,      2, ValType::F64, ValType::F64}},
            {"f64.mul",      {OpCode::F64Mul,      2, ValType::F64, ValType::F64}},
            {"f64.div",      {OpCode::F64Div,      2, ValType::F64, ValType::F64}},
            {"f64.min",      {OpCode::F64Min,      2, ValType::F64, ValType::F64}},
            {"f64.max",      {OpCode::F64Max,      2, ValType::F64, ValType::F64}},
            {"f64.copysign", {OpCode::F64Copysign, 2, ValType::F64, ValType::F64}},

            // Conversions
            {"i32.wrap_i64",        {OpCode::I32WrapI64,        1, ValType::I64, ValType::I32}},
            {"i64.extend_i32_s",    {OpCode::I64ExtendI32S,     1, ValType::I32, ValType::I64}},
            {"i64.extend_i32_u",    {OpCode::I64ExtendI32U,     1, ValType::I32, ValType::I64}},
            {"i32.trunc_f32_s",     {OpCode::I32TruncF32S,      1, ValType::F32, ValType::I32}},
            {"i32.trunc_f32_u",     {OpCode::I32TruncF32U,      1, ValType::F32, ValType::I32}},
            {"i32.trunc_f64_s",     {OpCode::I32TruncF64S,      1, ValType::F64, ValType::I32}},
            {"i32.trunc_f64_u",     {OpCode::I32TruncF64U,      1, ValType::F64, ValType::I32}},
            {"i64.trunc_f32_s",     {OpCode::I64TruncF32S,      1, ValType::F32, ValType::I64}},
            {"i64.trunc_f32_u",     {OpCode::I64TruncF32U,      1, ValType::F32, ValType::I64}},
            {"i64.trunc_f64_s",     {OpCode::I64TruncF64S,      1, ValType::F64, ValType::I64}},
            {"i64.trunc_f64_u",     {OpCode::I64TruncF64U,      1, ValType::F64, ValType::I64}},
            {"f32.convert_i32_s",   {OpCode::F32ConvertI32S,    1, ValType::I32, ValType::F32}},
            {"f32.convert_i32_u",   {OpCode::F32ConvertI32U,    1, ValType::I32, ValType::F32}},
            {"f32.convert_i64_s",   {OpCode::F32ConvertI64S,    1, ValType::I64, ValType::F32}},
            {"f32.convert_i64_u",   {OpCode::F32ConvertI64U,    1, ValType::I64, ValType::F32}},
            {"f64.convert_i32_s",   {OpCode::F64ConvertI32S,    1, ValType::I32, ValType::F64}},
            {"f64.convert_i32_u",   {OpCode::F64ConvertI32U,    1, ValType::I32, ValType::F64}},
            {"f64.convert_i64_s",   {OpCode::F64ConvertI64S,    1, ValType::I64, ValType::F64}},
            {"f64.convert_i64_u",   {OpCode::F64ConvertI64U,    1, ValType::I64, ValType::F64}},
            {"f32.demote_f64",      {OpCode::F32DemoteF64,      1, ValType::F64, ValType::F32}},
            {"f64.promote_f32",     {OpCode::F64PromoteF32,     1, ValType::F32, ValType::F64}},
            {"i32.reinterpret_f32", {OpCode::I32ReinterpretF32, 1, ValType::F32, ValType::I32}},
            {"i64.reinterpret_f64", {OpCode::I64ReinterpretF64, 1, ValType::F64, ValType::I64}},
            {"f32.reinterpret_i32", {OpCode::F32ReinterpretI32, 1, ValType::I32, ValType::F32}},
            {"f64.reinterpret_i64", {OpCode::F64ReinterpretI64, 1, ValType::I64, ValType::F64}},
            {"i32.extend8_s",       {OpCode::I32Extend8S,       1, ValType::I32, ValType::I32}},
            {"i32.extend16_s",      {OpCode::I32Extend16S,      1, ValType::I32, ValType::I32}},
            {"i64.extend8_s",       {OpCode::I64Extend8S,       1, ValType::I64, ValType::I64}},
            {"i64.extend16_s",      {OpCode::I64Extend16S,      1, ValType::I64, ValType::I64}},
            {"i64.extend32_s",      {OpCode::I64Extend32S,      1, ValType::I64, ValType::I64}},
        };

        auto it = kNumericOps.find(op);
        if (it != kNumericOps.end()) {
            const OpSig& sig = it->second;
            for (uint8_t k = 0; k < sig.arity; ++k) {
                pop_expected(sig.inType, line);
            }
            push_type(sig.outType);
            fn_.code.push_back(Instr::make(sig.code));
            return;
        }

        // Fail-fast validation on unknown opcodes!
        throw CompileError("unknown opcode '" + op + "' at line " + std::to_string(line));
    }

    ModuleContext&                            mctx_;
    CompiledFunc&                             fn_;
    std::unordered_map<std::string, uint32_t> localNames_;
    std::vector<ValType>                      typeStack_;
    std::vector<CtrlFrame>                    ctrlStack_;
    const std::vector<ValType>                emptyTypes_;
};

struct PendingFunc {
    uint32_t                                  funcIdx = 0;
    const SExpr*                              expr    = nullptr;
    size_t                                    bodyStart = 0;
    std::unordered_map<std::string, uint32_t> localNames;
};

} // namespace

CompiledModule compile_module(const SExpr& root) {
    if (!root.is_list_with_head("module")) {
        throw CompileError("expected top-level (module ...) at line " + std::to_string(root.line));
    }

    ModuleContext mctx;
    std::vector<PendingFunc> pendingFuncs;
    std::vector<const SExpr*> pendingExports;

    // Pass 1: Collect types, strings, globals, function signatures, and exports.
    for (size_t i = 1; i < root.children.size(); ++i) {
        const SExpr& item = root.children[i];
        if (!item.is_list() || item.children.empty() || !item.children[0].is_atom()) {
            throw CompileError("invalid module field at line " + std::to_string(item.line));
        }
        const std::string& head = item.children[0].text;

        if (head == "type") {
            size_t idx = 1;
            std::string name;
            if (idx < item.children.size() && is_ident(item.children[idx])) {
                name = item.children[idx++].text;
            }
            if (idx >= item.children.size() || !item.children[idx].is_list_with_head("func")) {
                throw CompileError("expected (func ...) in (type ...) at line " + std::to_string(item.line));
            }
            FuncSignature sig;
            const SExpr& fspec = item.children[idx];
            for (size_t k = 1; k < fspec.children.size(); ++k) {
                const SExpr& sub = fspec.children[k];
                if (sub.is_list_with_head("param")) {
                    size_t p = 1;
                    if (p < sub.children.size() && is_ident(sub.children[p])) ++p;
                    for (; p < sub.children.size(); ++p) {
                        sig.params.push_back(expect_val_type(sub.children[p]));
                    }
                } else if (sub.is_list_with_head("result")) {
                    for (size_t r = 1; r < sub.children.size(); ++r) {
                        sig.results.push_back(expect_val_type(sub.children[r]));
                    }
                }
            }
            uint32_t tidx = static_cast<uint32_t>(mctx.mod.types.size());
            mctx.mod.types.push_back(std::move(sig));
            if (!name.empty()) mctx.typeNames[name] = tidx;
        } else if (head == "string" || head == "const_str") {
            size_t idx = 1;
            std::string name;
            if (idx < item.children.size() && is_ident(item.children[idx])) {
                name = item.children[idx++].text;
            }
            if (idx >= item.children.size() || !item.children[idx].is_string()) {
                throw CompileError("expected string literal in (string ...) at line " + std::to_string(item.line));
            }
            uint32_t sidx = static_cast<uint32_t>(mctx.mod.strings.size());
            mctx.mod.strings.push_back(item.children[idx].text);
            if (!name.empty()) mctx.stringNames[name] = sidx;
        } else if (head == "global") {
            size_t idx = 1;
            CompiledGlobal g;
            if (idx < item.children.size() && is_ident(item.children[idx])) {
                g.name = item.children[idx++].text;
            }
            if (idx >= item.children.size()) {
                throw CompileError("incomplete (global ...) at line " + std::to_string(item.line));
            }
            if (item.children[idx].is_list_with_head("mut")) {
                if (item.children[idx].children.size() != 2) {
                    throw CompileError("invalid (mut ...) in global at line " + std::to_string(item.line));
                }
                g.isMut = true;
                g.type = expect_val_type(item.children[idx].children[1]);
                ++idx;
            } else {
                g.isMut = false;
                g.type = expect_val_type(item.children[idx]);
                ++idx;
            }
            g.initVal = Value::zero(g.type);
            if (idx < item.children.size()) {
                const SExpr& init = item.children[idx];
                if (!init.is_list() || init.children.empty() || !init.children[0].is_atom()) {
                    throw CompileError("invalid global initializer at line " + std::to_string(init.line));
                }
                const std::string& iop = init.children[0].text;
                if (iop == "i32.const" && init.children.size() == 2) {
                    if (g.type != ValType::I32) throw CompileError("global init type mismatch");
                    g.initVal = Value::from_i32(parse_i32_literal(init.children[1].text, init.line));
                } else if (iop == "i64.const" && init.children.size() == 2) {
                    if (g.type != ValType::I64) throw CompileError("global init type mismatch");
                    g.initVal = Value::from_i64(parse_i64_literal(init.children[1].text, init.line));
                } else if (iop == "f32.const" && init.children.size() == 2) {
                    if (g.type != ValType::F32) throw CompileError("global init type mismatch");
                    g.initVal = Value::from_f32(parse_f32_literal(init.children[1].text, init.line));
                } else if (iop == "f64.const" && init.children.size() == 2) {
                    if (g.type != ValType::F64) throw CompileError("global init type mismatch");
                    g.initVal = Value::from_f64(parse_f64_literal(init.children[1].text, init.line));
                } else if (iop == "handle.null") {
                    if (!is_handle_type(g.type)) throw CompileError("global init type mismatch");
                    g.initVal = Value::from_handle(FatHandle::null());
                } else if (iop == "mem.const_str" && init.children.size() == 2) {
                    if (g.type != ValType::RoHandle && g.type != ValType::Handle) {
                        throw CompileError("global mem.const_str requires ro_handle or handle");
                    }
                    g.initStrIdx = static_cast<int32_t>(mctx.resolve_or_add_string(init.children[1]));
                } else {
                    throw CompileError("unsupported global initializer '" + iop + "' at line " +
                                       std::to_string(init.line));
                }
            }
            uint32_t gidx = static_cast<uint32_t>(mctx.mod.globals.size());
            if (!g.name.empty()) mctx.globalNames[g.name] = gidx;
            mctx.mod.globals.push_back(std::move(g));
        } else if (head == "func") {
            uint32_t fidx = static_cast<uint32_t>(mctx.mod.funcs.size());
            CompiledFunc fn;
            std::unordered_map<std::string, uint32_t> localNames;
            size_t idx = 1;

            if (idx < item.children.size() && is_ident(item.children[idx])) {
                fn.name = item.children[idx++].text;
                mctx.funcNames[fn.name] = fidx;
            }

            // Inline (export "name")
            while (idx < item.children.size() && item.children[idx].is_list_with_head("export")) {
                if (item.children[idx].children.size() != 2 || !item.children[idx].children[1].is_string()) {
                    throw CompileError("invalid inline export at line " + std::to_string(item.children[idx].line));
                }
                mctx.mod.exports[item.children[idx].children[1].text] = fidx;
                ++idx;
            }

            // Optional (type $t)
            if (idx < item.children.size() && item.children[idx].is_list_with_head("type")) {
                if (item.children[idx].children.size() != 2) {
                    throw CompileError("invalid (type ...) reference at line " + std::to_string(item.children[idx].line));
                }
                uint32_t tidx = mctx.resolve_type(item.children[idx].children[1]);
                fn.sig = mctx.mod.types[tidx];
                fn.locals = fn.sig.params;
                ++idx;
            }

            // Params
            while (idx < item.children.size() && item.children[idx].is_list_with_head("param")) {
                const SExpr& pnode = item.children[idx];
                if (pnode.children.size() >= 3 && is_ident(pnode.children[1])) {
                    if (pnode.children.size() != 3) {
                        throw CompileError("named param must have exactly one type at line " + std::to_string(pnode.line));
                    }
                    ValType pt = expect_val_type(pnode.children[2]);
                    uint32_t lidx = static_cast<uint32_t>(fn.locals.size());
                    localNames[pnode.children[1].text] = lidx;
                    fn.sig.params.push_back(pt);
                    fn.locals.push_back(pt);
                } else {
                    for (size_t k = 1; k < pnode.children.size(); ++k) {
                        ValType pt = expect_val_type(pnode.children[k]);
                        fn.sig.params.push_back(pt);
                        fn.locals.push_back(pt);
                    }
                }
                ++idx;
            }
            fn.paramCount = static_cast<uint32_t>(fn.sig.params.size());

            // Results
            while (idx < item.children.size() && item.children[idx].is_list_with_head("result")) {
                const SExpr& rnode = item.children[idx];
                for (size_t k = 1; k < rnode.children.size(); ++k) {
                    fn.sig.results.push_back(expect_val_type(rnode.children[k]));
                }
                ++idx;
            }

            // Locals
            while (idx < item.children.size() && item.children[idx].is_list_with_head("local")) {
                const SExpr& lnode = item.children[idx];
                if (lnode.children.size() >= 3 && is_ident(lnode.children[1])) {
                    if (lnode.children.size() != 3) {
                        throw CompileError("named local must have exactly one type at line " + std::to_string(lnode.line));
                    }
                    ValType lt = expect_val_type(lnode.children[2]);
                    uint32_t lidx = static_cast<uint32_t>(fn.locals.size());
                    localNames[lnode.children[1].text] = lidx;
                    fn.locals.push_back(lt);
                } else {
                    for (size_t k = 1; k < lnode.children.size(); ++k) {
                        fn.locals.push_back(expect_val_type(lnode.children[k]));
                    }
                }
                ++idx;
            }

            mctx.mod.funcs.push_back(std::move(fn));
            pendingFuncs.push_back(PendingFunc{fidx, &item, idx, std::move(localNames)});
        } else if (head == "export") {
            pendingExports.push_back(&item);
        } else {
            throw CompileError("unknown module section '(" + head + " ...)' at line " +
                               std::to_string(item.line));
        }
    }

    // Resolve standalone exports
    for (const SExpr* exp : pendingExports) {
        if (exp->children.size() != 3 || !exp->children[1].is_string() ||
            !exp->children[2].is_list_with_head("func") || exp->children[2].children.size() != 2) {
            throw CompileError("invalid (export ...) declaration at line " + std::to_string(exp->line));
        }
        uint32_t fidx = mctx.resolve_func(exp->children[2].children[1]);
        mctx.mod.exports[exp->children[1].text] = fidx;
    }

    // Pass 2: Compile function bodies
    for (auto& pf : pendingFuncs) {
        FuncCompiler fc(mctx, mctx.mod.funcs[pf.funcIdx], std::move(pf.localNames));
        fc.compile_body(pf.expr->children, pf.bodyStart);
    }

    return std::move(mctx.mod);
}

CompiledModule compile_wat(std::string_view source) {
    SExpr root = parse_wat(source);
    return compile_module(root);
}

} // namespace optrich
