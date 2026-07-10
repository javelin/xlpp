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

- Build xlnt; write a 100-line prober that loads **all 97 workbooks**, dumps per-cell formula strings + cached values to a normalized text format.
- Cross-check the dump against the Python/openpyxl analysis already done (formula counts per sheet must match; see `docs/analysis.json`).
- Implement the workbook.xml side-parse (definedNames + calcPr) since xlnt drops both.
- **Exit:** all 97 files load; formula/value extraction verified on the 14-file sample; xlnt patch-vs-sideparse decision made.

## Phase 1 — Full-corpus inventory & grammar freeze (≈1 week)

- Run function/construct inventory on all 97 files (the PRD's 44-function list came from the 14-file sample; the remaining 83 files may add a handful).
- Freeze the grammar: literals, `% ^ * / + - &` and comparisons, unary ops, A1 refs, ranges, sheet-qualified refs (incl. quoted names like `'Calcs2 (powder sim)'!A1`), defined names, function calls, omitted args.
- Write the lexer + recursive-descent parser + AST; parse **every formula in the corpus** (≈600k cells) with zero failures.
- **Exit criterion (hard gate):** 100% parse rate corpus-wide; round-trip pretty-printer output re-parses to identical AST.

> **Phase 2+3 were merged** (approved 2026-07-09): the Phase 2 exit gate requires
> defined names and lookups, which were slated for Phase 3. Additional findings
> during execution, now part of the architecture:
>
> - **Evaluation is demand-driven**, not static-topo: static range dependencies
>   over-approximate (device tables overlap computed columns, e.g. TNY-4's
>   `TinySwitch_4_Table` includes calc cells), which would flag Excel-legal
>   workbooks as circular. The engine keeps the static graph only as a Kahn
>   scheduling hint; evaluation reads cells on demand via an explicit work
>   stack (`PendingCell` retry, no deep recursion), and only a cell read
>   *while it is itself being evaluated* is a true cycle — matching Excel.
> - **Shared formulas require translation** (PRD §6 gap 5): implemented as a
>   sheet-XML side-parse producing per-member translated formulas.
> - **Reference-vs-literal argument semantics**: aggregates and AND/OR skip
>   text/booleans behind references but coerce literal arguments.
> - **Oracle whitelist** (planned for Phase 6) pulled forward: 526 cells with
>   individually explained cached-value defects (474 provably stale `#VALUE!`
>   region in HiperPFS3; 51 FP-cancellation residues in InnoSwitch3CP KP1
>   Newton intermediates; 1 stale cell in DPAFwd) — `tools/verify_whitelist.tsv`.

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

## Phase 4 — Dynamic references & special constructs (≈1 week)

- INDIRECT + ADDRESS with dynamic-dependency feedback loop; OFFSET (single corpus use, range-shift feeding VLOOKUP).
- CSE array blocks: the six `{=MMULT(MINVERSE(7×7), 7×1)}` blocks in ACDC_LYTSwitch1_BuckBoost_Rev1 (Phase 1 corpus-scan finding) — fixed-size matrix solve spilled over the anchor block; no general array machinery.
- **Exit:** ≥ 99.9% match on ACDC_LYTSwitch-4_Flyback_Rev1 (121k formulas, 2.6k INDIRECT), LYTSwitch-0_Rev1 (154k formulas), and ACDC_LYTSwitch1_BuckBoost_Rev1. Perf: full recalc < 2 s each.

> **Phase 4 execution notes (2026-07-10):** the demand-driven engine made the
> dynamic-dependency feedback loop unnecessary — INDIRECT evaluates its text,
> parses it with the xlpp parser, and reads the target; PendingCell retry
> covers discovered dependencies. Array blocks are recovered from the sheet
> XML (`<f t="array" ref>`), evaluated once per block as a matrix expression
> (LU solve with iterative refinement for `MMULT(MINVERSE(A),b)`), and spilled
> to member cells. Oracle finding: LYTSwitch-4's entire INDIRECT region
> (52.9k cells) is stale cache — the file stores `#REF!` for INDIRECT cells
> whose cached targets hold plain numbers. The harness therefore gained two
> mechanical triage layers: a **staleness prover** (re-evaluate each mismatch
> against the file's own cached state; self-inconsistent ⇒ stale) with
> **transitive taint propagation**, and an **array-FP closure** (MINVERSE is
> not bit-reproducible; cells downstream of array blocks match at 1e-3).
> Whitelist regenerated: 357 entries, all FP-divergence with measured bounds.

## Phase 5 — Iterative calculation (≈1 week)

- Tarjan SCC condensation; fixed-point solver honoring iterateCount/iterateDelta; deterministic in-SCC ordering (row-major by sheet order) to mirror Excel.
- **Exit:** ≥ 99.9% match on all 15 `iterate="true"` workbooks.

> **Phase 5 execution notes (2026-07-10):** the demand-driven engine replaced
> SCC condensation — cycles are cut where a cell is read during its own
> evaluation; with `iterate=true` the reader receives the previous pass's
> value (first pass: the file's cached value, mirroring Excel's last-known
> values) and whole-book passes repeat until max |Δ| < iterateDelta or
> iterateCount. Corpus reality: all 15 iterate files evaluate with **zero
> live cycles** for their saved inputs (the self-referential branches are
> inactive) and pass at 100%, so the solver is proven by unit test
> (`tests/iterative_test.cpp`). The LNK-PL Buck/TappedBuck "stale" regions
> (~21.9k cells each) were root-caused: a literal `=#REF!` formula cell
> poisons today's state-machine chain while the caches predate it (#NAME?
> era) — genuine staleness, not fixed-point drift.

## Phase 6 — Hardening & release (≈2 weeks)

- Full-corpus verification gate in CI (all 97 files); mismatch whitelist with per-cell justification.
- Public API polish, incremental recalc (dirty propagation) + benchmarks, diagnostics for unsupported constructs, docs + examples.
- Answer PRD open questions (input-cell discovery, write-back, Python binding) as scoped follow-ups.
- **Exit:** NFR1/NFR2 met; tagged v0.1.

> **Phase 6 execution notes (2026-07-10):** triage of the 746 residual
> mismatches found a real engine-fidelity bug class, not just FP noise:
> 1-ulp key drift crossing **lookup-table boundaries** (HiperPLC810's
> `LM/Ll = 3.9999999999999996` vs a "4" table row selected a different row
> and diverged 30%+). Fixed with epsilon boundary snapping (1e-9 relative)
> in VLOOKUP/HLOOKUP/MATCH numeric candidate comparison — this alone took
> HiperPLC810 and five LYTSwitch3/5 files to 100% with no whitelisting. The
> remaining residuals were the InnoSwitch3-family KP1 cancellation class
> (592 cells ≤ 1.5e-7 absolute), 3 CCM/DCM mode-label boundary flips, and
> 1 HiperLCS cancellation cell — whitelisted with measured bounds (952
> entries total). Incremental recalc (FR5) uses the persisted static graph:
> dirty = formulas whose reference rects cover a changed input ∪ volatile
> (INDIRECT/OFFSET) cells, closed over dependents; measured 0.5 ms / 2 cells
> on a 21.9k-formula book. Final gate: `tools/corpus_gate.sh` — 97/97 files,
> 1,437,117 evaluated cells, 0 unexplained mismatches; NFR2: 154k-formula
> book full recalc 1.07 s.

## Testing strategy

Three layers, cheapest first: (1) unit tests per function against Excel-verified fixtures, including error-input matrices; (2) parser corpus test — parse all ~600k formulas, snapshot ASTs for a sample; (3) the oracle harness — the cached values in the 97 files are a free, exhaustive regression suite; run the 14-file sample on every commit and the full corpus nightly/CI-gate.

## Effort summary

≈ 10 weeks single-engineer, front-loaded with the two riskiest gates: xlnt parse fidelity (Phase 0) and 100% corpus parse rate (Phase 1). Everything after Phase 1 works against a frozen, corpus-proven grammar, which is what keeps this "tailored engine" small instead of becoming a general Excel clone.
