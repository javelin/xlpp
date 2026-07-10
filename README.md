# xl++ (xlpp)

A C++17 formula-evaluation engine for the Power Integrations PIXls design
spreadsheets — built on [xlnt](https://github.com/javelin/xlnt), tailored to
exactly the formula subset the 97-workbook corpus uses (54 functions), not a
general Excel clone. See `docs/PRD.md` and `docs/IMPLEMENTATION_PLAN.md`.

## Usage

```cpp
#include <xlpp/engine.hpp>

int main() {
    xlpp::Engine engine(xlpp::WorkbookModel::load("ACDC_TNY-4_Rev1.xlsx"));
    engine.recalculate();                                        // full calc

    engine.set_value("TinySwitch-4", "B7", xlpp::Value::make_number(15.0));
    engine.recalculate();                                        // incremental

    const xlpp::Value out = engine.value("TinySwitch-4", "E19");
    if (out.kind == xlpp::ValueKind::number) {
        std::printf("ILIMITMIN = %g\n", out.number);
    }
}
```

## Building

```sh
git submodule update --init --recursive   # xlnt + its nested submodules
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                    # unit tests
XLPP_CORPUS=../pixls ./tools/corpus_gate.sh build/verify   # oracle gate
```

## Verification

Every corpus workbook carries Excel's cached formula results; the `verify`
harness recomputes all ~1.74M formulas and diffs against them. Gate status:
**97/97 workbooks, 1,437,117 evaluated cells, zero unexplained mismatches.**
Classified exceptions: 952 whitelisted FP residues (each with a written
justification and measured bound, `tools/verify_whitelist.tsv`), ~306k cells
in provably stale cached regions (the file's own cached inputs contradict its
cached outputs), and 25 cells downstream of non-bit-reproducible MINVERSE.

## Performance (measured, Release, one core)

| Workbook | Formulas | Load | Full recalc | Incremental (1 input) |
|---|---|---|---|---|
| LYTSwitch-0_Rev1 | 153,580 | 0.63 s | 1.07 s | proportional to dirty set |
| InnoSwitch3CP | 21,894 | 0.27 s | 36 ms | 0.5 ms (2 cells) |

## Architecture notes

Demand-driven evaluation with an explicit work stack: the static dependency
graph is only a scheduling hint (device tables legally overlap computed
columns, so static cycle detection would reject Excel-legal files); a cell is
cyclic only if read during its own evaluation. `iterate=true` books resolve
such cycles by fixed-point iteration honoring iterateCount/iterateDelta.
Shared formulas are re-anchored per member via a sheet-XML side-parse (xlnt
hands members the master's text verbatim); defined names and calcPr come from
the same side-parse. Legacy CSE array blocks (`{=MMULT(MINVERSE(A),b)}`)
evaluate as whole matrices via LU with iterative refinement.
