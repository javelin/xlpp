// Unit test for FR6 fixed-point iteration. The corpus' iterate=true files
// currently evaluate without live cycles (their self-referential branches are
// inactive for the saved inputs), so the solver is exercised here with a
// synthetic model:
//
//   A1 = B1 + 1
//   B1 = A1 * 0.5      fixed point: A1 = 2, B1 = 1
//
// With iterate=true the engine must converge to the fixed point within
// iterateDelta; with iterate=false both cells must be cyclic diagnostics.

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

xlpp::WorkbookModel make_cycle_model(bool iterate) {
    xlpp::WorkbookModel model;
    model.sheet_names.push_back("S");
    model.calc.iterate = iterate;
    model.calc.iterate_count = 100;
    model.calc.iterate_delta = 1e-6;

    auto add_formula = [&model](std::uint32_t column, std::uint32_t row,
                                const char *formula) {
        const xlpp::CellKey key = xlpp::make_cell_key(0, column, row);
        xlpp::CellRecord record;
        record.cached = xlpp::Value::make_number(0.0); // seed, as a fresh Excel calc
        record.formula = static_cast<std::int32_t>(model.formulas.size());
        model.formulas.push_back(xlpp::parse_formula(formula));
        model.formula_cells.push_back(key);
        model.cells.emplace(key, std::move(record));
    };
    add_formula(1, 1, "=B1+1");   // A1
    add_formula(2, 1, "=A1*0.5"); // B1
    return model;
}

} // namespace

int main() {
    {
        xlpp::Engine engine(make_cycle_model(/*iterate=*/true));
        engine.recalculate();
        const xlpp::Value a1 = engine.value("S", "A1");
        const xlpp::Value b1 = engine.value("S", "B1");
        expect(engine.stats().converged, "iterate=true converges");
        expect(engine.stats().iterations > 0, "iterations were run");
        expect(a1.kind == xlpp::ValueKind::number && std::fabs(a1.number - 2.0) < 1e-4,
               "A1 reaches fixed point 2.0");
        expect(b1.kind == xlpp::ValueKind::number && std::fabs(b1.number - 1.0) < 1e-4,
               "B1 reaches fixed point 1.0");
    }
    {
        xlpp::Engine engine(make_cycle_model(/*iterate=*/false));
        engine.recalculate();
        expect(engine.stats().cyclic == 2, "iterate=false marks both cells cyclic");
        const xlpp::Value a1 = engine.value("S", "A1");
        expect(a1.kind == xlpp::ValueKind::error && a1.error == xlpp::ErrorCode::cyclic,
               "A1 is a cyclic diagnostic");
    }
    {
        // set_value on an input + recalc still works after iteration paths.
        xlpp::WorkbookModel model = make_cycle_model(/*iterate=*/true);
        xlpp::Engine engine(std::move(model));
        engine.recalculate();
        engine.set_value("S", "A1", xlpp::Value::make_number(7.0)); // detach A1
        engine.recalculate();
        const xlpp::Value b1 = engine.value("S", "B1");
        expect(b1.kind == xlpp::ValueKind::number && std::fabs(b1.number - 3.5) < 1e-9,
               "set_value detaches A1; B1 = A1*0.5 = 3.5 without cycles");
        expect(engine.stats().iterations == 0, "no iteration needed after detach");
    }
    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
