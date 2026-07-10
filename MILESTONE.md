# Milestones — xl++ Formula Engine

Working memory for the development cycle: what shipped, what is in flight, what is next.
Updated by the agent with every shipped change. Phase details and exit criteria:
`docs/IMPLEMENTATION_PLAN.md`.

**Current item:** Phase 5 (M5) — shipped, awaiting PR merge.

| ID | Milestone | Exit criterion (gate) | Status | Shipped |
|----|-----------|-----------------------|--------|---------|
| M-B | Repo bootstrap: xlnt submodule, PRD + plan docs, ship workflow | Repo builds the docs baseline; submodule resolvable | **Done** | Issue #1 / PR #2 |
| M0 | Feasibility spike: prober + workbook.xml sidecar (definedNames, calcPr) | All 97 corpus files load via xlnt; extraction verified vs openpyxl on 14-file sample; patch-vs-sideparse decision | **Done** — 97/97 load; 14/14 counts exact; 16,161-cell deep diff, 0 mismatches; decision: sidecar, no fork patch | Issue #3 / PR #4 |
| M1 | Full-corpus function/construct inventory; grammar freeze; lexer + parser + AST | 100% parse rate on all corpus formulas; pretty-print round-trip re-parses to identical AST | **Done** — 1,743,786/1,743,786 parsed, 0 round-trip failures; inventory frozen at 54 functions (+10 vs sample: LN, TAN, LOG10, ROUNDDOWN, COUNT, ISERR, ERROR.TYPE, OFFSET, MMULT, MINVERSE); 6 CSE array blocks found in one file (scoped to M4) | Issue #5 / PR #6 |
| M2+M3 | Evaluator core + lookups + defined names + text fns (merged, approved) | 100% on 3 small files; ≥ 99.9% on 6 mid-size files; every mismatch triaged | **Done** — 100.0000% on all 9 gate files (66k cells); 526-cell whitelist, each entry individually justified (474 provably stale #VALUE! region HiperPFS3, 51 FP-cancellation residues InnoSwitch3CP, 1 stale DPAFwd); corpus-wide bonus: 99.68% of 1.44M evaluable cells match; NFR2 met (121k-formula file: 0.6s load + 0.12s recalc) | Issue #7 / PR #8 |
| M4 | Dynamic references (INDIRECT/ADDRESS/OFFSET) + CSE array blocks | ≥ 99.9% match on LYTSwitch-4 (121k), LYTSwitch-0 (154k), LYTSwitch1_BuckBoost; full recalc < 2 s each | **Done** — 100.0000% on all three (plus ADDRESS file LNK-PL and OFFSET file TNY_Rev2 at 100%); both giants verify in 2.5s combined; harness gained a mechanical staleness prover + taint propagation (LYTSwitch-4's 52.9k-cell INDIRECT region is provably stale cache) and an array-FP closure; whitelist regenerated at 357 entries (51 KP1 + 306 matrix-inverse FP, all ≤ 4.4e-4 rel); corpus: 99.95% of 1.44M evaluated, 0 deferred, 0 cyclic | Issue #9 / PR #10 |
| M5 | Iterative calculation: fixed-point solver (FR6) | ≥ 99.9% match on all 15 iterate=true workbooks | **Done** — 100.0000% / 0 mismatches on all 15; their self-referential branches are inactive for saved inputs (0 live cycles), so the Gauss-Seidel solver (previous-pass value injection at cycle cuts, iterateCount/iterateDelta honored, converged flag in stats) is proven by unit test (tests/iterative_test.cpp: converges to fixed point; iterate=false → cyclic diagnostics; set_value detach). LNK-PL's 21.9k stale cells root-caused: literal =#REF! cell (N33) poisons the state-machine chain today, while caches date from an older #NAME? era — genuinely stale, not fixed-point drift | this change |
| M6 | Hardening: full-corpus CI gate, incremental recalc, API polish, v0.1 | NFR1/NFR2 met on all 97 files; tagged v0.1 | Pending | |

## Log

- 2026-07-09 — M-B shipped and merged (Issue #1, PR #2).
- 2026-07-09 — M0 shipped and merged (Issue #3, PR #4).
- 2026-07-09 — M1 implemented and verified in sandbox (1.74M formulas, 100% parse + round-trip); ship pending. Corpus scan grew the function list 44 → 54 and found the CSE array blocks; PRD §4 and plan Phase 4 updated.
- 2026-07-09 — M1 shipped and merged (Issue #5, PR #6).
- 2026-07-10 — M2+M3 (merged phases, Doc-approved) implemented and verified: demand-driven evaluator (static topo as scheduling hint only), 49 of 54 functions live (INDIRECT/ADDRESS/OFFSET/MMULT/MINVERSE deferred to M4), shared-formula translation side-parse (xlnt gap 5), oracle whitelist mechanism. 100% on all 9 gate files; ship pending.
- 2026-07-10 — M2+M3 shipped and merged (Issue #7, PR #8).
- 2026-07-10 — M4 implemented and verified: all 54 corpus functions now live. INDIRECT/OFFSET are trivial under demand-driven evaluation; CSE blocks evaluate as matrices (LU + iterative refinement). Major oracle finding: LYTSwitch-4 caches #REF! for all 2,644 INDIRECT cells while their cached targets hold plain numbers — 52.9k cells provably stale; harness gained a mechanical staleness prover (replay against cached state) + transitive taint propagation + array-FP closure. Residual corpus mismatches: 746 in 14 files (InnoSwitch3 sibling family KP1 pattern + HiperPLC810) → M6 corpus-gate triage. Note for M5: iterate=true files show 0 dynamic cycles (converged caches); LNK-PL's 21.9k stale cells deserve scrutiny then.
- 2026-07-10 — M4 shipped and merged (Issue #9, PR #10).
- 2026-07-10 — M5 implemented and verified: FR6 fixed-point solver + first unit test (tests/). All 15 iterate files at 100% / 0 mismatches. Investigation confirmed no corpus file has live cycles for saved inputs; LNK-PL staleness root-caused to a literal =#REF! formula cell (N33) poisoning the state-machine chain, with caches from an older #NAME? era. Ship pending.
