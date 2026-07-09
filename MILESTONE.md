# Milestones — xl++ Formula Engine

Working memory for the development cycle: what shipped, what is in flight, what is next.
Updated by the agent with every shipped change. Phase details and exit criteria:
`docs/IMPLEMENTATION_PLAN.md`.

**Current item:** Phase 2+3 (merged) — shipped, awaiting PR merge.

| ID | Milestone | Exit criterion (gate) | Status | Shipped |
|----|-----------|-----------------------|--------|---------|
| M-B | Repo bootstrap: xlnt submodule, PRD + plan docs, ship workflow | Repo builds the docs baseline; submodule resolvable | **Done** | Issue #1 / PR #2 |
| M0 | Feasibility spike: prober + workbook.xml sidecar (definedNames, calcPr) | All 97 corpus files load via xlnt; extraction verified vs openpyxl on 14-file sample; patch-vs-sideparse decision | **Done** — 97/97 load; 14/14 counts exact; 16,161-cell deep diff, 0 mismatches; decision: sidecar, no fork patch | Issue #3 / PR #4 |
| M1 | Full-corpus function/construct inventory; grammar freeze; lexer + parser + AST | 100% parse rate on all corpus formulas; pretty-print round-trip re-parses to identical AST | **Done** — 1,743,786/1,743,786 parsed, 0 round-trip failures; inventory frozen at 54 functions (+10 vs sample: LN, TAN, LOG10, ROUNDDOWN, COUNT, ISERR, ERROR.TYPE, OFFSET, MMULT, MINVERSE); 6 CSE array blocks found in one file (scoped to M4) | Issue #5 / PR #6 |
| M2+M3 | Evaluator core + lookups + defined names + text fns (merged, approved) | 100% on 3 small files; ≥ 99.9% on 6 mid-size files; every mismatch triaged | **Done** — 100.0000% on all 9 gate files (66k cells); 526-cell whitelist, each entry individually justified (474 provably stale #VALUE! region HiperPFS3, 51 FP-cancellation residues InnoSwitch3CP, 1 stale DPAFwd); corpus-wide bonus: 99.68% of 1.44M evaluable cells match; NFR2 met (121k-formula file: 0.6s load + 0.12s recalc) | this change |
| M4 | Dynamic references: INDIRECT/ADDRESS with dependency feedback | ≥ 99.9% match on LYTSwitch-4 (121k formulas) and LYTSwitch-0 (154k); full recalc < 2 s each | Pending | |
| M5 | Iterative calculation: SCC condensation + fixed-point solver | ≥ 99.9% match on all 15 iterate=true workbooks | Pending | |
| M6 | Hardening: full-corpus CI gate, incremental recalc, API polish, v0.1 | NFR1/NFR2 met on all 97 files; tagged v0.1 | Pending | |

## Log

- 2026-07-09 — M-B shipped and merged (Issue #1, PR #2).
- 2026-07-09 — M0 shipped and merged (Issue #3, PR #4).
- 2026-07-09 — M1 implemented and verified in sandbox (1.74M formulas, 100% parse + round-trip); ship pending. Corpus scan grew the function list 44 → 54 and found the CSE array blocks; PRD §4 and plan Phase 4 updated.
- 2026-07-09 — M1 shipped and merged (Issue #5, PR #6).
- 2026-07-10 — M2+M3 (merged phases, Doc-approved) implemented and verified: demand-driven evaluator (static topo as scheduling hint only), 49 of 54 functions live (INDIRECT/ADDRESS/OFFSET/MMULT/MINVERSE deferred to M4), shared-formula translation side-parse (xlnt gap 5), oracle whitelist mechanism. 100% on all 9 gate files; ship pending.
