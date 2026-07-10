#!/usr/bin/env bash
# =============================================================================
# corpus_gate.sh — full-corpus verification gate (Phase 6 / CI).
#
# Runs the oracle harness over every workbook in the corpus with the committed
# whitelist and fails on ANY unexplained mismatch. Stale caches, array-FP
# closure cells, and whitelisted FP residues are classified, not failures.
#
# Usage:
#   XLPP_CORPUS=/path/to/pixls ./tools/corpus_gate.sh [path/to/verify]
#
# Default verify binary: ./build/verify
# =============================================================================
set -euo pipefail

CORPUS="${XLPP_CORPUS:?set XLPP_CORPUS to the folder containing the .xlsx corpus}"
VERIFY="${1:-./build/verify}"
WHITELIST="$(dirname "$0")/verify_whitelist.tsv"

[ -x "$VERIFY" ] || { echo "ERROR: verify binary not found/executable: $VERIFY"; exit 2; }
[ -f "$WHITELIST" ] || { echo "ERROR: whitelist not found: $WHITELIST"; exit 2; }

shopt -s nullglob
FILES=("$CORPUS"/*.xlsx)
[ "${#FILES[@]}" -gt 0 ] || { echo "ERROR: no .xlsx files in $CORPUS"; exit 2; }

echo ">> corpus gate: ${#FILES[@]} workbooks"
STATUS=0
OUT=$("$VERIFY" --details 25 --whitelist "$WHITELIST" "${FILES[@]}") || STATUS=$?

GATE=0
echo "$OUT" | tail -n +2 | awk -F'\t' '
    NF > 6 {
        files++; formulas += $2; evaluated += $3; matched += $4; mismatched += $5;
        whitelisted += $6; stale += $7; array_fp += $8; cyclic += $10;
        if ($5 > 0) { bad++; printf "MISMATCHED FILE: %s (%d cells)\n", $1, $5 }
    }
    END {
        printf "files=%d formulas=%d evaluated=%d matched=%d mismatched=%d ", \
               files, formulas, evaluated, matched, mismatched
        printf "whitelisted=%d stale=%d array_fp=%d cyclic=%d\n", \
               whitelisted, stale, array_fp, cyclic
        exit (mismatched > 0 || files == 0) ? 1 : 0
    }
' || GATE=$?

if [ "$STATUS" -ne 0 ] || [ "$GATE" -ne 0 ]; then
    echo "CORPUS GATE: FAIL"
    exit 1
fi
echo "CORPUS GATE: PASS"
