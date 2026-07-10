#pragma once

// Internal shared-formula recovery. xlnt hands every member of a shared
// formula group the MASTER's formula text without re-anchoring relative
// references (<f t="shared" si="N"/> members carry no text of their own).
// This loader scans the sheet XML directly, and for each member produces the
// correctly translated formula (master shifted by the member's row/column
// offset), using the xlpp parser + printer for the translation.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlpp {
namespace detail {

// key: (sheet_index << 48) | (column << 28) | row — same packing as CellKey.
using SharedFormulaPatches = std::unordered_map<std::uint64_t, std::string>;

// One legacy CSE array block: <f t="array" ref="AG42:AG48">MMULT(...)</f> on
// the anchor cell; every cell of ref receives one element of the result.
struct ArrayBlockInfo {
    std::uint32_t sheet = 0;
    std::uint32_t row_first = 0, col_first = 0;
    std::uint32_t row_last = 0, col_last = 0;
    std::string formula;
};

struct SheetFormulaScan {
    SharedFormulaPatches shared;      // member cell -> translated formula
    std::vector<ArrayBlockInfo> arrays;
};

// sheet_names must be workbook.xml order (the sidecar's). Throws
// std::runtime_error on structurally broken files.
SheetFormulaScan scan_sheet_formulas(const std::string &xlsx_path,
                                     const std::vector<std::string> &sheet_names);

} // namespace detail
} // namespace xlpp
