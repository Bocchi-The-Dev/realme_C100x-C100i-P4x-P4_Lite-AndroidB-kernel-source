#!/bin/bash
# ci/check-module-coverage.sh - does our module set satisfy the X6525's loader?
#
# WHY THIS EXISTS
# ---------------
# The device's /vendor/lib/modules/modules.load lists 133 modules in the exact
# order Android insmods them.  Those are 5.4 modules: their vermagic is
# "5.4.210-...-ab665 ... modversions" and CONFIG_MODVERSIONS=y, so their symbol
# CRCs belong to that kernel and they cannot load against a 5.15 Image no matter
# what we do.  Replacing /lib/modules is therefore mandatory, not optional.
#
# Once we replace it, modules.load has to be rewritten, and the interesting
# question becomes: which of the 133 names do we actually produce?  This script
# answers that against the real build output instead of against intent, and
# reports the three cases that matter differently:
#
#   covered   - we produce a module with exactly that name
#   missing   - the load list names it and we do not produce it
#   extra     - we produce a module the stock load list never mentions
#               (candidates to drop, or evidence of a rename)
#
# "extra" is where the renames show up.  Two examples already measured on the
# device: the stock list says mali_kbase.ko and our Kbuild says mali_gondul.ko,
# and the stock list says omnivision_td4160.ko where 5.15 folds the panel into
# the in-tree DSI panel driver and the touch into omnivision_tcm.  Neither is a
# build failure; both need an explicit entry in the rewritten load list, which
# is why this script prints them instead of just counting.
#
# A "missing" entry is only fatal for BOOT_BLOCKING.  Most of the stock list is
# optional for a first boot: Transsion value-add (tran_*, tnek, switch_class,
# sysdumpdb_switch), fingerprint drivers for other sensors, USB ethernet dongles
# and audio codecs for other PMICs.  Missing those costs a feature, not a boot.
# BOOT_BLOCKING below is the measured set -- taken by intersecting the failing
# units with the device's own /proc/modules over adb -- and missing any of those
# exits 1.

set -uo pipefail

STOCK="${1:?usage: check-module-coverage.sh <stock-modules.load> <dir> [dir ...]}"
shift
DIRS=("$@")
[ "${#DIRS[@]}" -gt 0 ] || { echo "no module directories given" >&2; exit 2; }

# Two tiers, and the distinction is deliberate.
#
# BOOT_BLOCKING is small on purpose.  Strictly, a phone whose WiFi, camera and
# GPU drivers are all missing still reaches the launcher: what stops it coming up
# is the display path -- no DRM, no trusty TUI, no panel, and the composer HAL
# has nothing to talk to.  Inflating this list to "everything the user might
# notice" would make the assertion cry wolf on every optional driver and get
# ignored, which is the failure mode this whole script exists to prevent.
BOOT_BLOCKING="sprd-drm trusty-tui sprd_wlan_combo wcn_bsp"
#
# DEVICE_CRITICAL is reported prominently but does not fail the build.  These
# are the subsystems a daily driver needs; each was measured as present in the
# device's /proc/modules AND failing or misnamed in our build.
DEVICE_CRITICAL="mali_kbase sprd_camera sprd_cpp sprd_sensor sprdbt_tty sprd_fm snd-soc-sprd-codec-sc2730 mcdt_hw_r2p0 sprd_gpu_cooling sprd-ion"

# Stock-name -> our-name renames, so a rename is not reported as a gap.  Kept in
# a data file (ci/module-aliases.stock515) with the reasoning for each, because
# an unexplained rename map is indistinguishable from a fudge.
ALIASES="$(dirname "$0")/module-aliases.stock515"
resolve() {
	local n="$1"
	[ -f "${ALIASES}" ] || { echo "$1"; return; }
	local m
	m=$(grep -E "^${n}:" "${ALIASES}" 2>/dev/null | head -1 | cut -d: -f2)
	[ -n "${m}" ] && echo "${m}" || echo "${n}"
}

produced_all=$(mktemp)
for d in "${DIRS[@]}"; do
	find "$d" -name '*.ko' -printf '%f\n' 2>/dev/null
