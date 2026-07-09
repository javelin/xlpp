#!/usr/bin/env bash
# =============================================================================
# ship_change.sh — self-contained "ship a change" helper (xlpp)
#
# WHY THIS IS SELF-CONTAINED:
# The sandbox agent does not run any git write commands, because git locks it
# creates on the shared .git dir get owned by your user and can't be cleaned up
# from the sandbox (the recurring ".git/index.lock: Operation not permitted").
# So ALL git operations happen here, on your machine, where locks clear cleanly.
#
# The agent just edits files in the working tree (on master). This script then:
#   1. Clears any stale lock
#   2. Creates the feature branch off origin/master, CARRYING those edits onto
#      it (nothing is ever committed on master)
#   3. Runs any extra git setup for this change (e.g. submodule add)
#   4. Creates the issue via gh (captures the number)
#   5. Stages ONLY the listed paths, commits as "Issue #<N>: ...", pushes
#   6. Prints the PR link
#
# NOTE: this repo's main branch is `master`, not `main`.
#
# Run from the repo root:  ./ship_change.sh
# =============================================================================
set -euo pipefail

# ------------------------------- CONFIG --------------------------------------
# The agent fills these in for each change before you run the script.

REPO="javelin/xlpp"
PARENT_REF="origin/master"        # always branch off the remote master
BRANCH="chore/xlnt-submodule-and-docs"

# Files to stage (only these are added — nothing else). Paths created by the
# EXTRA GIT SETUP step below may be listed even though they don't exist yet;
# existence is only enforced for paths in FILES_MUST_EXIST.
FILES=(
  "ship_change.sh"
  "ai_rules.md"
  "docs/PRD.md"
  "docs/IMPLEMENTATION_PLAN.md"
  "docs/analysis.json"
  ".gitmodules"
  "third_party/xlnt"
)

FILES_MUST_EXIST=(
  "ship_change.sh"
  "ai_rules.md"
  "docs/PRD.md"
  "docs/IMPLEMENTATION_PLAN.md"
  "docs/analysis.json"
)

# Submodule for this change (leave SUBMODULE_PATH empty for changes without one).
SUBMODULE_URL="git@github.com:javelin/xlnt.git"
SUBMODULE_PATH="third_party/xlnt"

ISSUE_TITLE="Bootstrap repo: xlnt submodule, PRD + implementation plan, ship workflow"

read -r -d '' ISSUE_BODY <<'EOF' || true
## Purpose
Bootstrap the xlpp repository — the C++ formula-evaluation engine for the
PIXls power-supply design spreadsheets (see docs/PRD.md).

## Change
- Add `xlnt` (javelin fork) as a submodule at `third_party/xlnt`. The fork is
  used so xlnt gaps found in the corpus analysis (defined-name values and
  calcPr iteration settings are dropped by the reader) can be patched if the
  side-parse approach proves insufficient (PRD §6).
- Add `docs/PRD.md`, `docs/IMPLEMENTATION_PLAN.md`, and the raw corpus
  analysis data (`docs/analysis.json`, 14-file sample of the 99 workbooks).
- Track `ai_rules.md` (engineering rules) and `ship_change.sh` (this
  workflow: agent edits files, all git/gh operations run locally).

## Verification
- Script creates the branch off origin/master, adds the submodule, stages only
  the listed paths, and pushes; nothing is committed on master.
- After merge: `git submodule update --init` then confirm
  `third_party/xlnt/CMakeLists.txt` exists.
EOF

COMMIT_SUBJECT="Bootstrap repo: xlnt submodule, docs, ship workflow"

read -r -d '' COMMIT_BODY <<'EOF' || true
Add xlnt (javelin fork) as submodule at third_party/xlnt for the xlpp formula
engine (fork allows patching reader gaps: defined-name values and calcPr
iteration settings, PRD section 6). Add PRD, phased implementation plan, and
raw corpus analysis of the PIXls spreadsheets (44-function inventory, 15
iterative-calc workbooks, INDIRECT patterns). Track ai_rules.md and the
ship_change.sh workflow script.
EOF
# -----------------------------------------------------------------------------

