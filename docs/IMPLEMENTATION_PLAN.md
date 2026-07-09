# Implementation Plan — xl++ Formula Engine

Companion to `PRD.md`. Phases are gated: each ends with a measurable exit criterion against the real corpus.

## Architecture

```
                 ┌────────────────────────────────────────────┐
 .xlsx ──xlnt──▶ │ WorkbookModel                              │
        side-    │  cells (value, formula string, type)       │
        parse ──▶│  defined names (incl. constants, #REF!)    │
        (calcPr, │  calc settings (iterate, count, delta)     │
        names)   └───────────────┬────────────────────────────┘
                                 │ parse each formula once
                        ┌────────▼────────┐
                        │ Parser → AST    │  (arena-allocated, interned refs)
                        └────────┬────────┘
                                 │ static dependency extraction
                        ┌────────▼────────┐
                        │ DependencyGraph │  topo order; SCC detection (Tarjan)
                        └────────┬────────┘
                                 │
                        ┌────────▼────────┐     ┌──────────────────┐
                        │ Evaluator       │────▶│ Function library │ (44 fns)
                        │  Value = number │     └──────────────────┘
                        │  |string|bool   │
                        │  |error|blank   │  acyclic: one topo pass
                        │  |range         │  SCCs: fixed-point iteration
                        └────────┬────────┘  INDIRECT: dynamic dep feedback
                                 │
                        ┌────────▼────────┐
                        │ Engine API      │ set_value / recalculate / value
                        └─────────────────┘
```

Design decisions locked by the corpus analysis:

- **`Value` is a tagged scalar** (double, string, bool, error, blank) plus a non-owning range handle for lookup functions. No array/spill machinery — the corpus has zero array formulas.
- **Lazy argument evaluation for `IF`/`AND`/`OR` branches** — `IF` appears 174k times, frequently guarding `#DIV/0!`; eager evaluation would be both wrong-ish and slow.
- **Ranges evaluate lazily** — `VLOOKUP(x, LYT_DEVICE_TABLE, …)` should not materialize the table per call; lookups walk the underlying cells.
- **INDIRECT handling:** evaluate the text argument, parse it as a reference at runtime, and record the discovered edge. Because the corpus pattern is `INDIRECT(CONCATENATE("Sheet!COL", computed_row))`, deps of the INDIRECT cell = deps of the row expression + the discovered cell; a second evaluation pass over INDIRECT-dirty cells converges in one round. Guard with a re-eval loop capped at N passes.
- **Iterative mode:** when `iterate=true`, evaluate SCCs in topo order of the condensation graph; within an SCC, iterate all member cells up to `iterateCount` (100) times or until max |Δ| < `iterateDelta` (0.001), matching Excel's semantics of using stale values on first pass.

Repo layout (new repo or subfolder next to `xlnt/`):

```
xlpp/
  CMakeLists.txt          # FetchContent or submodule for xlnt
  include/xlpp/           # engine.hpp, value.hpp, diagnostics.hpp
  src/                    # loader.cpp, sidecar_xml.cpp, lexer.cpp, parser.cpp,
                          # deps.cpp, eval.cpp, functions/*.cpp
  tools/xlpp-verify/      # verification harness CLI
  tests/                  # unit + corpus tests
  corpus/ -> ../pixls     # not committed; path via env/CMake option
```

## Phase 0 — Feasibility spike (≈1 week)

- Build xlnt; write a 100-line prober that loads **all 99 workbooks**, dumps per-cell formula strings + cached values to a normalized text format.
- Cross-check the dump against the Python/openpyxl analysis already done (formula counts per sheet must match; see `docs/analysis.json`).
- Implement the workbook.xml side-parse (definedNames + calcPr) since xlnt drops both.
- **Exit:** all 99 files load; formula/value extraction verified on the 14-file sample; xlnt patch-vs-sideparse decision made.

## Phase 1 — Full-corpus inventory & grammar freeze (≈1 week)

