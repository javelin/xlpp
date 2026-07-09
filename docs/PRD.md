# PRD — xl++ : Formula Evaluation Engine for PIXls Design Spreadsheets

**Status:** Draft v1 · **Date:** 2026-07-09 · **Owner:** Doc

## 1. Problem

The `pixls` folder contains 97 `.xlsx` workbooks — Power Integrations PIXls calculation engines for power-supply designs (flyback, buck, boost, PFC, LLC, etc.). Each workbook takes design inputs (line voltage, output power, device selection) and computes a full design via cell formulas. Today the only way to run these calculations is Excel. The goal is a C++ library, built on **xlnt**, that opens these workbooks and evaluates their formulas natively — a calculation engine tailored to exactly the formula subset these files use, not a general Excel clone.

## 2. Goals

- Open any of the 97 workbooks via xlnt, set input cells, recalculate, and read output cells with results matching Excel.
- Support 100% of the formula constructs actually present in the corpus — nothing more.
- Verification oracle: every workbook ships with cached formula results; the engine must reproduce them.

## 3. Non-Goals

- General-purpose Excel compatibility (dynamic arrays, structured references, LAMBDA, etc.).
- Writing modified workbooks back to disk (xlnt can do this later if needed; not phase 1).
- Rendering, charts, formatting, VBA (no workbook in the corpus contains VBA).
- A GUI. This is a library + CLI verification harness.

## 4. Corpus Characterization (evidence base)

One representative was sampled from each similarly-named file group — 14 workbooks spanning generations and sizes:

| File | Formulas | Sheets | Distinct funcs | Notable |
|---|---|---|---|---|
| LinkSwitch-TN_Rev2 | 286 | 3 | 9 | **iterative calc ON** |
| ACDC_LinkSwitchTN2_Buck_Rev1 | 296 | 3 | 15 | iterative calc ON |
| ACDC_CP100X_Rev2 | 553 | 3 | 10 | oldest style |
| ACDC_TOPJX_Flb_Rev1 | 1,060 | 3 | 20 | iterative calc ON |
| DCDC_DPAFwd_Rev2 | 1,325 | 3 | 15 | |
| ACDC_InnoSwitchCP_Rev1 | 2,521 | 6 | 14 | |
| ACDC_TNY-4_Rev1 | 2,667 | 6 | 17 | #REF! defined names, charts |
| ACDC_LYTSwitch5_Buck_Rev2 | 2,909 | 7 | 26 | |
| HiperPFS3_Boost_Rev1 | 13,208 | 11 | 30 | most function variety |
| ACDC_InnoSwitch4CZ_USBPD_Rev3 | 20,555 | 9 | 23 | |
| ACDC_InnoSwitch3CP_Flyback_Rev1 | 21,894 | 8 | 23 | |
| ACDC_LNK-PL_Buck_Rev1 | 29,138 | 7 | 22 | INDIRECT, ADDRESS, iterative |
| ACDC_LYTSwitch-4_Flyback_Rev1 | 121,035 | 6 | 28 | 2,642 INDIRECT cells |
| LYTSwitch-0_Rev1 | 153,580 | 6 | 17 | largest; 5,307 cross-sheet cells |

Key findings:

**Function inventory is small and closed.** Across the sample, exactly **44 distinct functions** appear (string literals stripped to avoid false positives). By frequency: IF (174k), SIN, SQRT, COS, AND, ABS, EXP, PI, CONCATENATE, VLOOKUP, INDIRECT, COSH, MATCH, INDEX, MAX, SINH, MIN, FALSE, MOD, CEILING, OR, INT, ASIN, AVERAGE, LOG, TRUE, SUM, ROUND, ISNA, MID, ROUNDUP, NOT, FLOOR, ADDRESS, TEXT, HLOOKUP, ISERROR, SUMSQ, ATAN, ISNUMBER, SEARCH, FIXED, LEN, VALUE. It is dominated by scalar math/trig and IF-chains; the "hard" Excel surface (array formulas, dynamic arrays, financial/date functions) is entirely absent.

