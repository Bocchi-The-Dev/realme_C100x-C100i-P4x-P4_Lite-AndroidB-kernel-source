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
# There are two distinct causes, and the first one is not the interesting one:
#
#   1. The release string moved.  UTS_RELEASE embeds the HEAD commit, so every
#      commit invalidates every previously built module's vermagic, while kbuild
#      rebuilds only what changed.  This is the common one and it is invisible:
#      nothing about a commit implies a full module rebuild.  do_config in
#      ci/box-build.sh now purges out/ when out/.nova-release moves, but that
#      detection is defeated if the marker file is removed by hand.
#
#   2. The tree was dirty for part of the build, which appends -dirty to some
#      stages and not others.  This is what the first version of this message
#      blamed, exclusively, and it was wrong to be confident -- both were true
#      at different times and the first was the one that actually bit.
#
# Either way the check belongs in the build rather than in a human's memory.
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
	echo "  which is a failure on the device and not in this build.  Two causes," >&2
	echo "  both of which have bitten here, listed most common first:" >&2
	echo >&2
	echo "  1. The release string moved between builds.  UTS_RELEASE is" >&2
	echo "     5.15.189-g<short sha>, so every commit changes it, and it is in" >&2
	echo "     every module's vermagic.  kbuild only rebuilds what changed, so a" >&2
	echo "     build after a commit leaves the untouched modules on the previous" >&2
	echo "     release.  ci/box-build.sh's do_config now purges out/ when" >&2
	echo "     out/.nova-release differs from HEAD, so 'box-build.sh all' should" >&2
	echo "     not hit this.  If it did, the marker was probably removed by hand," >&2
	echo "     which disables the detection.  Fix: rm -rf out and rebuild." >&2
	echo >&2
	echo "  2. The tree was dirty for part of the build.  A path-limited" >&2
	echo "     'git checkout -f FETCH_HEAD -- <paths>' stages those paths, so" >&2
	echo "     setlocalversion appends -dirty to later builds but not earlier" >&2
	echo "     ones.  Fix: 'git checkout -f FETCH_HEAD' with no path limit, and" >&2
	echo "     keep out/ and pkg/ in .gitignore." >&2
	exit 1
fi

echo "  all $total modules match UTS_RELEASE and will load"
exit 0