- Run function/construct inventory on all 99 files (the PRD's 44-function list came from the 14-file sample; the remaining 85 files may add a handful).
- Freeze the grammar: literals, `% ^ * / + - &` and comparisons, unary ops, A1 refs, ranges, sheet-qualified refs (incl. quoted names like `'Calcs2 (powder sim)'!A1`), defined names, function calls, omitted args.
- Write the lexer + recursive-descent parser + AST; parse **every formula in the corpus** (≈600k cells) with zero failures.
- **Exit criterion (hard gate):** 100% parse rate corpus-wide; round-trip pretty-printer output re-parses to identical AST.

## Phase 2 — Evaluator core, acyclic engine (≈2 weeks)

- Value model, error propagation, implicit conversions (Excel coercion rules: "" vs blank vs 0, bool→number, text→number in arithmetic).
- Dependency extraction, topo sort, cycle detection (reject for now).
- Scalar function set: math/trig (SIN, COS, SQRT, EXP, PI, ABS, LOG, ASIN, ATAN, SINH, COSH, MOD, INT, ROUND/UP, CEILING, FLOOR, SUMSQ), logic (IF, AND, OR, NOT, TRUE, FALSE), aggregates over ranges (SUM, MAX, MIN, AVERAGE), CONCATENATE and `&`.
- Verification harness v1: evaluate + diff vs cached values.
- **Exit:** 100% match on LinkSwitch-TN_Rev2*, ACDC_CP100X_Rev2, DCDC_DPAFwd_Rev2 (*acyclic subset — full file needs Phase 5).

## Phase 3 — Lookups, defined names, cross-sheet (≈2 weeks)

- Defined-name resolution (workbook/sheet scope, constants, `#REF!` names → `#REF!` value).
- VLOOKUP/HLOOKUP (exact + approximate), MATCH (all match types), INDEX, ISNA/ISERROR/ISNUMBER.
- Text functions used by lookups/labels: MID, LEN, SEARCH, TEXT, FIXED, VALUE (only corpus-observed format codes).
- **Exit:** ≥ 99.9% match on InnoSwitchCP, TNY-4, LYTSwitch5_Buck, InnoSwitch3CP, InnoSwitch4CZ, HiperPFS3. Every mismatch triaged.

## Phase 4 — Dynamic references (≈1 week)

- INDIRECT + ADDRESS with dynamic-dependency feedback loop.
- **Exit:** ≥ 99.9% match on ACDC_LYTSwitch-4_Flyback_Rev1 (121k formulas, 2.6k INDIRECT) and LYTSwitch-0_Rev1 (154k formulas). Perf: full recalc < 2 s each.

## Phase 5 — Iterative calculation (≈1 week)

- Tarjan SCC condensation; fixed-point solver honoring iterateCount/iterateDelta; deterministic in-SCC ordering (row-major by sheet order) to mirror Excel.
- **Exit:** ≥ 99.9% match on all 15 `iterate="true"` workbooks.

## Phase 6 — Hardening & release (≈2 weeks)

- Full-corpus verification gate in CI (all 99 files); mismatch whitelist with per-cell justification.
- Public API polish, incremental recalc (dirty propagation) + benchmarks, diagnostics for unsupported constructs, docs + examples.
- Answer PRD open questions (input-cell discovery, write-back, Python binding) as scoped follow-ups.
- **Exit:** NFR1/NFR2 met; tagged v0.1.

## Testing strategy

Three layers, cheapest first: (1) unit tests per function against Excel-verified fixtures, including error-input matrices; (2) parser corpus test — parse all ~600k formulas, snapshot ASTs for a sample; (3) the oracle harness — the cached values in the 99 files are a free, exhaustive regression suite; run the 14-file sample on every commit and the full corpus nightly/CI-gate.

## Effort summary

≈ 10 weeks single-engineer, front-loaded with the two riskiest gates: xlnt parse fidelity (Phase 0) and 100% corpus parse rate (Phase 1). Everything after Phase 1 works against a frozen, corpus-proven grammar, which is what keeps this "tailored engine" small instead of becoming a general Excel clone.