**No array formulas, no VBA, no Excel tables, no pivot tables, no live external references** anywhere in the sample (one legacy `externalLink` part exists in TNY-4 but no cell references it).

**Defined names are heavily used**: 81–437 per workbook, referenced by name inside formulas (e.g. `VLOOKUP(LYTSwitch, LYT_DEVICE_TABLE, 5, FALSE())`). Some names resolve to `#REF!` (broken but present, e.g. 30+ in TNY-4); formulas referencing them must yield `#REF!` errors, which downstream `IF`/`ISNA` logic tolerates.

**INDIRECT is confined and stereotyped.** 2,642 of 2,647 INDIRECT calls are in one file (LYTSwitch-4) and all follow one pattern: `INDIRECT(CONCATENATE("Calcs!AL", Y7))` — a computed row number on a fixed sheet/column. `ADDRESS` (10 uses, LNK-PL only) is similar. Full dynamic-reference generality is not required.

**Iterative calculation is required.** 15 of 97 workbooks (whole-corpus scan) set `iterate="true"` with `iterateCount="100" iterateDelta="0.001"` — they contain intentional circular references solved by fixed-point iteration (LinkSwitch/LNK/TOP families).

**Error values are part of normal operation.** Cached results include thousands of `#N/A` (6,069 in three files alone), plus `#VALUE!`, `#DIV/0!`, `#REF!`. Error propagation and `ISNA`/`ISERROR` semantics must match Excel.

**Scale:** worst case 153,580 formula cells in one workbook (LYTSwitch-0); cross-sheet references up to 5,307 cells. Full recalculation must handle this comfortably.

## 5. Requirements

### 5.1 Functional

