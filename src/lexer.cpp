#include <xlpp/lexer.hpp>

#include <array>
#include <cstdlib>

namespace xlpp {
namespace {

bool is_identifier_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9') || c == '.' ||
           static_cast<unsigned char>(c) >= 0x80; // UTF-8 continuation in sheet names
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Longest-match error literals (PRD FR8 set).
const std::array<const char *, 7> error_literals = {
    "#DIV/0!", "#VALUE!", "#NAME?", "#NULL!", "#NUM!", "#REF!", "#N/A",
};

} // namespace

std::vector<Token> tokenize(const std::string &formula) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = formula.size();

    auto push = [&](TokenKind kind, std::size_t offset, std::string text = {}, double num = 0.0) {
        Token token;
        token.kind = kind;
        token.text = std::move(text);
        token.number = num;
        token.offset = offset;
        tokens.push_back(std::move(token));
    };

    while (i < n) {
        const char c = formula[i];
        const std::size_t start = i;

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }

        if (c == '"') { // string literal, "" escapes a quote
            std::string value;
            ++i;
            while (true) {
                if (i >= n) {
                    throw LexError("unterminated string literal", start);
                }
                if (formula[i] == '"') {
                    if (i + 1 < n && formula[i + 1] == '"') {
                        value += '"';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                value += formula[i++];
            }
            push(TokenKind::string, start, std::move(value));
            continue;
        }

        if (c == '\'') { // quoted sheet name, '' escapes a quote
            std::string value;
            ++i;
            while (true) {
                if (i >= n) {
                    throw LexError("unterminated quoted name", start);
                }
                if (formula[i] == '\'') {
                    if (i + 1 < n && formula[i + 1] == '\'') {
                        value += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                value += formula[i++];
            }
            push(TokenKind::quoted_name, start, std::move(value));
            continue;
        }

        if (c == '#') {
            bool matched = false;
            for (const char *literal : error_literals) {
                const std::size_t len = std::char_traits<char>::length(literal);
                if (formula.compare(i, len, literal) == 0) {
                    push(TokenKind::error_literal, start, literal);
                    i += len;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                throw LexError("unknown error literal", start);
            }
            continue;
        }

        if (is_digit(c) || (c == '.' && i + 1 < n && is_digit(formula[i + 1]))) {
            std::size_t j = i;
            while (j < n && is_digit(formula[j])) {
                ++j;
            }
            if (j < n && formula[j] == '.') {
                ++j;
                while (j < n && is_digit(formula[j])) {
                    ++j;
                }
            }
            if (j < n && (formula[j] == 'e' || formula[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < n && (formula[k] == '+' || formula[k] == '-')) {
                    ++k;
                }
                if (k < n && is_digit(formula[k])) {
                    j = k;
                    while (j < n && is_digit(formula[j])) {
                        ++j;
                    }
                }
            }
            const std::string text = formula.substr(i, j - i);
            push(TokenKind::number, start, text, std::strtod(text.c_str(), nullptr));
            i = j;
            continue;
        }

        if (is_identifier_start(c)) {
            std::size_t j = i;
            while (j < n && is_identifier_char(formula[j])) {
                ++j;
            }
            push(TokenKind::identifier, start, formula.substr(i, j - i));
            i = j;
            continue;
        }

        switch (c) {
        case '(': push(TokenKind::lparen, start); ++i; continue;
        case ')': push(TokenKind::rparen, start); ++i; continue;
        case ',': push(TokenKind::comma, start); ++i; continue;
        case ':': push(TokenKind::colon, start); ++i; continue;
        case '!': push(TokenKind::bang, start); ++i; continue;
        case '%': push(TokenKind::percent, start); ++i; continue;
        case '^': push(TokenKind::caret, start); ++i; continue;
        case '*': push(TokenKind::star, start); ++i; continue;
        case '/': push(TokenKind::slash, start); ++i; continue;
        case '+': push(TokenKind::plus, start); ++i; continue;
        case '-': push(TokenKind::minus, start); ++i; continue;
        case '&': push(TokenKind::ampersand, start); ++i; continue;
        case '=': push(TokenKind::op_eq, start); ++i; continue;
        case '<':
            if (i + 1 < n && formula[i + 1] == '>') {
                push(TokenKind::op_ne, start);
                i += 2;
            } else if (i + 1 < n && formula[i + 1] == '=') {
                push(TokenKind::op_le, start);
                i += 2;
            } else {
                push(TokenKind::op_lt, start);
                ++i;
            }
            continue;
        case '>':
            if (i + 1 < n && formula[i + 1] == '=') {
                push(TokenKind::op_ge, start);
                i += 2;
            } else {
                push(TokenKind::op_gt, start);
                ++i;
            }
            continue;
        default:
            throw LexError(std::string("unexpected character '") + c + "'", start);
        }
    }

    push(TokenKind::end, n);
    return tokens;
}

} // namespace xlpp
