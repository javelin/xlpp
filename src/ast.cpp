#include <xlpp/ast.hpp>

#include <cmath>
#include <cstdio>

namespace xlpp {
namespace {

const char *binary_op_text(BinaryOp op) {
    switch (op) {
    case BinaryOp::add: return "+";
    case BinaryOp::subtract: return "-";
    case BinaryOp::multiply: return "*";
    case BinaryOp::divide: return "/";
    case BinaryOp::power: return "^";
    case BinaryOp::concat: return "&";
    case BinaryOp::equal: return "=";
    case BinaryOp::not_equal: return "<>";
    case BinaryOp::less: return "<";
    case BinaryOp::less_equal: return "<=";
    case BinaryOp::greater: return ">";
    case BinaryOp::greater_equal: return ">=";
    }
    return "?";
}

bool needs_quoting(const std::string &sheet) {
    if (sheet.empty()) {
        return true;
    }
    for (const char c : sheet) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                           static_cast<unsigned char>(c) >= 0x80;
        if (!plain) {
            return true;
        }
    }
    // A sheet name that itself looks like a cell reference must be quoted.
    return (sheet[0] >= '0' && sheet[0] <= '9') || sheet[0] == '.';
}

void append_sheet(std::string &out, const Node &node) {
    if (!node.has_sheet) {
        return;
    }
    if (needs_quoting(node.sheet)) {
        out += '\'';
        for (const char c : node.sheet) {
            if (c == '\'') {
                out += "''";
            } else {
                out += c;
            }
        }
        out += '\'';
    } else {
        out += node.sheet;
    }
    out += '!';
}

void append_address(std::string &out, const CellAddress &address) {
    if (address.absolute_column) {
        out += '$';
    }
    out += column_letters(address.column);
    if (address.absolute_row) {
        out += '$';
    }
    out += std::to_string(address.row);
}

void append_number(std::string &out, double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}

// Compound operands are printed inside parentheses so precedence is explicit;
// parentheses create no AST node, so the round-trip stays structurally equal.
bool is_compound(const Node &node) {
    return node.kind == NodeKind::unary || node.kind == NodeKind::binary ||
           node.kind == NodeKind::percent;
}

void print_node(const Ast &ast, NodeId id, std::string &out);

void print_operand(const Ast &ast, NodeId id, std::string &out) {
    if (is_compound(ast.at(id))) {
        out += '(';
        print_node(ast, id, out);
        out += ')';
    } else {
        print_node(ast, id, out);
    }
}

void print_node(const Ast &ast, NodeId id, std::string &out) {
    const Node &node = ast.at(id);
    switch (node.kind) {
    case NodeKind::number:
        append_number(out, node.number);
        break;
    case NodeKind::string:
        out += '"';
        for (const char c : node.text) {
            if (c == '"') {
                out += "\"\"";
            } else {
                out += c;
            }
        }
        out += '"';
        break;
    case NodeKind::boolean:
        out += node.boolean_value ? "TRUE" : "FALSE";
        break;
    case NodeKind::error:
        out += node.text;
        break;
    case NodeKind::cell:
        append_sheet(out, node);
        append_address(out, node.address_a);
        break;
    case NodeKind::range:
        append_sheet(out, node);
        append_address(out, node.address_a);
        out += ':';
        append_address(out, node.address_b);
        break;
    case NodeKind::name:
        append_sheet(out, node);
        out += node.text;
        break;
    case NodeKind::missing:
        break;
    case NodeKind::unary:
        out += node.unary_op == UnaryOp::minus ? '-' : '+';
        print_operand(ast, node.children[0], out);
        break;
    case NodeKind::binary:
        print_operand(ast, node.children[0], out);
        out += binary_op_text(node.binary_op);
        print_operand(ast, node.children[1], out);
        break;
    case NodeKind::percent:
        print_operand(ast, node.children[0], out);
        out += '%';
        break;
    case NodeKind::call:
        out += node.text;
        out += '(';
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            print_node(ast, node.children[i], out);
        }
        out += ')';
        break;
    }
}

bool address_equal(const CellAddress &lhs, const CellAddress &rhs) {
    return lhs.row == rhs.row && lhs.column == rhs.column &&
           lhs.absolute_row == rhs.absolute_row && lhs.absolute_column == rhs.absolute_column;
}

} // namespace

std::string column_letters(std::uint32_t column) {
    std::string letters;
    while (column > 0) {
        const std::uint32_t remainder = (column - 1) % 26;
        letters.insert(letters.begin(), static_cast<char>('A' + remainder));
        column = (column - 1) / 26;
    }
    return letters;
}

std::string to_formula(const Ast &ast) {
    std::string out;
    if (ast.root() != invalid_node) {
        print_node(ast, ast.root(), out);
    }
    return out;
}

bool equal(const Ast &lhs, NodeId lhs_id, const Ast &rhs, NodeId rhs_id) {
    const Node &a = lhs.at(lhs_id);
    const Node &b = rhs.at(rhs_id);
    if (a.kind != b.kind || a.children.size() != b.children.size()) {
        return false;
    }
    switch (a.kind) {
    case NodeKind::number:
        // Bit-identical doubles required: the printer emits %.17g, which
        // round-trips exactly.
        if (!(a.number == b.number || (std::isnan(a.number) && std::isnan(b.number)))) {
            return false;
        }
        break;
    case NodeKind::string:
    case NodeKind::error:
        if (a.text != b.text) {
            return false;
        }
        break;
    case NodeKind::boolean:
        if (a.boolean_value != b.boolean_value) {
            return false;
        }
        break;
    case NodeKind::cell:
        if (a.has_sheet != b.has_sheet || a.sheet != b.sheet ||
            !address_equal(a.address_a, b.address_a)) {
            return false;
        }
        break;
    case NodeKind::range:
        if (a.has_sheet != b.has_sheet || a.sheet != b.sheet ||
            !address_equal(a.address_a, b.address_a) ||
            !address_equal(a.address_b, b.address_b)) {
            return false;
        }
        break;
    case NodeKind::name:
        if (a.has_sheet != b.has_sheet || a.sheet != b.sheet || a.text != b.text) {
            return false;
        }
        break;
    case NodeKind::missing:
        break;
    case NodeKind::unary:
        if (a.unary_op != b.unary_op) {
            return false;
        }
        break;
    case NodeKind::binary:
        if (a.binary_op != b.binary_op) {
            return false;
        }
        break;
    case NodeKind::percent:
        break;
    case NodeKind::call:
        if (a.text != b.text) {
            return false;
        }
        break;
    }
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!equal(lhs, a.children[i], rhs, b.children[i])) {
            return false;
        }
    }
    return true;
}

} // namespace xlpp