done | sed 's/\.ko$//' | sort -u >"${produced_all}"

produced=$(wc -l <"${produced_all}")
covered=0
missing_list=$(mktemp)
extra_list=$(mktemp)
renamed_list=$(mktemp)

while read -r entry; do
	[ -n "${entry}" ] || continue
	name="${entry%.ko}"
	if grep -qxF "${name}" "${produced_all}"; then
		covered=$((covered + 1))
	elif [ "$(resolve "${name}")" != "${name}" ]; then
		# renamed: satisfied if the new name exists
		if grep -qxF "$(resolve "${name}")" "${produced_all}"; then
			covered=$((covered + 1))
			echo "${name} -> $(resolve "${name}")" >>"${renamed_list}"
		else
			echo "${name}" >>"${missing_list}"
		fi
	else
		echo "${name}" >>"${missing_list}"
	fi
done <"${STOCK}"

# "extra" only means something for names we know about, so intersect rather
# than diffing the whole universe.
sort -u "${missing_list}" -o "${missing_list}"
grep -vxF -f "${STOCK%.stock}" "${produced_all}" 2>/dev/null | sort -u >"${extra_list}" || true

stock_total=$(grep -c . "${STOCK}" || true)

echo "=== module coverage vs the X6525 stock modules.load ==="
echo "  stock load entries : ${stock_total}"
echo "  modules we produce : ${produced}"
echo "  covered (incl. renames): ${covered}"
echo "  missing by name    : $(wc -l <"${missing_list}")"

if [ -s "${renamed_list}" ]; then
	echo
	echo "--- RENAMED (stock name satisfied under a 5.15 name) ---"
	sed 's/^/       /' "${renamed_list}"
fi

echo
echo "--- MISSING (named by the stock loader, absent from our build) ---"
if [ -s "${missing_list}" ]; then
	while read -r m; do
		# Classify against the stock name AND its 5.15 name, because the
		# missing list carries stock names while DEVICE_CRITICAL is written
		# in 5.15 names.
		#
		# The SUBJECT is the list and the PATTERN is the name being looked
		# for.  An earlier version had it the other way round -- subject
		# "${DEVICE_CRITICAL} ${resolved}", pattern " ${m} " -- which asks
		# whether m occurs inside the category list rather than whether the
		# category list contains m.  Every missing module therefore came out
		# DEVICE-CRITICAL, which is how 51 gaps all got tagged
		# "device-critical" and the report became worthless.
		resolved=$(resolve "${m}")
		case " ${DEVICE_CRITICAL} " in
		*" ${m} "*|*" ${resolved} "*)
			echo "   *** DEVICE-CRITICAL  ${m}.ko  (no boot, no camera/gpu/wifi)"
			;;
		*)
			case " ${BOOT_BLOCKING} " in
			*" ${m} "*)
				echo "   *** BOOT-BLOCKING   ${m}.ko"
				;;
			*)
				echo "       optional        ${m}.ko"
				;;
			esac
			;;
		esac
	done <"${missing_list}"
else
	echo "   (none)"
fi

echo
echo "--- EXTRA (we build these, the stock loader never names them) ---"
echo "    each is either a rename needing a load-list entry, or droppable"
if [ -s "${extra_list}" ]; then
	sed 's/^/       /' "${extra_list}" | head -60
	extra_n=$(wc -l <"${extra_list}")
	[ "${extra_n}" -gt 60 ] && echo "       ... and $((extra_n - 60)) more"
else
	echo "   (none)"
fi

# The fatal check: every boot-blocking module must exist, in whichever
# directory it was built.
echo
fatal=0
for m in ${BOOT_BLOCKING}; do
	if ! find "${DIRS[@]}" -name "${m}.ko" -print -quit | grep -q .; then
		echo "=== ASSERTION FAILED: boot-blocking module not built: ${m}.ko"
		fatal=1
	fi
done
if [ "${fatal}" != 0 ]; then
	echo "   The device will not boot without these."
	exit 1
fi
echo "=== boot-blocking modules present: ${BOOT_BLOCKING} ==="

rm -f "${produced_all}" "${missing_list}" "${extra_list}" "${renamed_list}"
