#pragma once

// Formula AST for the frozen corpus grammar (IMPLEMENTATION_PLAN.md, Phase 1).
// Nodes live in a flat pool owned by Ast; children are referenced by NodeId.

#include <cstdint>
#include <string>
#include <vector>

namespace xlpp {

using NodeId = std::uint32_t;
constexpr NodeId invalid_node = 0xFFFFFFFFu;

enum class NodeKind : std::uint8_t {
    number,   // numeric literal
    string,   // string literal (text, unescaped)
    boolean,  // TRUE / FALSE literal (without parentheses)
    error,    // error literal, e.g. #REF! (text)
    cell,     // single cell reference (address_a, optional sheet)
    range,    // rectangular range (address_a:address_b, optional sheet)
    name,     // defined-name reference (text, optional sheet qualifier)
    missing,  // omitted function argument
    unary,    // unary_op applied to children[0]
    binary,   // binary_op applied to children[0], children[1]
    percent,  // postfix % applied to children[0]
    call,     // function call: text = function name, children = arguments
};

enum class UnaryOp : std::uint8_t { plus, minus };

enum class BinaryOp : std::uint8_t {
    add, subtract, multiply, divide, power, concat,
    equal, not_equal, less, less_equal, greater, greater_equal,
};

struct CellAddress {
    std::uint32_t row = 0;    // 1-based
    std::uint32_t column = 0; // 1-based (A = 1)
    bool absolute_row = false;
    bool absolute_column = false;
};

struct Node {
    NodeKind kind = NodeKind::missing;
    UnaryOp unary_op = UnaryOp::plus;
    BinaryOp binary_op = BinaryOp::add;
    bool boolean_value = false;
    bool has_sheet = false;
    double number = 0.0;
    std::string text;  // string value / error text / name / function name
    std::string sheet; // sheet qualifier (unescaped) when has_sheet
    CellAddress address_a;
    CellAddress address_b;
    std::vector<NodeId> children;
};

class Ast {
public:
    NodeId add(Node node) {
        nodes_.push_back(std::move(node));
        return static_cast<NodeId>(nodes_.size() - 1);
    }

    const Node &at(NodeId id) const {
        return nodes_[id];
    }

    std::size_t size() const {
        return nodes_.size();
    }

    NodeId root() const {
        return root_;
    }

    void set_root(NodeId id) {
        root_ = id;
    }

private:
    std::vector<Node> nodes_;
    NodeId root_ = invalid_node;
};

// Canonical text form (compound operands fully parenthesized); re-parsing the
// output must yield a structurally identical AST — the Phase 1 gate.
std::string to_formula(const Ast &ast);

// Deep structural equality of two subtrees.
bool equal(const Ast &lhs, NodeId lhs_id, const Ast &rhs, NodeId rhs_id);

// Column letters (1-based) <-> text, shared by printer and parser.
std::string column_letters(std::uint32_t column);

} // namespace xlpp
