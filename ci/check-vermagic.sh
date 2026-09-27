#!/bin/bash
# ci/check-vermagic.sh - assert every module we ship will actually load.
#
# Why this exists
#
# The kernel compares a module's vermagic against its own UTS_RELEASE as an
# exact string, and a mismatch makes insmod fail with
#
#   version magic '5.15.189-ga3d8d2e5b8b5' should be '5.15.189-ga3d8d2e5b8b5-dirty'
#
# which is a failure at load time on the device, not at build time.  Nothing in
# the build reports it: the modules compile, modpost is happy, coverage is
# 100%, and the set is only rejected once it is already on the phone.
#
# The cause is mundane.  UTS_RELEASE picks up a -dirty suffix from
# scripts/setlocalversion when `git status --porcelain` is non-empty.  Doing a
# path-limited update --
#
#   git checkout -f FETCH_HEAD -- ci/ arch/arm64/configs/ drivers/...
#
# -- stages those paths, so the tree is non-empty afterwards and every
# subsequent build stamps -dirty into every module it rebuilds.  A build that
# mixes an earlier clean-tree stage with a later dirty-tree stage therefore ends
# up with BOTH strings present, and only the minority is wrong.  That is exactly
# what happened: 178 modules carried -dirty and 96 did not, against a kernel
# built with -dirty.
#
# It is also self-inflicted by this project's own iteration loop, which does
# path-limited checkouts to avoid a full vmlinux relink.  So the check belongs
# in the build rather than in a human's memory.
#
# Usage: ci/check-vermagic.sh <out-dir> [<out-dir> ...]
#   Each argument is searched for .ko files.  Exits non-zero on any mismatch.

set -uo pipefail

if [ "$#" -eq 0 ]; then
	echo "usage: $0 <out-dir>..." >&2
	exit 2
fi

OUTDIRS=("$@")

# The kernel's own idea of its release, as generated into the build tree.
RELEASE=""
for cand in \
	"${OUTDIRS[0]}/include/generated/utsrelease.h" \
	out/include/generated/utsrelease.h
do
	[ -f "$cand" ] || continue
	RELEASE="$(sed -n 's/^#define UTS_RELEASE "\(.*\)"$/\1/p' "$cand" | head -1)"
	[ -n "$RELEASE" ] && break
done

if [ -z "$RELEASE" ]; then
	echo "=== VERMAGIC ==="
	echo "could not read UTS_RELEASE from include/generated/utsrelease.h" >&2
	exit 2
fi

# Collect (vermagic, path) for every module.  'strings' is used rather than
# modinfo because modinfo is not always present on a bare build box, and the
# vermagic is a plain embedded string.
list_modules() {
	local d
	for d in "${OUTDIRS[@]}"; do
		[ -d "$d" ] || continue
		find "$d" -name '*.ko' -type f 2>/dev/null
	done
}

total=0
bad=0
declare -A seen_counts 2>/dev/null || true

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

while IFS= read -r ko; do
	v="$(strings "$ko" 2>/dev/null | grep -m1 '^vermagic=' | cut -d' ' -f1)"
	v="${v#vermagic=}"
	total=$((total + 1))
	if [ "$v" != "$RELEASE" ]; then
		bad=$((bad + 1))
		# Keep a bounded sample: a wholesale mismatch is one fact, not 200.
		if [ "$bad" -le 10 ]; then
			printf '  MISMATCH %-64s %s\n' "$v" "$ko" >&2
		fi
	fi
	seen_counts["$v"]=$(( ${seen_counts["$v"]:-0} + 1 ))
done < <(list_modules)

echo "=== VERMAGIC ==="
echo "  kernel UTS_RELEASE : $RELEASE"
echo "  modules checked    : $total"
for v in "${!seen_counts[@]}"; do
	n="${seen_counts[$v]}"
	if [ "$v" = "$RELEASE" ]; then
		printf '  ok       %-56s %d\n' "$v" "$n"
	else
		printf '  MISMATCH %-56s %d\n' "$v" "$n"
	fi
done

if [ "$total" -eq 0 ]; then
	echo "  ASSERTION FAILED: no modules found to check" >&2
	exit 1
fi

if [ "$bad" -ne 0 ]; then
	echo "  ASSERTION FAILED: $bad of $total modules will not load" >&2
	echo "  Those modules would be rejected with 'version magic ... should be'," >&2
	echo "  which is a failure on the device and not in this build.  Usually a" >&2
	echo "  path-limited 'git checkout -f FETCH_HEAD -- <paths>' left the tree" >&2
	echo "  dirty, so some stage built without -dirty and some with.  Fix by" >&2
	echo "  rebuilding from a clean tree: git checkout -f FETCH_HEAD (no path" >&2
	echo "  limit), then a full 'make Image modules'." >&2
	exit 1
fi

echo "  all $total modules match UTS_RELEASE and will load"
exit 0
