#include "evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace xlpp {
namespace detail {
namespace {

// ------------------------------ coercions -----------------------------------

char lower_ascii_char(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

int compare_text_ci(const std::string &a, const std::string &b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char ca = lower_ascii_char(a[i]);
        const char cb = lower_ascii_char(b[i]);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }
    if (a.size() == b.size()) {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

bool text_to_number(const std::string &text, double &out) {
    const char *begin = text.c_str();
    char *end = nullptr;
    const double parsed = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

// number or error
Value to_number(const Value &value) {
    switch (value.kind) {
    case ValueKind::blank:
        return Value::make_number(0.0);
    case ValueKind::number:
        return value;
    case ValueKind::boolean:
        return Value::make_number(value.boolean ? 1.0 : 0.0);
    case ValueKind::text: {
        double parsed = 0.0;
        if (text_to_number(value.text, parsed)) {
            return Value::make_number(parsed);
        }
        return Value::make_error(ErrorCode::value_error);
    }
    case ValueKind::error:
        return value;
    }
    return Value::make_error(ErrorCode::value_error);
}

// boolean or error; blank -> FALSE (Excel IF semantics)
Value to_boolean(const Value &value) {
    switch (value.kind) {
    case ValueKind::blank:
        return Value::make_boolean(false);
    case ValueKind::number:
        return Value::make_boolean(value.number != 0.0);
    case ValueKind::boolean:
        return value;
    case ValueKind::text: {
        const std::string upper = upper_ascii(value.text);
        if (upper == "TRUE") {
            return Value::make_boolean(true);
        }
        if (upper == "FALSE") {
            return Value::make_boolean(false);
        }
        return Value::make_error(ErrorCode::value_error);
    }
    case ValueKind::error:
        return value;
    }
    return Value::make_error(ErrorCode::value_error);
}

// display text for & / CONCATENATE; errors propagate at call sites
std::string to_display_text(const Value &value) {
    switch (value.kind) {
    case ValueKind::blank: return "";
    case ValueKind::number: return number_to_text(value.number);
    case ValueKind::boolean: return value.boolean ? "TRUE" : "FALSE";
    case ValueKind::text: return value.text;
    case ValueKind::error: return error_text(value.error);
    }
    return "";
}

int type_rank(const Value &value) {
    switch (value.kind) {
    case ValueKind::number: return 0;
    case ValueKind::text: return 1;
    case ValueKind::boolean: return 2;
    default: return 0;
    }
}

// Excel comparison: number < text < FALSE < TRUE; text case-insensitive;
// blank coerces to the other operand's type.
Value compare(const Value &lhs, const Value &rhs, BinaryOp op) {
    if (lhs.is_error()) {
        return lhs;
    }
    if (rhs.is_error()) {
        return rhs;
    }
    Value a = lhs;
    Value b = rhs;
    if (a.kind == ValueKind::blank && b.kind == ValueKind::blank) {
        a = Value::make_number(0.0);
        b = Value::make_number(0.0);
    } else if (a.kind == ValueKind::blank) {
        switch (b.kind) {
        case ValueKind::number: a = Value::make_number(0.0); break;
        case ValueKind::text: a = Value::make_text(""); break;
        case ValueKind::boolean: a = Value::make_boolean(false); break;
        default: break;
        }
    } else if (b.kind == ValueKind::blank) {
        switch (a.kind) {
        case ValueKind::number: b = Value::make_number(0.0); break;
        case ValueKind::text: b = Value::make_text(""); break;
        case ValueKind::boolean: b = Value::make_boolean(false); break;
        default: break;
        }
    }

    int order = 0;
    if (a.kind != b.kind) {
        order = type_rank(a) < type_rank(b) ? -1 : 1;
    } else {
        switch (a.kind) {
        case ValueKind::number:
            order = a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
            break;
        case ValueKind::text:
            order = compare_text_ci(a.text, b.text);
            break;
        case ValueKind::boolean:
            order = static_cast<int>(a.boolean) - static_cast<int>(b.boolean);
            break;
        default:
            break;
        }
    }

    bool result = false;
    switch (op) {
    case BinaryOp::equal: result = order == 0; break;
    case BinaryOp::not_equal: result = order != 0; break;
    case BinaryOp::less: result = order < 0; break;
    case BinaryOp::less_equal: result = order <= 0; break;
    case BinaryOp::greater: result = order > 0; break;
    case BinaryOp::greater_equal: result = order >= 0; break;
    default: break;
    }
    return Value::make_boolean(result);
}

Value arithmetic(BinaryOp op, const Value &lhs, const Value &rhs) {
    const Value a = to_number(lhs);
    if (a.is_error()) {
        return a;
    }
    const Value b = to_number(rhs);
    if (b.is_error()) {
        return b;
    }
    double result = 0.0;
    switch (op) {
    case BinaryOp::add: result = a.number + b.number; break;
    case BinaryOp::subtract: result = a.number - b.number; break;
    case BinaryOp::multiply: result = a.number * b.number; break;
    case BinaryOp::divide:
        if (b.number == 0.0) {
            return Value::make_error(ErrorCode::div0);
        }
        result = a.number / b.number;
        break;
    case BinaryOp::power:
        if (a.number == 0.0 && b.number == 0.0) {
            return Value::make_error(ErrorCode::num_error);
        }
        result = std::pow(a.number, b.number);
        break;
    default:
        return Value::make_error(ErrorCode::value_error);
    }
    if (!std::isfinite(result)) {
        return Value::make_error(ErrorCode::num_error);
    }
    return Value::make_number(result);
}

// Snaps near-integer intermediate products before ceil/floor so binary noise
// (e.g. 1.6*100 = 160.0000000000000284) does not flip the rounding direction.
double snap_integer(double value) {
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9 * std::max(1.0, std::fabs(value))) {
        return rounded;
    }
    return value;
}

} // namespace

bool is_deferred_function(const std::string &upper_name) {
    return upper_name == "INDIRECT" || upper_name == "ADDRESS" || upper_name == "OFFSET" ||
           upper_name == "MMULT" || upper_name == "MINVERSE";
}

// ------------------------------- evaluator ----------------------------------

Value Evaluator::evaluate_cell(CellKey anchor, const Ast &ast) {
    anchor_ = anchor;
    name_depth_ = 0; // may be non-zero if a prior attempt threw PendingCell
    const EvalResult result = eval(ast, ast.root());
    Value value = scalar(result);
    if (value.kind == ValueKind::blank) {
        return Value::make_number(0.0); // Excel: =Ref to blank yields 0
    }
    return value;
}

Value Evaluator::cell_value(std::int32_t sheet, std::uint32_t column, std::uint32_t row) const {
    const CellKey key = make_cell_key(static_cast<std::uint32_t>(sheet), column, row);
    const auto computed = computed_.find(key);
    if (computed != computed_.end()) {
        return computed->second;
    }
    const auto cell = model_.cells.find(key);
    if (cell != model_.cells.end()) {
        if (cell->second.formula >= 0) {
            throw PendingCell{key}; // engine computes it and retries
        }
        return cell->second.cached;
    }
    return Value::make_blank();
}

std::int32_t Evaluator::resolve_sheet(const Node &node) {
    if (!node.has_sheet) {
        return static_cast<std::int32_t>(key_sheet(anchor_));
    }
    const auto cached = sheet_cache_.find(node.sheet);
    if (cached != sheet_cache_.end()) {
        return cached->second;
    }
    const std::int32_t index = model_.sheet_index(node.sheet);
    sheet_cache_.emplace(node.sheet, index);
    return index;
}

Evaluator::EvalResult Evaluator::eval_reference(const Node &node) {
    EvalResult result;
    const std::int32_t sheet = resolve_sheet(node);
    if (sheet < 0) {
        result.value = Value::make_error(ErrorCode::ref_error);
        return result;
    }
    if (node.kind == NodeKind::cell) {
        result.value = cell_value(sheet, node.address_a.column, node.address_a.row);
        result.from_reference = true;
        return result;
    }
    result.is_range = true;
    result.from_reference = true;
    result.range.sheet = sheet;
    result.range.col_first = std::min(node.address_a.column, node.address_b.column);
    result.range.col_last = std::max(node.address_a.column, node.address_b.column);
    result.range.row_first = std::min(node.address_a.row, node.address_b.row);
    result.range.row_last = std::max(node.address_a.row, node.address_b.row);
    return result;
}

Evaluator::EvalResult Evaluator::eval_name(const Node &node) {
    EvalResult result;
    const std::int32_t anchor_sheet = static_cast<std::int32_t>(key_sheet(anchor_));
    const std::string cache_key = node.text + '\x1f' + std::to_string(anchor_sheet);
    const NameDefinition *definition = nullptr;
    const auto cached = name_cache_.find(cache_key);
    if (cached != name_cache_.end()) {
        definition = cached->second;
    } else {
        definition = model_.find_name(node.text, anchor_sheet);
        name_cache_.emplace(cache_key, definition);
    }
    if (definition == nullptr || !definition->parsed) {
        result.value = Value::make_error(ErrorCode::name_error);
        return result;
    }
    if (name_depth_ > 8) {
        result.value = Value::make_error(ErrorCode::name_error);
        return result;
    }
    ++name_depth_;
    result = eval(definition->ast, definition->ast.root());
    --name_depth_;
    return result;
}

Value Evaluator::scalar(const EvalResult &result) {
    if (!result.is_range) {
        return result.value;
    }
    const RangeRect &r = result.range;
    if (r.col_first == r.col_last && r.row_first == r.row_last) {
        return cell_value(r.sheet, r.col_first, r.row_first);
    }
    // Implicit intersection with the anchor cell (legacy Excel semantics).
    const std::uint32_t anchor_row = key_row(anchor_);
    const std::uint32_t anchor_col = key_column(anchor_);
    const bool same_sheet = r.sheet == static_cast<std::int32_t>(key_sheet(anchor_));
    if (same_sheet && r.col_first == r.col_last && anchor_row >= r.row_first &&
        anchor_row <= r.row_last) {
        return cell_value(r.sheet, r.col_first, anchor_row);
    }
    if (same_sheet && r.row_first == r.row_last && anchor_col >= r.col_first &&
        anchor_col <= r.col_last) {
        return cell_value(r.sheet, anchor_col, r.row_first);
    }
    return Value::make_error(ErrorCode::value_error);
}

Evaluator::EvalResult Evaluator::eval(const Ast &ast, NodeId id) {
    const Node &node = ast.at(id);
    EvalResult result;
    switch (node.kind) {
    case NodeKind::number:
        result.value = Value::make_number(node.number);
        return result;
    case NodeKind::string:
        result.value = Value::make_text(node.text);
        return result;
    case NodeKind::boolean:
        result.value = Value::make_boolean(node.boolean_value);
        return result;
    case NodeKind::error: {
        ErrorCode code = ErrorCode::value_error;
        parse_error_text(node.text, code);
        result.value = Value::make_error(code);
        return result;
    }
    case NodeKind::missing:
        result.value = Value::make_blank();
        return result;
    case NodeKind::cell:
    case NodeKind::range:
        return eval_reference(node);
    case NodeKind::name:
        return eval_name(node);
    case NodeKind::unary: {
        const Value operand = scalar(eval(ast, node.children[0]));
        if (operand.is_error()) {
            result.value = operand;
            return result;
        }
        const Value number = to_number(operand);
        if (number.is_error()) {
            result.value = number;
            return result;
        }
        result.value = Value::make_number(
            node.unary_op == UnaryOp::minus ? -number.number : number.number);
        return result;
    }
    case NodeKind::percent: {
        const Value operand = scalar(eval(ast, node.children[0]));
        const Value number = to_number(operand);
        if (number.is_error()) {
            result.value = number;
            return result;
        }
        result.value = Value::make_number(number.number / 100.0);
        return result;
    }
    case NodeKind::binary: {
        const Value lhs = scalar(eval(ast, node.children[0]));
        const Value rhs = scalar(eval(ast, node.children[1]));
        switch (node.binary_op) {
        case BinaryOp::equal:
        case BinaryOp::not_equal:
        case BinaryOp::less:
        case BinaryOp::less_equal:
        case BinaryOp::greater:
        case BinaryOp::greater_equal:
            result.value = compare(lhs, rhs, node.binary_op);
            return result;
        case BinaryOp::concat:
            if (lhs.is_error()) {
                result.value = lhs;
                return result;
            }
            if (rhs.is_error()) {
                result.value = rhs;
                return result;
            }
            result.value = Value::make_text(to_display_text(lhs) + to_display_text(rhs));
            return result;
        default:
            if (lhs.is_error()) {
                result.value = lhs;
                return result;
            }
            if (rhs.is_error()) {
                result.value = rhs;
                return result;
            }
            result.value = arithmetic(node.binary_op, lhs, rhs);
            return result;
        }
    }
    case NodeKind::call:
        result.value = eval_call(ast, node);
        return result;
    }
    result.value = Value::make_error(ErrorCode::value_error);
    return result;
}

// ------------------------------- functions ----------------------------------

Value Evaluator::eval_call(const Ast &ast, const Node &node) {
    const std::string name = upper_ascii(node.text);
    const auto &args = node.children;
    const std::size_t argc = args.size();

    auto arg = [&](std::size_t i) {
        return eval(ast, args[i]);
    };
    auto arg_scalar = [&](std::size_t i) {
        return scalar(eval(ast, args[i]));
    };
    auto arg_number = [&](std::size_t i) {
        return to_number(arg_scalar(i));
    };
    auto missing = [&](std::size_t i) {
        return i >= argc || ast.at(args[i]).kind == NodeKind::missing;
    };

    if (is_deferred_function(name)) {
        return Value::make_error(ErrorCode::unsupported);
    }

    // ---- lazy logic -----------------------------------------------------
    if (name == "IF") {
        if (argc < 2 || argc > 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value condition = to_boolean(arg_scalar(0));
        if (condition.is_error()) {
            return condition;
        }
        if (condition.boolean) {
            return arg_scalar(1);
        }
        if (argc == 3 && !missing(2)) {
            return arg_scalar(2);
        }
        return Value::make_boolean(false);
    }

    // ---- zero-argument --------------------------------------------------
    if (name == "PI") {
        return Value::make_number(3.14159265358979323846);
    }
    if (name == "TRUE") {
        return Value::make_boolean(true);
    }
    if (name == "FALSE") {
        return Value::make_boolean(false);
    }

    // ---- logic over arguments (eager, error-propagating) -----------------
    if (name == "AND" || name == "OR") {
        bool any = false;
        bool acc = name == "AND";
        for (std::size_t i = 0; i < argc; ++i) {
            const EvalResult r = arg(i);
            if (r.is_range) {
                for (std::uint32_t row = r.range.row_first; row <= r.range.row_last; ++row) {
                    for (std::uint32_t col = r.range.col_first; col <= r.range.col_last;
                         ++col) {
                        const Value v = cell_value(r.range.sheet, col, row);
                        if (v.is_error()) {
                            return v;
                        }
                        if (v.kind == ValueKind::number || v.kind == ValueKind::boolean) {
                            const bool b = v.kind == ValueKind::number ? v.number != 0.0
                                                                       : v.boolean;
                            acc = name == "AND" ? (acc && b) : (acc || b);
                            any = true;
                        }
                    }
                }
                continue;
            }
            if (r.value.is_error()) {
                return r.value;
            }
            if (r.value.kind == ValueKind::blank ||
                (r.from_reference && r.value.kind == ValueKind::text)) {
                continue; // Excel ignores blanks and referenced text in AND/OR
            }
            const Value b = to_boolean(r.value);
            if (b.is_error()) {
                return b;
            }
            acc = name == "AND" ? (acc && b.boolean) : (acc || b.boolean);
            any = true;
        }
        if (!any) {
            return Value::make_error(ErrorCode::value_error);
        }
        return Value::make_boolean(acc);
    }
    if (name == "NOT") {
        const Value b = to_boolean(arg_scalar(0));
        if (b.is_error()) {
            return b;
        }
        return Value::make_boolean(!b.boolean);
    }

    // ---- error inspection -------------------------------------------------
    if (name == "ISNA" || name == "ISERROR" || name == "ISERR" || name == "ISNUMBER") {
        const Value v = arg_scalar(0);
        if (v.is_diagnostic()) {
            return v;
        }
        if (name == "ISNA") {
            return Value::make_boolean(v.is_error() && v.error == ErrorCode::na);
        }
        if (name == "ISERROR") {
            return Value::make_boolean(v.is_error());
        }
        if (name == "ISERR") {
            return Value::make_boolean(v.is_error() && v.error != ErrorCode::na);
        }
        return Value::make_boolean(v.kind == ValueKind::number);
    }
    if (name == "ERROR.TYPE") {
        const Value v = arg_scalar(0);
        if (v.is_diagnostic()) {
            return v;
        }
        if (!v.is_error()) {
            return Value::make_error(ErrorCode::na);
        }
        switch (v.error) {
        case ErrorCode::null_error: return Value::make_number(1);
        case ErrorCode::div0: return Value::make_number(2);
        case ErrorCode::value_error: return Value::make_number(3);
        case ErrorCode::ref_error: return Value::make_number(4);
        case ErrorCode::name_error: return Value::make_number(5);
        case ErrorCode::num_error: return Value::make_number(6);
        default: return Value::make_number(7);
        }
    }

    // ---- aggregates over scalars and ranges --------------------------------
    if (name == "SUM" || name == "MAX" || name == "MIN" || name == "AVERAGE" ||
        name == "SUMSQ" || name == "COUNT") {
        double sum = 0.0;
        double best_max = -std::numeric_limits<double>::infinity();
        double best_min = std::numeric_limits<double>::infinity();
        std::size_t count = 0;
        for (std::size_t i = 0; i < argc; ++i) {
            const EvalResult r = arg(i);
            if (r.is_range) {
                for (std::uint32_t row = r.range.row_first; row <= r.range.row_last; ++row) {
                    for (std::uint32_t col = r.range.col_first; col <= r.range.col_last;
                         ++col) {
                        const Value v = cell_value(r.range.sheet, col, row);
                        if (v.is_error()) {
                            if (name == "COUNT") {
                                continue; // COUNT skips errors in ranges
                            }
                            return v;
                        }
                        if (v.kind != ValueKind::number) {
                            continue; // range aggregates use numbers only
                        }
                        sum += v.number;
                        sum += name == "SUMSQ" ? v.number * v.number - v.number : 0.0;
                        best_max = std::max(best_max, v.number);
                        best_min = std::min(best_min, v.number);
                        ++count;
                    }
                }
                continue;
            }
            if (r.value.is_error()) {
                if (name == "COUNT" && r.from_reference) {
                    continue; // COUNT ignores errors behind references
                }
                return r.value;
            }
            if (r.value.kind == ValueKind::blank) {
                continue;
            }
            if (r.from_reference && r.value.kind != ValueKind::number) {
                continue; // Excel skips text/booleans in referenced cells
            }
            const Value v = to_number(r.value);
            if (v.is_error()) {
                if (name == "COUNT") {
                    continue;
                }
                return v;
            }
            sum += v.number;
            sum += name == "SUMSQ" ? v.number * v.number - v.number : 0.0;
            best_max = std::max(best_max, v.number);
            best_min = std::min(best_min, v.number);
            ++count;
        }
        if (name == "COUNT") {
            return Value::make_number(static_cast<double>(count));
        }
        if (name == "SUM" || name == "SUMSQ") {
            return Value::make_number(sum);
        }
        if (name == "AVERAGE") {
            if (count == 0) {
                return Value::make_error(ErrorCode::div0);
            }
            return Value::make_number(sum / static_cast<double>(count));
        }
        if (count == 0) {
            return Value::make_number(0.0);
        }
        return Value::make_number(name == "MAX" ? best_max : best_min);
    }

    // ---- lookups ------------------------------------------------------------
    if (name == "VLOOKUP" || name == "HLOOKUP") {
        if (argc < 3 || argc > 4) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value key = arg_scalar(0);
        if (key.is_error()) {
            return key;
        }
        const EvalResult table = arg(1);
        if (!table.is_range) {
            return table.value.is_error() ? table.value
                                          : Value::make_error(ErrorCode::value_error);
        }
        const Value index_value = arg_number(2);
        if (index_value.is_error()) {
            return index_value;
        }
        const auto index = static_cast<std::int64_t>(index_value.number);
        bool approximate = true;
        if (argc == 4 && !missing(3)) {
            const Value flag = to_boolean(arg_scalar(3));
            if (flag.is_error()) {
                return flag;
            }
            approximate = flag.boolean;
        }
        const RangeRect &r = table.range;
        const bool vertical = name == "VLOOKUP";
        const std::uint32_t lanes = vertical ? r.row_last - r.row_first + 1
                                             : r.col_last - r.col_first + 1;
        const std::int64_t width = vertical ? r.col_last - r.col_first + 1
                                            : r.row_last - r.row_first + 1;
        if (index < 1) {
            return Value::make_error(ErrorCode::value_error);
        }
        if (index > width) {
            return Value::make_error(ErrorCode::ref_error);
        }
        std::int64_t match_lane = -1;
        for (std::uint32_t lane = 0; lane < lanes; ++lane) {
            const std::uint32_t col = vertical ? r.col_first : r.col_first + lane;
            const std::uint32_t row = vertical ? r.row_first + lane : r.row_first;
            const Value candidate = cell_value(r.sheet, col, row);
            if (candidate.kind != key.kind &&
                !(candidate.kind == ValueKind::number && key.kind == ValueKind::number)) {
                continue;
            }
            const Value cmp_eq = compare(candidate, key, BinaryOp::equal);
            if (cmp_eq.kind == ValueKind::boolean && cmp_eq.boolean) {
                match_lane = lane;
                if (!approximate) {
                    break;
                }
                continue;
            }
            if (approximate) {
                const Value cmp_le = compare(candidate, key, BinaryOp::less);
                if (cmp_le.kind == ValueKind::boolean && cmp_le.boolean) {
                    match_lane = lane;
                }
            }
        }
        if (match_lane < 0) {
            return Value::make_error(ErrorCode::na);
        }
        const std::uint32_t col = vertical
            ? r.col_first + static_cast<std::uint32_t>(index - 1)
            : r.col_first + static_cast<std::uint32_t>(match_lane);
        const std::uint32_t row = vertical
            ? r.row_first + static_cast<std::uint32_t>(match_lane)
            : r.row_first + static_cast<std::uint32_t>(index - 1);
        return cell_value(r.sheet, col, row);
    }
    if (name == "MATCH") {
        if (argc < 2 || argc > 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value key = arg_scalar(0);
        if (key.is_error()) {
            return key;
        }
        const EvalResult vector = arg(1);
        if (!vector.is_range) {
            return vector.value.is_error() ? vector.value
                                           : Value::make_error(ErrorCode::value_error);
        }
        int match_type = 1;
        if (argc == 3 && !missing(2)) {
            const Value type_value = arg_number(2);
            if (type_value.is_error()) {
                return type_value;
            }
            match_type = type_value.number > 0 ? 1 : (type_value.number < 0 ? -1 : 0);
        }
        const RangeRect &r = vector.range;
        const bool vertical = r.col_first == r.col_last;
        const std::uint32_t lanes = vertical ? r.row_last - r.row_first + 1
                                             : r.col_last - r.col_first + 1;
        std::int64_t best = -1;
        for (std::uint32_t lane = 0; lane < lanes; ++lane) {
            const std::uint32_t col = vertical ? r.col_first : r.col_first + lane;
            const std::uint32_t row = vertical ? r.row_first + lane : r.row_first;
            const Value candidate = cell_value(r.sheet, col, row);
            if (candidate.kind != key.kind &&
                !(candidate.kind == ValueKind::number && key.kind == ValueKind::number)) {
                continue;
            }
            const Value cmp_eq = compare(candidate, key, BinaryOp::equal);
            if (cmp_eq.kind == ValueKind::boolean && cmp_eq.boolean) {
                best = lane;
                if (match_type <= 0) {
                    break;
                }
                continue;
            }
            if (match_type == 1) {
                const Value cmp = compare(candidate, key, BinaryOp::less);
                if (cmp.kind == ValueKind::boolean && cmp.boolean) {
                    best = lane;
                }
            } else if (match_type == -1) {
                const Value cmp = compare(candidate, key, BinaryOp::greater);
                if (cmp.kind == ValueKind::boolean && cmp.boolean) {
                    best = lane;
                }
            }
        }
        if (best < 0) {
            return Value::make_error(ErrorCode::na);
        }
        return Value::make_number(static_cast<double>(best + 1));
    }
    if (name == "INDEX") {
        if (argc < 2 || argc > 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const EvalResult range = arg(0);
        if (!range.is_range) {
            return range.value.is_error() ? range.value
                                          : Value::make_error(ErrorCode::value_error);
        }
        const Value row_value = arg_number(1);
        if (row_value.is_error()) {
            return row_value;
        }
        const RangeRect &r = range.range;
        const std::uint32_t height = r.row_last - r.row_first + 1;
        const std::uint32_t width = r.col_last - r.col_first + 1;
        std::int64_t row_index = static_cast<std::int64_t>(row_value.number);
        std::int64_t col_index = 1;
        if (argc == 3 && !missing(2)) {
            const Value col_value = arg_number(2);
            if (col_value.is_error()) {
                return col_value;
            }
            col_index = static_cast<std::int64_t>(col_value.number);
        } else if (height == 1 && width > 1) {
            col_index = row_index; // vector form on a row vector
            row_index = 1;
        }
        if (row_index < 1 || col_index < 1 ||
            row_index > static_cast<std::int64_t>(height) ||
            col_index > static_cast<std::int64_t>(width)) {
            return Value::make_error(ErrorCode::ref_error);
        }
        return cell_value(r.sheet, r.col_first + static_cast<std::uint32_t>(col_index - 1),
                          r.row_first + static_cast<std::uint32_t>(row_index - 1));
    }

    // ---- text ---------------------------------------------------------------
    if (name == "CONCATENATE") {
        std::string out;
        for (std::size_t i = 0; i < argc; ++i) {
            const Value v = arg_scalar(i);
            if (v.is_error()) {
                return v;
            }
            out += to_display_text(v);
        }
        return Value::make_text(out);
    }
    if (name == "LEN") {
        const Value v = arg_scalar(0);
        if (v.is_error()) {
            return v;
        }
        return Value::make_number(static_cast<double>(to_display_text(v).size()));
    }
    if (name == "MID") {
        if (argc != 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value text = arg_scalar(0);
        if (text.is_error()) {
            return text;
        }
        const Value start = arg_number(1);
        const Value length = arg_number(2);
        if (start.is_error()) {
            return start;
        }
        if (length.is_error()) {
            return length;
        }
        const auto start_index = static_cast<std::int64_t>(start.number);
        const auto count = static_cast<std::int64_t>(length.number);
        if (start_index < 1 || count < 0) {
            return Value::make_error(ErrorCode::value_error);
        }
        const std::string source = to_display_text(text);
        if (static_cast<std::size_t>(start_index) > source.size()) {
            return Value::make_text("");
        }
        return Value::make_text(source.substr(static_cast<std::size_t>(start_index - 1),
                                              static_cast<std::size_t>(count)));
    }
    if (name == "SEARCH") {
        if (argc < 2 || argc > 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value needle_value = arg_scalar(0);
        const Value haystack_value = arg_scalar(1);
        if (needle_value.is_error()) {
            return needle_value;
        }
        if (haystack_value.is_error()) {
            return haystack_value;
        }
        std::size_t start = 1;
        if (argc == 3 && !missing(2)) {
            const Value start_value = arg_number(2);
            if (start_value.is_error()) {
                return start_value;
            }
            if (start_value.number < 1) {
                return Value::make_error(ErrorCode::value_error);
            }
            start = static_cast<std::size_t>(start_value.number);
        }
        const std::string needle = upper_ascii(to_display_text(needle_value));
        const std::string haystack = upper_ascii(to_display_text(haystack_value));
        if (start > haystack.size() + 1) {
            return Value::make_error(ErrorCode::value_error);
        }
        const std::size_t position = haystack.find(needle, start - 1);
        if (position == std::string::npos) {
            return Value::make_error(ErrorCode::value_error);
        }
        return Value::make_number(static_cast<double>(position + 1));
    }
    if (name == "VALUE") {
        const Value v = arg_scalar(0);
        if (v.is_error()) {
            return v;
        }
        if (v.kind == ValueKind::number) {
            return v;
        }
        double parsed = 0.0;
        if (v.kind == ValueKind::text && text_to_number(v.text, parsed)) {
            return Value::make_number(parsed);
        }
        return Value::make_error(ErrorCode::value_error);
    }
    if (name == "TEXT") {
        if (argc != 2) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value number = arg_number(0);
        if (number.is_error()) {
            return number;
        }
        const Value format = arg_scalar(1);
        if (format.is_error()) {
            return format;
        }
        // Corpus formats are "0", "0.0", "0.00" — fixed decimal places.
        const std::string &fmt = format.text;
        if (!fmt.empty() && fmt[0] == '0' &&
            (fmt.size() == 1 || (fmt[1] == '.' && fmt.find_first_not_of('0', 2) ==
                                                      std::string::npos))) {
            const int decimals = fmt.size() <= 2 ? 0 : static_cast<int>(fmt.size() - 2);
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, number.number);
            return Value::make_text(buffer);
        }
        return Value::make_error(ErrorCode::unsupported);
    }
    if (name == "FIXED") {
        if (argc < 1 || argc > 3) {
            return Value::make_error(ErrorCode::value_error);
        }
        const Value number = arg_number(0);
        if (number.is_error()) {
            return number;
        }
        int decimals = 2;
        if (argc >= 2 && !missing(1)) {
            const Value d = arg_number(1);
            if (d.is_error()) {
                return d;
            }
            decimals = static_cast<int>(d.number);
        }
        bool no_commas = false;
        if (argc == 3 && !missing(2)) {
            const Value flag = to_boolean(arg_scalar(2));
            if (flag.is_error()) {
                return flag;
            }
            no_commas = flag.boolean;
        }
        if (decimals < 0) {
            return Value::make_error(ErrorCode::value_error);
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, number.number);
        std::string text = buffer;
        if (!no_commas) {
            const std::size_t dot = text.find('.');
            std::size_t end = dot == std::string::npos ? text.size() : dot;
            const std::size_t first = text[0] == '-' ? 1 : 0;
            while (end > first + 3) {
                end -= 3;
                text.insert(end, ",");
            }
        }
        return Value::make_text(text);
    }

    // ---- single-argument math -------------------------------------------------
    if (argc == 1 || name == "LOG" || name == "ROUND" || name == "ROUNDUP" ||
        name == "ROUNDDOWN" || name == "CEILING" || name == "FLOOR" || name == "MOD") {
        const Value first = arg_number(0);
        if (first.is_error()) {
            return first;
        }
        const double x = first.number;
        auto finite = [](double v) {
            return std::isfinite(v) ? Value::make_number(v)
                                    : Value::make_error(ErrorCode::num_error);
        };
        if (name == "ABS") return Value::make_number(std::fabs(x));
        if (name == "INT") return Value::make_number(std::floor(x));
        if (name == "SQRT") {
            return x < 0 ? Value::make_error(ErrorCode::num_error)
                         : Value::make_number(std::sqrt(x));
        }
        if (name == "EXP") return finite(std::exp(x));
        if (name == "LN") {
            return x <= 0 ? Value::make_error(ErrorCode::num_error)
                          : Value::make_number(std::log(x));
        }
        if (name == "LOG10") {
            return x <= 0 ? Value::make_error(ErrorCode::num_error)
                          : Value::make_number(std::log10(x));
        }
        if (name == "SIN") return Value::make_number(std::sin(x));
        if (name == "COS") return Value::make_number(std::cos(x));
        if (name == "TAN") return finite(std::tan(x));
        if (name == "ASIN") {
            return x < -1 || x > 1 ? Value::make_error(ErrorCode::num_error)
                                   : Value::make_number(std::asin(x));
        }
        if (name == "ATAN") return Value::make_number(std::atan(x));
        if (name == "SINH") return finite(std::sinh(x));
        if (name == "COSH") return finite(std::cosh(x));
        if (name == "LOG") {
            double base = 10.0;
            if (argc >= 2 && !missing(1)) {
                const Value b = arg_number(1);
                if (b.is_error()) {
                    return b;
                }
                base = b.number;
            }
            if (x <= 0 || base <= 0 || base == 1.0) {
                return Value::make_error(ErrorCode::num_error);
            }
            return Value::make_number(std::log(x) / std::log(base));
        }
        if (name == "ROUND" || name == "ROUNDUP" || name == "ROUNDDOWN") {
            if (argc != 2) {
                return Value::make_error(ErrorCode::value_error);
            }
            const Value digits_value = arg_number(1);
            if (digits_value.is_error()) {
                return digits_value;
            }
            const int digits = static_cast<int>(digits_value.number);
            const double factor = std::pow(10.0, digits);
            const double scaled = snap_integer(std::fabs(x) * factor);
            double magnitude = 0.0;
            if (name == "ROUND") {
                magnitude = std::floor(scaled + 0.5);
            } else if (name == "ROUNDUP") {
                magnitude = std::ceil(scaled);
            } else {
                magnitude = std::floor(scaled);
            }
            return finite((x < 0 ? -magnitude : magnitude) / factor);
        }
        if (name == "CEILING" || name == "FLOOR") {
            if (argc != 2) {
                return Value::make_error(ErrorCode::value_error);
            }
            const Value significance = arg_number(1);
            if (significance.is_error()) {
                return significance;
            }
            const double sig = significance.number;
            if (sig == 0.0) {
                return name == "FLOOR" && x != 0.0
                    ? Value::make_error(ErrorCode::div0)
                    : Value::make_number(0.0);
            }
            if (x > 0 && sig < 0) {
                return Value::make_error(ErrorCode::num_error);
            }
            const double quotient = snap_integer(x / sig);
            const double steps = name == "CEILING" ? std::ceil(quotient)
                                                   : std::floor(quotient);
            return finite(steps * sig);
        }
        if (name == "MOD") {
            if (argc != 2) {
                return Value::make_error(ErrorCode::value_error);
            }
            const Value divisor = arg_number(1);
            if (divisor.is_error()) {
                return divisor;
            }
            if (divisor.number == 0.0) {
                return Value::make_error(ErrorCode::div0);
            }
            const double quotient = std::floor(snap_integer(x / divisor.number));
            return finite(x - divisor.number * quotient);
        }
    }

    return Value::make_error(ErrorCode::unsupported); // outside frozen inventory (NFR4)
}

} // namespace detail
} // namespace xlpp
