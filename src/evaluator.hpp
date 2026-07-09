#pragma once

// Internal expression evaluator (not part of the public API). Evaluates one
// formula AST against the workbook model plus the computed-value store that
// the Engine fills in dependency order.

#include <xlpp/ast.hpp>
#include <xlpp/value.hpp>
#include <xlpp/workbook_model.hpp>

#include <unordered_map>

namespace xlpp {
namespace detail {

struct RangeRect {
    std::int32_t sheet = -1;
    std::uint32_t col_first = 0, row_first = 0;
    std::uint32_t col_last = 0, row_last = 0;
};

// Thrown when evaluation reads a formula cell that has not been computed yet.
// The Engine catches it, computes the dependency, and retries — this is what
// makes dependency discovery dynamic (only cells actually read count), which
// matches Excel: lookup tables may overlap computed columns without creating
// circular references (e.g. TNY-4's TinySwitch_4_Table).
struct PendingCell {
    CellKey key;
};

class Evaluator {
public:
    Evaluator(const WorkbookModel &model, const std::unordered_map<CellKey, Value> &computed)
        : model_(model), computed_(computed) {
    }

    // anchor: the cell owning the formula (implicit intersection, name scope).
    Value evaluate_cell(CellKey anchor, const Ast &ast);

private:
    struct EvalResult {
        bool is_range = false;
        // True when the value came from a cell reference (directly or via a
        // defined name). Excel's aggregates and AND/OR skip text/boolean/blank
        // in referenced cells but coerce literal arguments — the distinction
        // needs to survive evaluation.
        bool from_reference = false;
        Value value;
        RangeRect range;
    };

    EvalResult eval(const Ast &ast, NodeId id);
    EvalResult eval_reference(const Node &node);
    EvalResult eval_name(const Node &node);
    Value eval_call(const Ast &ast, const Node &node);
    Value scalar(const EvalResult &result);          // implicit intersection
    Value cell_value(std::int32_t sheet, std::uint32_t column, std::uint32_t row) const;
    std::int32_t resolve_sheet(const Node &node);

    const WorkbookModel &model_;
    const std::unordered_map<CellKey, Value> &computed_;
    CellKey anchor_ = 0;
    int name_depth_ = 0;
    // Hot-path caches: sheet-name and defined-name resolution both involve
    // case-insensitive string work; large workbooks resolve the same few
    // thousand names millions of times.
    std::unordered_map<std::string, std::int32_t> sheet_cache_;
    std::unordered_map<std::string, const NameDefinition *> name_cache_;
};

// Function names deferred to Phase 4/5 — evaluate to the `unsupported`
// diagnostic and are excluded from match-rate gates.
bool is_deferred_function(const std::string &upper_name);

} // namespace detail
} // namespace xlpp
