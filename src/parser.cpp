#include "optrich/parser.hpp"

#include <cctype>
#include <sstream>

namespace optrich {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

class Parser {
public:
    explicit Parser(std::string_view src) : src_(src) {}

    SExpr parse_top() {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            throw ParseError("empty WAT input");
        }
        SExpr node = parse_expr();
        skip_ws_and_comments();
        if (pos_ < src_.size()) {
            throw ParseError("unexpected trailing tokens at line " + std::to_string(line_));
        }
        return node;
    }

private:
    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (c == '\n') ++line_;
                ++pos_;
                continue;
            }
            // Line comment: ;; ...
            if (c == ';' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ';') {
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            // Block comment: (; ... ;) (nestable)
            if (c == '(' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ';') {
                pos_ += 2;
                int depth = 1;
                while (pos_ < src_.size() && depth > 0) {
                    if (src_[pos_] == '\n') {
                        ++line_;
                        ++pos_;
                    } else if (src_[pos_] == '(' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ';') {
                        ++depth;
                        pos_ += 2;
                    } else if (src_[pos_] == ';' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ')') {
                        --depth;
                        pos_ += 2;
                    } else {
                        ++pos_;
                    }
                }
                if (depth != 0) {
                    throw ParseError("unterminated block comment at line " + std::to_string(line_));
                }
                continue;
            }
            break;
        }
    }

    SExpr parse_expr() {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            throw ParseError("unexpected end of input at line " + std::to_string(line_));
        }
        char c = src_[pos_];
        if (c == '(') return parse_list();
        if (c == '"') return parse_string();
        if (c == ')') {
            throw ParseError("unexpected ')' at line " + std::to_string(line_));
        }
        return parse_atom();
    }

    SExpr parse_list() {
        SExpr node;
        node.kind = SExpr::Kind::List;
        node.line = line_;
        ++pos_; // consume '('

        while (true) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) {
                throw ParseError("unterminated '(' opened at line " + std::to_string(node.line));
            }
            if (src_[pos_] == ')') {
                ++pos_;
                return node;
            }
            node.children.push_back(parse_expr());
        }
    }

    SExpr parse_string() {
        SExpr node;
        node.kind = SExpr::Kind::String;
        node.line = line_;
        ++pos_; // consume opening '"'

        std::string out;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') {
                node.text = std::move(out);
                return node;
            }
            if (c == '\n') {
                throw ParseError("unterminated string literal at line " + std::to_string(node.line));
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    throw ParseError("unterminated escape in string at line " + std::to_string(line_));
                }
                char e = src_[pos_++];
                switch (e) {
                    case 't':  out.push_back('\t'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case '"':  out.push_back('"');  break;
                    case '\'': out.push_back('\''); break;
                    case '\\': out.push_back('\\'); break;
                    case '0':  out.push_back('\0'); break;
                    default: {
                        int hi = hex_digit(e);
                        if (hi >= 0 && pos_ < src_.size()) {
                            int lo = hex_digit(src_[pos_]);
                            if (lo >= 0) {
                                ++pos_;
                                out.push_back(static_cast<char>((hi << 4) | lo));
                                break;
                            }
                        }
                        throw ParseError("invalid string escape '\\" + std::string(1, e) +
                                         "' at line " + std::to_string(line_));
                    }
                }
            } else {
                out.push_back(c);
            }
        }
        throw ParseError("unterminated string literal at line " + std::to_string(node.line));
    }

    SExpr parse_atom() {
        SExpr node;
        node.kind = SExpr::Kind::Atom;
        node.line = line_;
        size_t start = pos_;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                c == '(' || c == ')' || c == '"' || c == ';') {
                break;
            }
            ++pos_;
        }
        node.text = std::string(src_.substr(start, pos_ - start));
        return node;
    }

    std::string_view src_;
    size_t           pos_  = 0;
    size_t           line_ = 1;
};

} // namespace

SExpr parse_wat(std::string_view source) {
    Parser p(source);
    return p.parse_top();
}

} // namespace optrich
