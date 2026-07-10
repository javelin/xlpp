#pragma once

// Calculation engine (PRD FR5, FR9): builds the dependency graph over formula
// cells, evaluates in topological order, and exposes set-input/recalc/read.
// Cycles and Phase 4/5 constructs produce per-cell diagnostics, never crashes.

#include <xlpp/value.hpp>
#include <xlpp/workbook_model.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>

namespace xlpp {

class Engine {
public:
    explicit Engine(WorkbookModel model);

    // Overrides an input cell (detaches the formula if the cell had one) and
    // marks its dependents dirty. Returns false if sheet or ref is invalid.
    bool set_value(const std::string &sheet, const std::string &ref, const Value &value);

    // Recalculation. The first call (or any call after structural changes)
    // computes every formula cell. Subsequent calls after set_value are
    // incremental (FR5): only the dirty closure — cells whose static
    // references cover a changed input, volatile cells (INDIRECT/OFFSET),
    // and their transitive dependents — is re-evaluated.
    void recalculate();

    // Computed value if the cell is a formula cell (after recalculate()),
    // otherwise the input/cached value; blank if the cell does not exist.
    Value value(const std::string &sheet, const std::string &ref) const;
    Value value(CellKey key) const;

    struct Stats {
        std::size_t evaluated = 0;    // formula cells with a computed value
        std::size_t unsupported = 0;  // constructs outside the frozen subset
        std::size_t cyclic = 0;       // cells in cycles (iterate=false files)
        std::size_t iterations = 0;   // fixed-point passes run (iterate=true)
        std::size_t recalculated = 0; // cells evaluated by the last recalculate()
        bool converged = true;        // max |delta| < iterate_delta reached
    };

    const Stats &stats() const {
        return stats_;
    }

    const WorkbookModel &model() const {
        return model_;
    }

private:
    struct RefRect {
        std::int32_t sheet = -1;
        std::uint32_t col_first = 0, row_first = 0;
        std::uint32_t col_last = 0, row_last = 0;
    };

    bool resolve(const std::string &sheet, const std::string &ref, CellKey &key) const;
    void build_graph();

    WorkbookModel model_;
    std::unordered_map<CellKey, Value> computed_;
    Stats stats_;

    // Static scheduling graph, persisted for incremental recalc.
    bool graph_built_ = false;
    bool cycles_seen_ = false; // iterate books fall back to full recalc
    std::vector<std::uint32_t> order_;
    std::vector<std::vector<std::uint32_t>> dependents_;
    std::vector<std::vector<RefRect>> reference_rects_; // per formula
    std::vector<bool> volatile_;                        // INDIRECT/OFFSET present
    std::vector<CellKey> changed_inputs_;               // since last recalc
};

} // namespace xlpp
