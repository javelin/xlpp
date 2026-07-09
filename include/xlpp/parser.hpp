#pragma once

// Recursive-descent parser for the frozen corpus formula grammar (PRD FR2).
//
// Operator precedence (loosest to tightest), matching Excel:
//   comparisons (= <> < <= > >=)  <  &  <  + -  <  * /  <  ^  <
//   unary + -  <  postfix %  <  primary
// Note Excel's quirk: unary minus binds tighter than ^, so -2^2 = 4.

#include <xlpp/ast.hpp>

#include <stdexcept>
#include <string>

namespace xlpp {

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string &message, std::size_t offset)
        : std::runtime_error(message), offset_(offset) {
    }

    std::size_t offset() const {
        return offset_;
    }

private:
    std::size_t offset_;
};

// Parses a formula body (leading '=' stripped if present) into an Ast.
// Throws ParseError or LexError on any input outside the frozen grammar.
Ast parse_formula(const std::string &formula);

// Parses a bare A1 reference like "B5" or "$AC$12". Returns false if the text
// is not a valid single-cell reference.
bool parse_a1_reference(const std::string &text, CellAddress &address);

} // namespace xlpp
