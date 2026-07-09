#pragma once

// In-memory workbook snapshot (PRD FR1): cached cell values, parsed formula
// ASTs, resolved defined names, and calculation settings. Built once from
// xlnt + the sidecar; the Engine evaluates against this model.

#include <xlpp/ast.hpp>
#include <xlpp/sidecar.hpp>
#include <xlpp/value.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlpp {

using CellKey = std::uint64_t;

constexpr CellKey make_cell_key(std::uint32_t sheet, std::uint32_t column, std::uint32_t row) {
    return (static_cast<CellKey>(sheet) << 48) | (static_cast<CellKey>(column) << 28) | row;
}

constexpr std::uint32_t key_sheet(CellKey key) {
    return static_cast<std::uint32_t>(key >> 48);
}

constexpr std::uint32_t key_column(CellKey key) {
    return static_cast<std::uint32_t>((key >> 28) & 0xFFFFFu);
}

constexpr std::uint32_t key_row(CellKey key) {
    return static_cast<std::uint32_t>(key & 0x0FFFFFFFu);
}

struct CellRecord {
    Value cached;              // value stored in the file (oracle / input)
    std::int32_t formula = -1; // index into WorkbookModel::formulas, -1 = input cell
};

struct NameDefinition {
    std::int32_t sheet_scope = -1; // -1 = workbook-scoped
    bool parsed = false;           // false => using the name yields #NAME?
    Ast ast;                       // definition expression (reference/constant)
};

class WorkbookModel {
public:
    // Loads the workbook; throws std::runtime_error on load failure. Formula
    // parse failures are recorded per cell (formula_parse_failures) and the
    // cell downgraded to an input holding its cached value — NFR4, no crash.
    static WorkbookModel load(const std::string &xlsx_path);

    std::vector<std::string> sheet_names;               // workbook order
    std::unordered_map<CellKey, CellRecord> cells;      // all non-empty cells
    std::vector<Ast> formulas;                          // parsed formula per formula cell
    std::vector<CellKey> formula_cells;                 // anchor of formulas[i]
    std::unordered_map<std::string, std::vector<NameDefinition>> names; // key: upper-case
    CalcSettings calc;
    std::size_t formula_parse_failures = 0;

    // Case-insensitive sheet lookup; returns -1 if absent.
    std::int32_t sheet_index(const std::string &name) const;

    // Case-insensitive name lookup honoring sheet scope (sheet-scoped
    // definition wins over workbook-scoped). Null if undefined.
    const NameDefinition *find_name(const std::string &name, std::int32_t sheet) const;
};

std::string upper_ascii(const std::string &text);

} // namespace xlpp
