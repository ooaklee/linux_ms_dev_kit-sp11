#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# export-package.sh — regenerate the SP11 self-compile driver package from git.
#
# Copies the three standalone-qualified research drivers verbatim from the
# tree and stamps their source SHAs into each per-driver .source-sha, so the
# package cannot drift silently from the in-tree sources.
#
# Usage: scripts/export-package.sh [TREEISH]   (default: HEAD)
set -eu

TREEISH=${1:-HEAD}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG="$ROOT/sp11-selfcompile-drivers"

# dir|source-path|module-name
ENTRIES="
vd55g0|drivers/media/i2c/vd55g1.c|vd55g1
ov13858|drivers/media/i2c/ov13858.c|ov13858
ps883x|drivers/usb/typec/mux/ps883x.c|ps883x
"

echo "$ENTRIES" | while IFS='|' read -r dir src mod; do
	[ -n "$dir" ] || continue
	if ! sha=$(git -C "$ROOT" rev-parse "$TREEISH:$src" 2>/dev/null); then
		echo "ERROR: $src not found in $TREEISH" >&2
		exit 1
	fi
	mkdir -p "$PKG/$dir"
	git -C "$ROOT" show "$TREEISH:$src" > "$PKG/$dir/$(basename "$src")"
	{
		echo "source: $src"
		echo "tree:   $TREEISH ($(git -C "$ROOT" rev-parse --short "$TREEISH"))"
		echo "sha:    $sha"
	} > "$PKG/$dir/.source-sha"
done

# Standalone distribution must carry the license text (GPLv2 section 1).
if [ -f "$ROOT/COPYING" ]; then
	cp "$ROOT/COPYING" "$PKG/COPYING"
else
	echo "ERROR: kernel COPYING not found at $ROOT/COPYING" >&2
	exit 1
fi

# Package-local ignores for build artifacts (useful when building in-tree).
printf '%s\n' '*.o' '*.ko' '.*.cmd' '*.mod' '*.mod.c' 'Module.symvers' \
	'modules.order' '.tmp_versions' > "$PKG/.gitignore"

echo "Package sources refreshed under $PKG from $TREEISH"
