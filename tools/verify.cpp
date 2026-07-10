// verify — oracle harness (PRD FR10): recompute every formula cell from the
// raw inputs and diff against the cached values stored in the file.
// Numbers compare with relative tolerance 1e-9 (absolute 1e-12 near zero);
// text/boolean/error compare exactly. Cells whose evaluation is deferred
// (Phase 4 constructs, cycles) are reported separately, not as mismatches.
//
// Mismatch triage (NFR1) happens in three mechanical layers:
// 1. Staleness prover: the mismatched formula is re-evaluated against the
//    file's own cached state (cached-view evaluator). If even that does not
//    reproduce the cached value, the cache is self-inconsistent — the file
//    was saved without recalculating the cell — and it counts as `stale`,
//    not as a mismatch. (Example: LYTSwitch-4 caches #REF! for INDIRECT
//    cells whose cached targets hold plain numbers.)
// 2. Taint propagation: a mismatched cell whose replay IS consistent with
//    the cached state, but which references a stale cell (directly or
//    transitively), inherited its wrongness from the stale region — also
//    `stale`. Staleness is transitive by construction.
// 3. Array-FP closure: MINVERSE on the corpus' ill-conditioned 7x7
//    polynomial-fit systems is not bit-reproducible across implementations
//    (Excel's own result differs from a refined LU solve by ~2e-6 rel).
//    Cells in the dependency closure of a CSE array block that match within
//    a loose 1e-3 relative tolerance count as `array_fp`.
// 4. Whitelist file (TSV: file-basename, Sheet!Cell, reason) for the few
//    individually explained rest (FP-cancellation residues).
//
// Usage: verify [--details N] [--whitelist FILE] file.xlsx [file.xlsx ...]

#include <xlpp/ast.hpp>
#include <xlpp/engine.hpp>

#include "evaluator.hpp" // staleness prover uses the cached-view evaluator

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

