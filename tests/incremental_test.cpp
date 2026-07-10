// Unit test for FR5 incremental recalculation: after set_value, only the
// dirty closure (statically referencing formulas, volatile cells, and their
// transitive dependents) is re-evaluated.

#include <xlpp/engine.hpp>
#include <xlpp/parser.hpp>
#include <xlpp/workbook_model.hpp>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("ok:   %s\n", what);
    }
}

xlpp::WorkbookModel make_model() {
    xlpp::WorkbookModel model;
    model.sheet_names.push_back("S");

    auto add_input = [&model](std::uint32_t column, std::uint32_t row, double v) {
        xlpp::CellRecord record;
        record.cached = xlpp::Value::make_number(v);
        model.cells.emplace(xlpp::make_cell_key(0, column, row), std::move(record));
    };
    auto add_formula = [&model](std::uint32_t column, std::uint32_t row,
                                const char *formula) {
        const xlpp::CellKey key = xlpp::make_cell_key(0, column, row);
        xlpp::CellRecord record;
        record.formula = static_cast<std::int32_t>(model.formulas.size());
        model.formulas.push_back(xlpp::parse_formula(formula));
        model.formula_cells.push_back(key);
        model.cells.emplace(key, std::move(record));
    };
    add_input(1, 1, 2.0);        // A1 = 2
    add_formula(2, 1, "=A1*3");  // B1
    add_formula(3, 1, "=B1+1");  // C1
    add_input(1, 2, 10.0);       // A2 = 10 (independent branch)
    add_formula(2, 2, "=A2*2");  // B2
    return model;
}

} // namespace

int main() {
    xlpp::Engine engine(make_model());
    engine.recalculate();
    expect(engine.stats().recalculated == 3, "first recalc evaluates all 3 formulas");
    expect(engine.value("S", "C1").number == 7.0, "C1 = A1*3+1 = 7");
    expect(engine.value("S", "B2").number == 20.0, "B2 = A2*2 = 20");

    engine.set_value("S", "A1", xlpp::Value::make_number(5.0));
    engine.recalculate();
    expect(engine.value("S", "C1").number == 16.0, "C1 updates to 16 after A1=5");
    expect(engine.value("S", "B2").number == 20.0, "B2 unchanged");
    expect(engine.stats().recalculated == 2,
           "incremental recalc evaluates only B1+C1 (dirty closure), not B2");

    engine.recalculate();
    expect(engine.stats().recalculated == 0, "no changes => nothing re-evaluated");

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
