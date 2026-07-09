#pragma once

// Tokenizer for the frozen corpus formula grammar (PRD FR2).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace xlpp {

enum class TokenKind : std::uint8_t {
    number,       // numeric literal (value in Token::number)
    string,       // string literal (unescaped text)
    identifier,   // name / cell ref / function name / unquoted sheet ([$A-Za-z0-9_.]+)
    quoted_name,  // 'sheet name' (unescaped text)
    error_literal,// #REF! etc. (text)
    lparen, rparen, comma, colon, bang, percent,
    caret, star, slash, plus, minus, ampersand,
    op_eq, op_ne, op_lt, op_le, op_gt, op_ge,
    end,
};

struct Token {
    TokenKind kind = TokenKind::end;
    std::string text;
    double number = 0.0;
    std::size_t offset = 0; // position in the source formula, for diagnostics
};

class LexError : public std::runtime_error {
public:
    LexError(const std::string &message, std::size_t offset)
        : std::runtime_error(message), offset_(offset) {
    }

    std::size_t offset() const {
        return offset_;
    }

private:
    std::size_t offset_;
};

// Tokenizes a formula body (leading '=' must already be stripped).
// Throws LexError on unrecognizable input.
std::vector<Token> tokenize(const std::string &formula);

} // namespace xlpp