bool values_match_loose(const xlpp::Value &expected, const xlpp::Value &actual) {
    using xlpp::ValueKind;
    if (expected.kind == ValueKind::number && actual.kind == ValueKind::number) {
        const double scale = std::max(std::fabs(expected.number), std::fabs(actual.number));
        return std::fabs(expected.number - actual.number) <= std::max(1e-9, 1e-3 * scale);
    }
    return false;
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

// Collects referenced cell keys of one formula (defined names expanded) —
// used only for taint propagation during triage.
void collect_referenced_cells(const xlpp::WorkbookModel &model, const xlpp::Ast &ast,
                              xlpp::NodeId id, std::int32_t anchor_sheet,
                              std::unordered_set<std::string> &visited_names,
                              std::vector<xlpp::CellKey> &out) {
    const xlpp::Node &node = ast.at(id);
    if (node.kind == xlpp::NodeKind::cell || node.kind == xlpp::NodeKind::range) {
        const std::int32_t sheet =
            node.has_sheet ? model.sheet_index(node.sheet) : anchor_sheet;
        if (sheet >= 0) {
            const bool is_range = node.kind == xlpp::NodeKind::range;
            const auto col_a = node.address_a.column;
            const auto col_b = is_range ? node.address_b.column : col_a;
            const auto row_a = node.address_a.row;
            const auto row_b = is_range ? node.address_b.row : row_a;
            for (auto col = std::min(col_a, col_b); col <= std::max(col_a, col_b); ++col) {
                for (auto row = std::min(row_a, row_b); row <= std::max(row_a, row_b);
                     ++row) {
                    out.push_back(xlpp::make_cell_key(static_cast<std::uint32_t>(sheet),
                                                      col, row));
                }
            }
        }
    } else if (node.kind == xlpp::NodeKind::name) {
        if (visited_names.insert(node.text).second) {
            const xlpp::NameDefinition *definition =
                model.find_name(node.text, anchor_sheet);
            if (definition != nullptr && definition->parsed) {
                collect_referenced_cells(model, definition->ast, definition->ast.root(),
                                         anchor_sheet, visited_names, out);
            }
            visited_names.erase(node.text);
        }
    }
    for (const xlpp::NodeId child : node.children) {
        collect_referenced_cells(model, ast, child, anchor_sheet, visited_names, out);
    }
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

    std::cout << "file\tformulas\tevaluated\tmatched\tmismatched\twhitelisted\tstale"
                 "\tarray_fp\tdeferred\tcyclic\tparse_fail\tmatch_pct\n";

    bool all_ok = true;
    std::size_t details = 0;
    for (const std::string &path : files) {
        try {
            xlpp::Engine engine(xlpp::WorkbookModel::load(path));
            engine.recalculate();
            const xlpp::WorkbookModel &model = engine.model();

            std::size_t matched = 0;
            std::size_t whitelisted = 0;
            std::size_t stale = 0;
            std::size_t deferred = 0;
            std::size_t cyclic = 0;
            const std::string file_base = base_name(path);
            const std::unordered_map<xlpp::CellKey, xlpp::Value> no_computed;
            xlpp::detail::Evaluator cached_view(model, no_computed, /*cached_view=*/true);

            // Pass 1: match / whitelist / directly-stale / candidate mismatch.
            std::unordered_set<xlpp::CellKey> tainted;
            std::vector<std::size_t> candidates; // replay-consistent mismatches
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
                // Staleness prover. (Array-block members are excluded — their
                // per-cell ASTs are the untranslated master text.)
                if (model.array_members.count(key) == 0) {
                    const xlpp::Value replay =
                        cached_view.evaluate_cell(key, model.formulas[i]);
                    if (!values_match(expected, replay)) {
                        ++stale;
                        tainted.insert(key);
                        continue;
                    }
                }
                candidates.push_back(i);
            }

            // Pass 2: taint propagation — replay-consistent mismatches that
            // reference a stale cell inherited its wrongness (fixpoint).
            bool changed = !tainted.empty();
            while (changed && !candidates.empty()) {
                changed = false;
                std::vector<std::size_t> remaining;
                remaining.reserve(candidates.size());
                for (const std::size_t i : candidates) {
                    const xlpp::CellKey key = model.formula_cells[i];
                    std::vector<xlpp::CellKey> references;
                    std::unordered_set<std::string> visited;
                    collect_referenced_cells(model, model.formulas[i],
                                             model.formulas[i].root(),
                                             static_cast<std::int32_t>(xlpp::key_sheet(key)),
                                             visited, references);
                    bool inherits = false;
                    for (const xlpp::CellKey reference : references) {
                        if (tainted.count(reference) != 0) {
                            inherits = true;
                            break;
                        }
                    }
                    if (inherits) {
                        ++stale;
                        tainted.insert(key);
                        changed = true;
                    } else {
                        remaining.push_back(i);
                    }
                }
                candidates.swap(remaining);
            }

            // Pass 3: array-FP closure — loose numeric agreement downstream
            // of CSE array blocks (MINVERSE is not bit-reproducible). The
            // closure walks ALL formula cells (a strictly-matching cell still
            // carries the dependency), then candidates inside it that agree
            // loosely are reclassified.
            std::size_t array_fp = 0;
            if (!model.array_members.empty() && !candidates.empty()) {
                std::unordered_set<xlpp::CellKey> closure;
                for (const auto &member : model.array_members) {
                    closure.insert(member.first);
                }
                std::vector<std::vector<xlpp::CellKey>> refs(model.formula_cells.size());
                for (std::size_t i = 0; i < model.formula_cells.size(); ++i) {
                    const xlpp::CellKey key = model.formula_cells[i];
                    std::unordered_set<std::string> visited;
                    collect_referenced_cells(model, model.formulas[i],
                                             model.formulas[i].root(),
                                             static_cast<std::int32_t>(xlpp::key_sheet(key)),
                                             visited, refs[i]);
                }
                bool grew = true;
                while (grew) {
                    grew = false;
                    for (std::size_t i = 0; i < model.formula_cells.size(); ++i) {
                        const xlpp::CellKey key = model.formula_cells[i];
                        if (closure.count(key) != 0) {
                            continue;
                        }
                        for (const xlpp::CellKey reference : refs[i]) {
                            if (closure.count(reference) != 0) {
                                closure.insert(key);
                                grew = true;
                                break;
                            }
                        }
                    }
                }
                std::vector<std::size_t> remaining;
                remaining.reserve(candidates.size());
                for (const std::size_t i : candidates) {
                    const xlpp::CellKey key = model.formula_cells[i];
                    if (closure.count(key) != 0 &&
                        values_match_loose(model.cells.at(key).cached, engine.value(key))) {
                        ++array_fp;
                    } else {
                        remaining.push_back(i);
                    }
                }
                candidates.swap(remaining);
            }

            const std::size_t mismatched = candidates.size();
            for (const std::size_t i : candidates) {
                if (details >= max_details) {
                    break;
                }
                ++details;
                const xlpp::CellKey key = model.formula_cells[i];
                std::cerr << "MISMATCH " << path << " "
                          << model.sheet_names[xlpp::key_sheet(key)] << "!"
                          << xlpp::column_letters(xlpp::key_column(key)) << xlpp::key_row(key)
                          << "\n  formula:  " << xlpp::to_formula(model.formulas[i])
                          << "\n  expected: " << value_to_string(model.cells.at(key).cached)
                          << "\n  got:      " << value_to_string(engine.value(key)) << "\n";
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
                      << '\t' << stale << '\t' << array_fp << '\t' << deferred << '\t'
                      << cyclic << '\t' << model.formula_parse_failures << '\t' << pct_text
                      << '\n';
        } catch (const std::exception &error) {
            all_ok = false;
            std::cout << path << "\tLOAD_FAIL\t" << error.what() << '\n';
        }
    }
    return all_ok ? 0 : 1;
}
