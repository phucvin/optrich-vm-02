#pragma once

#include "optrich/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace optrich {

// Generic S-expression node produced by the WAT parser.
struct SExpr {
    enum class Kind {
        Atom,
        String,
        List,
    };

    Kind               kind = Kind::Atom;
    std::string        text; // for Atom or decoded String
    std::vector<SExpr> children;
    size_t             line = 1;

    bool is_atom() const { return kind == Kind::Atom; }
    bool is_string() const { return kind == Kind::String; }
    bool is_list() const { return kind == Kind::List; }
    bool is_list_with_head(std::string_view head) const {
        return kind == Kind::List && !children.empty() &&
               children[0].kind == Kind::Atom && children[0].text == head;
    }
};

// Parse a WAT source string into a top-level S-expression (typically `(module ...)`).
SExpr parse_wat(std::string_view source);

} // namespace optrich