- **FR1 — Load:** Open any corpus workbook through xlnt; extract cell values, formula strings, defined names, and calculation settings. Shared formulas must be expanded (xlnt already does this at parse time).
- **FR2 — Parse:** Tokenize and parse the corpus formula grammar into an AST: numbers, strings, booleans, `%` postfix, unary `±`, operators `+ - * / ^ &` and comparisons, A1 references (relative/absolute), ranges, cross-sheet refs (quoted and unquoted sheet names), defined names, function calls with omitted arguments.
- **FR3 — Functions:** Implement the 44 corpus functions with Excel-compatible semantics (short-circuit `IF`; `VLOOKUP`/`HLOOKUP`/`MATCH` approximate- and exact-match modes; Excel's `CEILING`/`FLOOR` significance semantics; `TEXT`/`FIXED` number formatting for the handful of format codes actually used).
- **FR4 — Defined names:** Resolve workbook- and sheet-scoped names to references or constants, including names whose definition is `#REF!` (evaluate to `#REF!` error, not a load failure).
- **FR5 — Dependency engine:** Build the dependency graph, evaluate in topological order, cache results, and support dirty-marking + minimal recalculation when an input changes.
- **FR6 — Iterative calculation:** Detect cycles. If the workbook's `calcPr` has `iterate="true"`, resolve strongly-connected components by fixed-point iteration honoring `iterateCount` and `iterateDelta`; otherwise report a circular-reference diagnostic.
- **FR7 — Dynamic references:** Support `INDIRECT(CONCATENATE(...))` and `ADDRESS`-composed references. Dependencies discovered at evaluation time must feed back into the recalc ordering.
- **FR8 — Error semantics:** First-class error values (`#N/A`, `#VALUE!`, `#DIV/0!`, `#REF!`, `#NAME?`, `#NUM!`) with Excel propagation rules; `ISNA`/`ISERROR`/`ISNUMBER` behave correctly on them.
- **FR9 — API:** Set a cell value (typed: number/string/bool), trigger recalculation, read any cell's computed value; enumerate sheets/cells; report per-cell evaluation diagnostics.
- **FR10 — Verification harness:** CLI tool that loads a workbook, evaluates every formula cell from raw inputs, and diffs against the cached values stored in the file (relative tolerance for numerics, exact for strings/booleans/errors), producing a per-file match report.

### 5.2 Non-Functional

- **NFR1 — Correctness:** ≥ 99.9% cell-match vs cached values per workbook across all 97 files; 100% on the 14-file sample. Mismatches must be individually explainable (e.g. stale cache in file).
- **NFR2 — Performance:** Full recalc of the largest workbook (≈154k formulas) in < 2 s on a desktop; incremental recalc after one input change proportional to the dirty set.
- **NFR3 — Portability:** C++17, CMake, dependencies limited to xlnt and its bundled third-party libs. Same platforms xlnt supports.
- **NFR4 — Robustness:** A formula construct outside the supported subset must produce a per-cell diagnostic (`#NAME?`-style), never a crash or silent wrong number.

## 6. xlnt: Capabilities and Gaps (verified against the source in `xlnt/`)

Provided by xlnt: xlsx load; `cell::formula()` as string with shared-formula expansion and array-formula capture; cached values via `cell::value()` (the verification oracle); `calculation_properties`.

Gaps the project must cover:

1. **No formula evaluator** — the core of this project.
2. **Defined names:** public API exposes only simple `named_range`s; the internal `detail::defined_name` (which carries the raw value string, including constants and `#REF!`) is not public. Mitigation: side-parse `xl/workbook.xml` `<definedNames>` directly (trivial XML), or carry a small xlnt patch.
3. **Iteration settings:** `calculation_properties` only holds `calc_id`/`concurrent_calc`; `iterate`, `iterateCount`, `iterateDelta` are dropped. Mitigation: same side-parse of `xl/workbook.xml` `<calcPr>`.
4. **Parse fidelity risk:** xlnt's reading of these specific files (some 3–4 MB, 150k+ formulas) must be validated early — Phase 0 spike.

## 7. Success Metrics

- Verification harness passes NFR1 thresholds on all 97 workbooks.
- Round-trip demo: change `VACMIN`/`PO`-class inputs on three workbook families, recalc, outputs match Excel to 6 significant digits.
- Library consumable via CMake `add_subdirectory`/`find_package` with a ≤ 20-line usage example.

## 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Cached values stale (file saved without full recalc) | Medium | Tolerance policy + cross-check questionable cells in Excel/LibreOffice; whitelist known-stale cells |
| Floating-point divergence from Excel (transcendentals, `^`) | Medium | Relative tolerance 1e-9 in harness; exactness only where Excel guarantees it |
| Unsampled files contain constructs outside the 44-function inventory | Medium | Phase 1 runs the inventory scan on **all 97 files**, not just the sample, before evaluator work starts |
| xlnt load failures on edge files | Low | Phase 0 loads all 97 files first |
| `TEXT`/`FIXED` format-code semantics | Low | Only implement format codes observed in corpus |

## 9. Open Questions

1. Should the engine expose which cells are "inputs" (the workbooks use data-validation dropdowns and unlocked cells that could identify them automatically)?
2. Is write-back (saving computed workbooks) needed in a later phase?
3. Target binding beyond C++ (Python?) — xlnt already has Python bindings infrastructure.

## Appendix A — Sampling method

Files were grouped by naming family (InnoSwitch gen-1/3/4/5, LNK-PH/PL, LNK legacy, LYTSwitch 0/-2/-4/1/3/5/7, LinkSwitch legacy/TN/TN2/XT2/ZERO/HF, TNY, TOP, Hiper PFS/TFS/LCS/PLC, DCDC/DPA, SC10xx, CP100X, PeakSwitch) and one file chosen per group, biased toward newest revision and non-trivial size. Raw analysis data: `docs/analysis.json`.
