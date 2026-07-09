#pragma once

// Sidecar loader: parses xl/workbook.xml directly for the data xlnt's reader
// drops — defined-name definitions (including constants and #REF! names, with
// scope) and calcPr iterative-calculation settings (PRD section 6, gaps 2-3).

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace xlpp {

struct DefinedName {
    std::string name;
    // Index into WorkbookSidecar::sheet_names (workbook.xml <sheets> order).
    // Empty means workbook-scoped.
    std::optional<std::size_t> local_sheet_id;
    bool hidden = false;
    // Raw refers-to text, e.g. "'Lists Data'!$A$1:$G$16", "0.001", "#REF!".
    std::string value;
};

struct CalcSettings {
    // Excel defaults; corpus files with iterate=true always specify all three.
    bool iterate = false;
    std::size_t iterate_count = 100;
    double iterate_delta = 0.001;
};

struct WorkbookSidecar {
    std::vector<std::string> sheet_names; // in workbook.xml order (resolves local_sheet_id)
    std::vector<DefinedName> defined_names;
    CalcSettings calc;
};

// Reads xl/workbook.xml from the given .xlsx. Throws std::runtime_error with
// file/element context on zip or parse failure.
WorkbookSidecar load_workbook_sidecar(const std::string &xlsx_path);

} // namespace xlpp