# --------------------------- PRE-FLIGHT CHECKS -------------------------------
# Clear any stale git lock first (runs as YOU, so it can actually remove it).
rm -f .git/*.lock

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "ERROR: not inside a git repo. cd to the repo root first."; exit 1; }
command -v gh >/dev/null 2>&1 || { echo "ERROR: gh (GitHub CLI) not found."; exit 1; }

[ -n "$BRANCH" ] || { echo "ERROR: BRANCH is empty — the config wasn't filled in."; exit 1; }
[ "${#FILES[@]}" -gt 0 ] || { echo "ERROR: FILES is empty — nothing to ship."; exit 1; }

for f in "${FILES_MUST_EXIST[@]}"; do
  [ -e "$f" ] || { echo "ERROR: file not found: $f"; exit 1; }
done

if git show-ref --verify --quiet "refs/heads/${BRANCH}"; then
  echo "ERROR: branch '${BRANCH}' already exists locally. Delete it or pick a new name."; exit 1
fi

# Confirm the pre-existing files actually have changes to ship — including NEW
# (untracked) files, which `git diff` does not report.
if [ -z "$(git status --porcelain -- "${FILES_MUST_EXIST[@]}")" ]; then
  echo "ERROR: no changes detected in the listed files. Did the edits land?"; exit 1
fi

# ------------------- 1. FETCH + BRANCH off origin/master ---------------------
# Branch off the latest remote master, carrying the working-tree edits with us.
echo ">> Fetching origin..."
git fetch origin --quiet

echo ">> Creating branch '${BRANCH}' off ${PARENT_REF} (carrying your edits)..."
git checkout -b "$BRANCH" "$PARENT_REF"

# Safety: never commit on main/master.
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$CURRENT_BRANCH" = "main" ] || [ "$CURRENT_BRANCH" = "master" ]; then
  echo "ERROR: expected to be on '${BRANCH}' but on '${CURRENT_BRANCH}'. Aborting before commit."; exit 1
fi

# ---------------------- 2. EXTRA GIT SETUP (submodule) -----------------------
if [ -n "$SUBMODULE_PATH" ]; then
  if git config --file .gitmodules --get "submodule.${SUBMODULE_PATH}.url" >/dev/null 2>&1; then
    echo ">> Submodule '${SUBMODULE_PATH}' already registered — skipping add."
  else
    echo ">> Adding submodule ${SUBMODULE_URL} at ${SUBMODULE_PATH}..."
    git submodule add "$SUBMODULE_URL" "$SUBMODULE_PATH"
  fi
fi

# --------------------------- 3. CREATE ISSUE ---------------------------------
echo ">> Creating issue..."
ISSUE_URL=$(gh issue create --repo "$REPO" --title "$ISSUE_TITLE" --body "$ISSUE_BODY")
ISSUE_NUM=$(basename "$ISSUE_URL")
[[ "$ISSUE_NUM" =~ ^[0-9]+$ ]] || { echo "ERROR: could not parse issue number from '$ISSUE_URL'"; exit 1; }
echo ">> Created issue #${ISSUE_NUM} -> ${ISSUE_URL}"

# ----------------------------- 4. STAGE + COMMIT -----------------------------
echo ">> Staging files..."
git add -- "${FILES[@]}"
git status --short

echo ">> Committing..."
git commit \
  -m "Issue #${ISSUE_NUM}: ${COMMIT_SUBJECT}" \
  -m "${COMMIT_BODY}" \
  -m "Closes #${ISSUE_NUM}"

# -------------------------------- 5. PUSH ------------------------------------
echo ">> Pushing..."
git push -u origin "$BRANCH"

echo ""
echo "============================================================"
echo "Done. Issue #${ISSUE_NUM}"
echo "Open the PR:"
echo "  https://github.com/${REPO}/compare/master...${BRANCH}?expand=1"
echo ""
echo "After merging, sync master for the next change:"
echo "  git checkout master && rm -f .git/*.lock && git pull --ff-only origin master"
echo "============================================================"
