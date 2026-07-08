#!/usr/bin/env bash
#
# uppercase_tree.sh — prep a directory tree for an ISO9660 / Dreamcast build.
#
#   1. Deletes every dot-entry (dot files AND dot directories, whole subtree):
#      .DS_Store, ._AppleDouble, .git, .svn, .hidden, etc.
#   2. UPPERCASEs every remaining file AND directory name, recursively.
#
# Case-only renames go through a temp name so they survive a case-INSENSITIVE
# volume (macOS APFS/HFS+), where `mv foo FOO` is otherwise a no-op.
#
# Usage:  ./uppercase_tree.sh <directory> [-n|--dry-run]
#
set -euo pipefail

TARGET="${1:?usage: $0 <directory> [-n|--dry-run]}"
DRY=0
[ "${2:-}" = "-n" ] || [ "${2:-}" = "--dry-run" ] && DRY=1

[ -d "$TARGET" ] || { echo "not a directory: $TARGET" >&2; exit 1; }

run() { if [ "$DRY" = 1 ]; then echo "DRYRUN: $*"; else "$@"; fi; }

# ---- 1. remove dot-entries (‑prune so we don't descend into a dot dir twice) ----
while IFS= read -r -d '' dot; do
    echo "rm    $dot"
    run rm -rf -- "$dot"
done < <(find "$TARGET" -mindepth 1 -name '.*' -prune -print0)

# ---- 2. uppercase names, deepest-first so a parent rename can't strand a child ----
while IFS= read -r -d '' path; do
    dir=$(dirname -- "$path")
    base=$(basename -- "$path")
    upper=$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')
    [ "$base" = "$upper" ] && continue                    # already uppercase
    target="$dir/$upper"
    if [ -e "$target" ] && ! [ "$path" -ef "$target" ]; then
        echo "SKIP  collision: $path  ->  $upper (already exists)" >&2
        continue
    fi
    echo "mv    $path  ->  $upper"
    if [ "$DRY" = 0 ]; then
        tmp="$dir/.__uptmp__$$_$RANDOM"
        mv -- "$path" "$tmp"                              # 2-step survives
        mv -- "$tmp" "$target"                            # case-insensitive FS
    fi
done < <(find "$TARGET" -depth -mindepth 1 -print0)

echo "done."
