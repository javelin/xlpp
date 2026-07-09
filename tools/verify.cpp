// verify — oracle harness (PRD FR10): recompute every formula cell from the
// raw inputs and diff against the cached values stored in the file.
// Numbers compare with relative tolerance 1e-9 (absolute 1e-12 near zero);
// text/boolean/error compare exactly. Cells whose evaluation is deferred
// (Phase 4 constructs, cycles) are reported separately, not as mismatches.
//
// A whitelist file (TSV: file-basename, Sheet!Cell, reason) marks cells whose
// cached value is individually explained (stale caches, FP-cancellation
// residues); they are counted in their own column, not as mismatches (NFR1).
//
// Usage: verify [--details N] [--whitelist FILE] file.xlsx [file.xlsx ...]

#include <xlpp/ast.hpp>
#include <xlpp/engine.hpp>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

namespace {

bool values_match(const xlpp::Value &expected, const xlpp::Value &actual) {
    using xlpp::ValueKind;
    if (expected.kind == ValueKind::number && actual.kind == ValueKind::number) {
        const double a = expected.number;
        const double b = actual.number;
        if (a == b) {
            return true;
        }
        const double scale = std::max(std::fabs(a), std::fabs(b));
        return std::fabs(a - b) <= std::max(1e-12, 1e-9 * scale);
    }
    if (expected.kind != actual.kind) {
        // Excel caches a blank-producing formula as 0 and vice versa.
        if (expected.kind == ValueKind::blank && actual.kind == ValueKind::number) {
            return actual.number == 0.0;
        }
        if (expected.kind == ValueKind::text && actual.kind == ValueKind::blank) {
            return expected.text.empty();
        }
        // Excel writes no cached <v> for a formula whose result is "".
        if (expected.kind == ValueKind::blank && actual.kind == ValueKind::text) {
            return actual.text.empty();
        }
        return false;
    }
    switch (expected.kind) {
    case ValueKind::blank: return true;
    case ValueKind::text: return expected.text == actual.text;
    case ValueKind::boolean: return expected.boolean == actual.boolean;
    case ValueKind::error: return expected.error == actual.error;
    default: return false;
    }
}

std::string base_name(const std::string &path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// keys: "<file-basename>|<Sheet>!<Cell>"
std::unordered_set<std::string> load_whitelist(const std::string &path) {
    std::unordered_set<std::string> entries;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "ERROR: cannot open whitelist: " << path << "\n";
        std::exit(2);
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto tab1 = line.find('\t');
        const auto tab2 = line.find('\t', tab1 + 1);
        if (tab1 == std::string::npos || tab2 == std::string::npos) {
            continue;
        }
        entries.insert(line.substr(0, tab1) + "|" + line.substr(tab1 + 1, tab2 - tab1 - 1));
    }
    return entries;
}

std::string value_to_string(const xlpp::Value &value) {
    switch (value.kind) {
    case xlpp::ValueKind::blank: return "<blank>";
    case xlpp::ValueKind::number: return xlpp::number_to_text(value.number);
    case xlpp::ValueKind::text: return "\"" + value.text + "\"";
    case xlpp::ValueKind::boolean: return value.boolean ? "TRUE" : "FALSE";
    case xlpp::ValueKind::error: return xlpp::error_text(value.error);
    }
    return "?";
}

} // namespace

int main(int argc, char **argv) {
    std::size_t max_details = 10;
    std::vector<std::string> files;
    std::unordered_set<std::string> whitelist;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--details" && i + 1 < argc) {
            max_details = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--whitelist" && i + 1 < argc) {
            whitelist = load_whitelist(argv[++i]);
        } else {
            files.push_back(arg);
        }
    }
    if (files.empty()) {
        std::cerr << "usage: verify [--details N] file.xlsx [file.xlsx ...]\n";
        return 2;
    }

    std::cout << "file\tformulas\tevaluated\tmatched\tmismatched\twhitelisted\tdeferred"
                 "\tcyclic\tparse_fail\tmatch_pct\n";

    bool all_ok = true;
    std::size_t details = 0;
    for (const std::string &path : files) {
        try {
            xlpp::Engine engine(xlpp::WorkbookModel::load(path));
            engine.recalculate();
            const xlpp::WorkbookModel &model = engine.model();

            std::size_t matched = 0;
            std::size_t mismatched = 0;
            std::size_t whitelisted = 0;
            std::size_t deferred = 0;
            std::size_t cyclic = 0;
            const std::string file_base = base_name(path);
            for (std::size_t i = 0; i < model.formula_cells.size(); ++i) {
                const xlpp::CellKey key = model.formula_cells[i];
                const xlpp::Value actual = engine.value(key);
                if (actual.is_diagnostic()) {
                    ++(actual.error == xlpp::ErrorCode::cyclic ? cyclic : deferred);
                    continue;
                }
                const xlpp::Value &expected = model.cells.at(key).cached;
                if (values_match(expected, actual)) {
                    ++matched;
                    continue;
                }
                const std::string cell_id = model.sheet_names[xlpp::key_sheet(key)] + "!" +
                                            xlpp::column_letters(xlpp::key_column(key)) +
                                            std::to_string(xlpp::key_row(key));
                if (whitelist.count(file_base + "|" + cell_id) != 0) {
                    ++whitelisted;
                    continue;
                }
                ++mismatched;
                if (details < max_details) {
                    ++details;
                    std::cerr << "MISMATCH " << path << " "
                              << model.sheet_names[xlpp::key_sheet(key)] << "!"
                              << xlpp::column_letters(xlpp::key_column(key))
                              << xlpp::key_row(key)
                              << "\n  formula:  " << xlpp::to_formula(model.formulas[i])
                              << "\n  expected: " << value_to_string(expected)
                              << "\n  got:      " << value_to_string(actual) << "\n";
                }
            }
            const std::size_t evaluated = matched + mismatched;
            const double pct = evaluated == 0
                ? 100.0
                : 100.0 * static_cast<double>(matched) / static_cast<double>(evaluated);
            if (mismatched > 0) {
                all_ok = false;
            }
            char pct_text[16];
            std::snprintf(pct_text, sizeof(pct_text), "%.4f", pct);
            std::cout << path << '\t' << model.formula_cells.size() << '\t' << evaluated
                      << '\t' << matched << '\t' << mismatched << '\t' << whitelisted
                      << '\t' << deferred << '\t' << cyclic << '\t'
                      << model.formula_parse_failures << '\t' << pct_text << '\n';
        } catch (const std::exception &error) {
            all_ok = false;
            std::cout << path << "\tLOAD_FAIL\t" << error.what() << '\n';
        }
    }
    return all_ok ? 0 : 1;
}
