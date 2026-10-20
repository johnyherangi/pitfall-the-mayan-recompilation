#!/bin/bash
# publish.sh <public repository URL or path> [branch]
#
# The public repository (pitfall-the-mayan-recompilation) from this private one
# (pitfall-the-mayan-decompilation): the same history with the original program's decompilation,
# disassembly, dumps and screenshots removed from every commit, pushed with --force.  Run it
# again after new commits here; the public history is rewritten each time, so nobody should
# commit to the public repository directly (changes go here).
#
# What the public repository does not get (kept here, see re/README.md):
#   re/pitfall_decomp.c and its .data.txt / .symbols.txt, re/pitfall_handlers.c   Ghidra's decompile
#   re/listings/                                                                    annotated disassembly
#   re/evidence1/                                                                   the REA evidence bundle
#   re/index.tsv re/procs.* re/strings.* re/imports.txt re/sprite_slots*.txt       symbol and string dumps
#   re/renders/                                                                     frames rendered during the work
# Everything else goes: the source, the tests, the tools, the docs, re/verify and re/drivers.
set -eu
URL=${1:?usage: publish.sh <public repository URL or path> [branch]}
BRANCH=${2:-main}
SRC=$(git rev-parse --show-toplevel)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git clone -q --no-local "$SRC" "$TMP/pub"
cd "$TMP/pub"
git checkout -q "$BRANCH"
export FILTER_BRANCH_SQUELCH_WARNING=1
git filter-branch -f --prune-empty --index-filter '
  git rm -r -q --cached --ignore-unmatch \
    re/pitfall_decomp.c re/pitfall_decomp.c.data.txt re/pitfall_decomp.c.symbols.txt re/pitfall_handlers.c \
    re/listings re/evidence1 re/renders \
    re/index.tsv re/procs.tsv re/procs.txt re/strings.tsv re/strings.txt re/imports.txt \
    re/sprite_slots.txt re/sprite_slots2.txt re/sprite_slots_all.txt' -- "$BRANCH" > /dev/null
# what is left of re/ in every commit, to be sure
if git log --all --name-only --pretty=format: | sort -u | grep -q -E '^re/(listings|evidence1|renders)/|^re/pitfall_decomp|^re/pitfall_handlers|^re/strings|^re/procs|^re/index.tsv|^re/imports|^re/sprite_slots'; then
  echo "publish.sh: a removed path is still in the history" >&2; exit 1
fi
echo "publish.sh: $(git rev-list --count "$BRANCH") commits, $(git ls-files | wc -l) files at the tip"
git remote set-url origin "$URL"
git push -q --force origin "$BRANCH"
echo "publish.sh: pushed $BRANCH to $URL"
