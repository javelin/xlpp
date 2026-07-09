#include <xlpp/parser.hpp>

#include <xlpp/lexer.hpp>

#include <cstdlib>

namespace xlpp {
namespace {

// Classifies an identifier as an A1 cell reference: $?[A-Za-z]{1,3}$?[1-9][0-9]*
// Excel forbids defining names that collide with A1 references, so this is
// unambiguous. Returns false if the identifier is a defined name.
bool parse_cell_identifier(const std::string &text, CellAddress &address) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    CellAddress result;

    if (i < n && text[i] == '$') {
        result.absolute_column = true;
        ++i;
    }
    std::size_t letters = 0;
    std::uint32_t column = 0;
    while (i < n && ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z'))) {
        const char upper = static_cast<char>(text[i] & ~0x20);
        column = column * 26 + static_cast<std::uint32_t>(upper - 'A' + 1);
        ++letters;
        ++i;
    }
    if (letters == 0 || letters > 3) {
        return false;
    }
    if (i < n && text[i] == '$') {
        result.absolute_row = true;
        ++i;
    }
    if (i >= n || text[i] == '0') {
        return false;
    }
    std::uint32_t row = 0;
    while (i < n && text[i] >= '0' && text[i] <= '9') {
        row = row * 10 + static_cast<std::uint32_t>(text[i] - '0');
        ++i;
    }
    if (i != n || row == 0 || row > 1048576 || column > 16384) {
        return false;
    }
    result.row = row;
    result.column = column;
    address = result;
    return true;
}

class Parser {
public:
    explicit Parser(const std::string &formula)
        : tokens_(tokenize(formula)) {
    }

    Ast run() {
        const NodeId root = parse_expression();
        expect(TokenKind::end, "trailing input after expression");
        ast_.set_root(root);
        return std::move(ast_);
    }

private:
    const Token &peek(std::size_t ahead = 0) const {
        const std::size_t index = position_ + ahead;
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }

    const Token &advance() {
        return tokens_[position_ < tokens_.size() - 1 ? position_++ : position_];
    }

    bool accept(TokenKind kind) {
        if (peek().kind == kind) {
            advance();
            return true;
        }
        return false;
    }

    void expect(TokenKind kind, const char *message) {
        if (!accept(kind)) {
            throw ParseError(message, peek().offset);
        }
    }

    [[noreturn]] void fail(const std::string &message) {
        throw ParseError(message, peek().offset);
    }

    // ----- precedence climbing ------------------------------------------------

    NodeId parse_expression() {
        return parse_comparison();
    }

    NodeId parse_comparison() {
        NodeId left = parse_concat();
        while (true) {
            BinaryOp op;
            switch (peek().kind) {
            case TokenKind::op_eq: op = BinaryOp::equal; break;
            case TokenKind::op_ne: op = BinaryOp::not_equal; break;
            case TokenKind::op_lt: op = BinaryOp::less; break;
            case TokenKind::op_le: op = BinaryOp::less_equal; break;
            case TokenKind::op_gt: op = BinaryOp::greater; break;
            case TokenKind::op_ge: op = BinaryOp::greater_equal; break;
            default:
                return left;
            }
            advance();
            left = make_binary(op, left, parse_concat());
        }
    }

    NodeId parse_concat() {
        NodeId left = parse_additive();
        while (accept(TokenKind::ampersand)) {
            left = make_binary(BinaryOp::concat, left, parse_additive());
        }
        return left;
    }

    NodeId parse_additive() {
        NodeId left = parse_multiplicative();
        while (true) {
            if (accept(TokenKind::plus)) {
                left = make_binary(BinaryOp::add, left, parse_multiplicative());
            } else if (accept(TokenKind::minus)) {
                left = make_binary(BinaryOp::subtract, left, parse_multiplicative());
            } else {
                return left;
            }
        }
    }

    NodeId parse_multiplicative() {
        NodeId left = parse_power();
        while (true) {
            if (accept(TokenKind::star)) {
                left = make_binary(BinaryOp::multiply, left, parse_power());
            } else if (accept(TokenKind::slash)) {
                left = make_binary(BinaryOp::divide, left, parse_power());
            } else {
                return left;
            }
        }
    }

    NodeId parse_power() {
        NodeId left = parse_unary();
        while (accept(TokenKind::caret)) { // left-associative in Excel
            left = make_binary(BinaryOp::power, left, parse_unary());
        }
        return left;
    }

    NodeId parse_unary() {
        if (accept(TokenKind::minus)) {
            return make_unary(UnaryOp::minus, parse_unary());
        }
        if (accept(TokenKind::plus)) {
            return make_unary(UnaryOp::plus, parse_unary());
        }
        return parse_postfix();
    }

    NodeId parse_postfix() {
        NodeId node = parse_primary();
        while (accept(TokenKind::percent)) {
            Node percent;
            percent.kind = NodeKind::percent;
            percent.children.push_back(node);
            node = ast_.add(std::move(percent));
        }
        return node;
    }

