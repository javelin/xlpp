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

// sheet_names must be workbook.xml order (the sidecar's). Throws
// std::runtime_error on structurally broken files.
SharedFormulaPatches load_shared_formula_patches(const std::string &xlsx_path,
                                                 const std::vector<std::string> &sheet_names);

} // namespace detail
} // namespace xlpp
