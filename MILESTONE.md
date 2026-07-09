# Milestones — xl++ Formula Engine

Working memory for the development cycle: what shipped, what is in flight, what is next.
Updated by the agent with every shipped change. Phase details and exit criteria:
`docs/IMPLEMENTATION_PLAN.md`.

**Current item:** Phase 1 — shipped, awaiting PR merge.

| ID | Milestone | Exit criterion (gate) | Status | Shipped |
|----|-----------|-----------------------|--------|---------|
| M-B | Repo bootstrap: xlnt submodule, PRD + plan docs, ship workflow | Repo builds the docs baseline; submodule resolvable | **Done** | Issue #1 / PR #2 |
| M0 | Feasibility spike: prober + workbook.xml sidecar (definedNames, calcPr) | All 97 corpus files load via xlnt; extraction verified vs openpyxl on 14-file sample; patch-vs-sideparse decision | **Done** — 97/97 load; 14/14 counts exact; 16,161-cell deep diff, 0 mismatches; decision: sidecar, no fork patch | Issue #3 / PR #4 |
| M1 | Full-corpus function/construct inventory; grammar freeze; lexer + parser + AST | 100% parse rate on all corpus formulas; pretty-print round-trip re-parses to identical AST | **Done** — 1,743,786/1,743,786 parsed, 0 round-trip failures; inventory frozen at 54 functions (+10 vs sample: LN, TAN, LOG10, ROUNDDOWN, COUNT, ISERR, ERROR.TYPE, OFFSET, MMULT, MINVERSE); 6 CSE array blocks found in one file (scoped to M4) | this change |
| M2 | Evaluator core: value model, error propagation, dependency graph, scalar functions | 100% cell match on LinkSwitch-TN_Rev2 (acyclic subset), ACDC_CP100X_Rev2, DCDC_DPAFwd_Rev2 | Pending | |
| M3 | Lookups (VLOOKUP/HLOOKUP/MATCH/INDEX), defined names, cross-sheet, text fns | ≥ 99.9% match on the six mid-size sample files; every mismatch triaged | Pending | |
| M4 | Dynamic references: INDIRECT/ADDRESS with dependency feedback | ≥ 99.9% match on LYTSwitch-4 (121k formulas) and LYTSwitch-0 (154k); full recalc < 2 s each | Pending | |
| M5 | Iterative calculation: SCC condensation + fixed-point solver | ≥ 99.9% match on all 15 iterate=true workbooks | Pending | |
| M6 | Hardening: full-corpus CI gate, incremental recalc, API polish, v0.1 | NFR1/NFR2 met on all 97 files; tagged v0.1 | Pending | |

## Log

- 2026-07-09 — M-B shipped and merged (Issue #1, PR #2).
- 2026-07-09 — M0 shipped and merged (Issue #3, PR #4).
- 2026-07-09 — M1 implemented and verified in sandbox (1.74M formulas, 100% parse + round-trip); ship pending. Corpus scan grew the function list 44 → 54 and found the CSE array blocks; PRD §4 and plan Phase 4 updated.