    // ----- primaries ----------------------------------------------------------

    NodeId parse_primary() {
        const Token &token = peek();
        switch (token.kind) {
        case TokenKind::number: {
            Node node;
            node.kind = NodeKind::number;
            node.number = token.number;
            advance();
            return ast_.add(std::move(node));
        }
        case TokenKind::string: {
            Node node;
            node.kind = NodeKind::string;
            node.text = token.text;
            advance();
            return ast_.add(std::move(node));
        }
        case TokenKind::error_literal: {
            Node node;
            node.kind = NodeKind::error;
            node.text = token.text;
            advance();
            return ast_.add(std::move(node));
        }
        case TokenKind::lparen: {
            advance();
            const NodeId inner = parse_expression();
            expect(TokenKind::rparen, "expected ')'");
            return inner;
        }
        case TokenKind::quoted_name: {
            const std::string sheet = token.text;
            advance();
            expect(TokenKind::bang, "expected '!' after quoted sheet name");
            return parse_reference(sheet, true);
        }
        case TokenKind::identifier:
            return parse_identifier();
        default:
            fail("expected expression");
        }
    }

    NodeId parse_identifier() {
        const Token token = advance(); // identifier
        // Function call: identifier immediately followed by '('.
        if (peek().kind == TokenKind::lparen) {
            advance();
            Node call;
            call.kind = NodeKind::call;
            call.text = token.text;
            if (peek().kind != TokenKind::rparen) {
                call.children.push_back(parse_argument());
                while (accept(TokenKind::comma)) {
                    call.children.push_back(parse_argument());
                }
            }
            expect(TokenKind::rparen, "expected ')' after arguments");
            return ast_.add(std::move(call));
        }
        // Unquoted sheet qualifier: identifier '!' reference.
        if (peek().kind == TokenKind::bang) {
            advance();
            return parse_reference(token.text, true);
        }
        return finish_reference(token, false, {});
    }

    NodeId parse_argument() {
        if (peek().kind == TokenKind::comma || peek().kind == TokenKind::rparen) {
            Node missing;
            missing.kind = NodeKind::missing;
            return ast_.add(std::move(missing));
        }
        return parse_expression();
    }

    // Parses the part after "sheet!"; also used for unqualified identifiers
    // via finish_reference.
    NodeId parse_reference(const std::string &sheet, bool has_sheet) {
        if (peek().kind != TokenKind::identifier) {
            fail("expected cell reference or name after '!'");
        }
        const Token token = advance();
        return finish_reference(token, has_sheet, sheet);
    }

    NodeId finish_reference(const Token &token, bool has_sheet, const std::string &sheet) {
        CellAddress first;
        if (parse_cell_identifier(token.text, first)) {
            if (accept(TokenKind::colon)) {
                if (peek().kind != TokenKind::identifier) {
                    fail("expected cell reference after ':'");
                }
                const Token second_token = advance();
                CellAddress second;
                if (!parse_cell_identifier(second_token.text, second)) {
                    throw ParseError("invalid range end '" + second_token.text + "'",
                                     second_token.offset);
                }
                Node range;
                range.kind = NodeKind::range;
                range.address_a = first;
                range.address_b = second;
                range.has_sheet = has_sheet;
                range.sheet = sheet;
                return ast_.add(std::move(range));
            }
            Node cell;
            cell.kind = NodeKind::cell;
            cell.address_a = first;
            cell.has_sheet = has_sheet;
            cell.sheet = sheet;
            return ast_.add(std::move(cell));
        }
        if (!has_sheet && (token.text == "TRUE" || token.text == "FALSE")) {
            Node boolean;
            boolean.kind = NodeKind::boolean;
            boolean.boolean_value = token.text == "TRUE";
            return ast_.add(std::move(boolean));
        }
        Node name;
        name.kind = NodeKind::name;
        name.text = token.text;
        name.has_sheet = has_sheet;
        name.sheet = sheet;
        return ast_.add(std::move(name));
    }

    NodeId make_unary(UnaryOp op, NodeId operand) {
        Node node;
        node.kind = NodeKind::unary;
        node.unary_op = op;
        node.children.push_back(operand);
        return ast_.add(std::move(node));
    }

    NodeId make_binary(BinaryOp op, NodeId left, NodeId right) {
        Node node;
        node.kind = NodeKind::binary;
        node.binary_op = op;
        node.children.push_back(left);
        node.children.push_back(right);
        return ast_.add(std::move(node));
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    Ast ast_;
};

} // namespace

Ast parse_formula(const std::string &formula) {
    const bool has_equals = !formula.empty() && formula[0] == '=';
    Parser parser(has_equals ? formula.substr(1) : formula);
    return parser.run();
}

bool parse_a1_reference(const std::string &text, CellAddress &address) {
    return parse_cell_identifier(text, address);
}

} // namespace xlpp
