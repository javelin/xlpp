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

    // Overrides an input cell (detaches the formula if the cell had one).
    // Returns false if sheet or reference is invalid.
    bool set_value(const std::string &sheet, const std::string &ref, const Value &value);

    // Full recalculation of every formula cell in dependency order.
    void recalculate();

    // Computed value if the cell is a formula cell (after recalculate()),
    // otherwise the input/cached value; blank if the cell does not exist.
    Value value(const std::string &sheet, const std::string &ref) const;
    Value value(CellKey key) const;

    struct Stats {
        std::size_t evaluated = 0;   // formula cells computed this recalc
        std::size_t unsupported = 0; // deferred constructs (Phase 4)
        std::size_t cyclic = 0;      // cells in reference cycles (Phase 5)
    };

    const Stats &stats() const {
        return stats_;
    }

    const WorkbookModel &model() const {
        return model_;
    }

private:
    bool resolve(const std::string &sheet, const std::string &ref, CellKey &key) const;

    WorkbookModel model_;
    std::unordered_map<CellKey, Value> computed_;
    Stats stats_;
};

} // namespace xlpp
