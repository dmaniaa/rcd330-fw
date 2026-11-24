#!/usr/bin/env bash
# Builds/updates the public GitHub branch (default: github-main) from main
# without touching the working tree, stripping raw Ghidra decompilation outputs
# (docs/decompiled/**/functions/*, decompile-index.json, decompile-journal.jsonl)
# while keeping all written docs, per-binary READMEs, symbol metadata, emulator,
# tools, and recreated src/hmi/.
#
# Usage:
#   tools/export_public.sh [--orphan] [--branch <name>] [--from <ref>] [commit-message]
#
# First publish:  tools/export_public.sh --orphan
# Later updates:  tools/export_public.sh "Sync updates to public branch"
# Then push:      git push github github-main:main
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

ORPHAN=0
TARGET_BRANCH="github-main"
SRC_REF="main"
COMMIT_MSG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --orphan) ORPHAN=1; shift ;;
        --branch) TARGET_BRANCH="$2"; shift 2 ;;
        --from)   SRC_REF="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,13p' "$0"
            exit 0
            ;;
        *)
            COMMIT_MSG="$1"
            shift
            ;;
    esac
done

AUTHOR_NAME="${PUBLIC_AUTHOR_NAME:-dmaniaa}"
AUTHOR_EMAIL="${PUBLIC_AUTHOR_EMAIL:-dmaniaa@users.noreply.github.com}"

if [[ -z "$COMMIT_MSG" ]]; then
    if [[ $ORPHAN -eq 1 ]] || ! git rev-parse --verify --quiet "refs/heads/$TARGET_BRANCH" >/dev/null; then
        COMMIT_MSG="Initial public release: RCD 330 reverse engineering, QEMU emulator, and HMI Phase 0-1"
    else
        SRC_SUBJ="$(git log -1 --format=%s "$SRC_REF")"
        COMMIT_MSG="Public sync: $SRC_SUBJ"
    fi
fi

TMP_INDEX="$(mktemp "${TMPDIR:-/tmp}/rcd330_pub_index.XXXXXX")"
rm -f "$TMP_INDEX"
trap 'rm -f "$TMP_INDEX"' EXIT

GIT_INDEX_FILE="$TMP_INDEX" git read-tree "$SRC_REF"

# Strip raw Ghidra decompiled pseudocode and raw export manifests/journals
# from the temporary index only (working tree is never touched).
{
    GIT_INDEX_FILE="$TMP_INDEX" git ls-files \
        | grep -E '^docs/decompiled/.*/(functions/|decompile-(index\.json|journal\.jsonl)$)' || true
} | GIT_INDEX_FILE="$TMP_INDEX" git update-index --force-remove --stdin

TREE="$(GIT_INDEX_FILE="$TMP_INDEX" git write-tree)"

# Safety check: refuse to publish if any decompiled .c file or manifest slipped through.
LEAKED="$(git ls-tree -r --name-only "$TREE" | grep -E '^docs/decompiled/.*(\.c$|/(functions/|decompile-(index\.json|journal\.jsonl)$))' || true)"
if [[ -n "$LEAKED" ]]; then
    echo "ERROR: raw decompiled files still present in public tree:" >&2
    printf '%s\n' "$LEAKED" | sed -n '1,20p' >&2
    exit 1
fi

FILE_COUNT="$(git ls-tree -r --name-only "$TREE" | wc -l | tr -d ' ')"

PARENT_ARGS=()
if [[ $ORPHAN -eq 0 ]] && git rev-parse --verify --quiet "refs/heads/$TARGET_BRANCH" >/dev/null; then
    CUR_TREE="$(git rev-parse "${TARGET_BRANCH}^{tree}")"
    if [[ "$CUR_TREE" == "$TREE" ]]; then
        echo "$TARGET_BRANCH is already up to date with $SRC_REF ($FILE_COUNT public files)."
        exit 0
    fi
    PARENT_ARGS=(-p "refs/heads/$TARGET_BRANCH")
fi

COMMIT="$(
    GIT_AUTHOR_NAME="$AUTHOR_NAME" \
    GIT_AUTHOR_EMAIL="$AUTHOR_EMAIL" \
    GIT_COMMITTER_NAME="$AUTHOR_NAME" \
    GIT_COMMITTER_EMAIL="$AUTHOR_EMAIL" \
    git commit-tree "$TREE" ${PARENT_ARGS[@]+"${PARENT_ARGS[@]}"} -m "$COMMIT_MSG"
)"

git update-ref "refs/heads/$TARGET_BRANCH" "$COMMIT"
echo "Updated $TARGET_BRANCH -> $(git rev-parse --short "$COMMIT") ($FILE_COUNT files, 0 decompiled .c files)"
echo "Push with: git push github $TARGET_BRANCH:main"
