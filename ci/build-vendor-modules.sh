#!/bin/bash
# Build the GKI 2.0 out-of-tree vendor modules under kernel_modules/.
#
# WHY THIS IS A SCRIPT AND NOT A LOOP IN THE WORKFLOW
# ---------------------------------------------------
# kernel_modules/<subsystem>/ (audio, display, gpu, input, touchscreen, wcn,
# video, vender, npu, nfc) is only a CONTAINER -- it has no Makefile, so
# `make -C kernel_modules/kernel5.15/audio` fails with "No targets specified
# and no makefile found".  The real build units are the leaf directories that
# contain a Kbuild.  The tree ships zero prebuilt .ko, and the vendor's own
# build_5.15.sh never touches kernel_modules, so these units are the only real
# source for the vendor module set.
#
# Each unit is built as a kbuild external module.  Where the vendor's wrapper
# Makefile exists we use it (it symlinks the Kbuild into
# BSP_MODULES_OUT/<name>/ and calls `make -C BSP_KERNEL_PATH M=... src=...
# modules`); otherwise we call kbuild directly.  Two mechanisms because the
# two disagree about how src= is threaded through, and shipping modules
# matters more than elegance.
#
# Units are built in PARALLEL.  There are 214 of them and each is a small
# external-module build, so doing them one at a time serialises ~214 kbuild
# invocations and turns this step into an hour of apparent silence.  A per
# unit timeout keeps one wedged build from stalling the whole job.
#
# Failures are counted and named.  An earlier version used
# `|| echo WARN` and reported a green step while producing zero modules.

set -uo pipefail

KSRC="${KSRC:?KSRC must be set}"
KBUILD="${KBUILD:?KBUILD must be set}"
MODOUT="${MODOUT:?MODOUT must be set}"
JOBS="${JOBS:-$(nproc)}"
UNIT_TIMEOUT="${UNIT_TIMEOUT:-300}"

rm -rf "${MODOUT}"
mkdir -p "${MODOUT}"
RESULTS="${MODOUT}/_results"

build_one() {
	local d="$1"
	local tag log built=0
	tag=$(echo "${d}" | tr '/' '_')
	log="${MODOUT}/${tag}.log"

	# Per-unit extra make variables.  The GPU needs one, and the reason is
	# specific: the vendor Makefile defaults CONFIG_MALI_PLATFORM_NAME to
	# "devicetree", but no UNISOC platform directory is called that.
	# gpu/sprd/natt/platform/ contains qogirl6, qogirn6l and qogirn6pro, and
	# gpu/sprd/gondul/platform/ contains only sharkl5Pro.  The live device tree
	# says soc/mm/gpu@23100000 compatible = "sprd,mali-natt" on a qogirl6 SoC,
	# so qogirl6 is the measured value, not a guess.
	local extra=()
	# NOTE: ${d} is the full path relative to the repo root, e.g.
	# "kernel_modules/kernel5.15/gpu/natt/mali" -- NOT "gpu/natt/mali".
	# An earlier version used the short form and silently matched nothing, so
	# CONFIG_MALI_PLATFORM_NAME was never passed and the GPU failed again with
	# the same missing-subdirectory error despite the fix being right.
	case "${d}" in
		*/gpu/natt/mali) extra=("CONFIG_MALI_PLATFORM_NAME=qogirl6") ;;
	esac

	# Three mechanisms, because the vendor wrappers in this tree disagree about
	# both the target name and the variables they read:
	#   1. wrappers with a "modules:" target reading BSP_KERNEL_PATH
	#   2. wrappers with an "all:" target reading KDIR/M   <- the GPU units
	#   3. bare kbuild, as a last resort
	# Mechanism 2 matters: gpu/*/mali/Makefile computes the CONFIG_MALI_*
	# defaults itself, and its own comment says "Dependency resolution is done
	# through statements as Kconfig is not supported for out-of-tree builds".
	# Reaching for bare kbuild instead skipped that, leaving
	# CONFIG_MALI_REAL_HW undefined, so the Kbuild's
	#   ifneq ($(CONFIG_MALI_REAL_HW),y)
	#       mali_gondul-y += backend/gpu/mali_kbase_model_linux.o
	# evaluated true and BOTH mali_kbase_irq_linux.o and mali_kbase_model_linux.o
	# were linked -- four duplicate symbols at the LTO link.
	if [ -f "${KSRC}/${d}/Makefile" ]; then
		if timeout "${UNIT_TIMEOUT}" make -C "${KSRC}/${d}" \
			BSP_KERNEL_PATH="${KBUILD}" BSP_MODULES_OUT="${MODOUT}" \
			ARCH=arm64 LLVM=1 LLVM_IAS=1 -j1 modules >"${log}" 2>&1; then
			built=1
		fi
		if [ "${built}" = 0 ] && grep -qE '^all:' "${KSRC}/${d}/Makefile"; then
			if timeout "${UNIT_TIMEOUT}" make -C "${KSRC}/${d}" \
				KDIR="${KBUILD}" M="${KSRC}/${d}" "${extra[@]}" \
				ARCH=arm64 LLVM=1 LLVM_IAS=1 -j1 all >>"${log}" 2>&1; then
				built=1
			fi
		fi
	fi
	if [ "${built}" = 0 ]; then
		if timeout "${UNIT_TIMEOUT}" make -C "${KBUILD}" \
			M="${KSRC}/${d}" src="${KSRC}/${d}" "${extra[@]}" \
			ARCH=arm64 LLVM=1 LLVM_IAS=1 -j1 modules >>"${log}" 2>&1; then
			built=1
		fi
	fi

	if [ "${built}" = 1 ]; then
		echo "OK ${d}" >>"${RESULTS}"
		echo "  ok      ${d}"
	else
		echo "FAIL ${d}" >>"${RESULTS}"
		echo "  FAILED  ${d}   (log: ${tag}.log)"
	fi
}
# The BSP_* variables the unit Kbuilds gate on.  Not optional: the vendor's own
# build system exports them, and without them the Kbuilds silently take the
# wrong branch.  Two of them caused real, essential failures:
#
#   BSP_KERNEL_VERSION=kernel5.15
#       wcn/wlan/wlan_combo gates its include path on this:
#         ifeq ($(strip $(BSP_KERNEL_VERSION)),kernel5.15)
#         KO_MODULE_PATH := $(src)
#       Unset, KO_MODULE_PATH stays empty, no -I is emitted, and
#       common/chip_ops.h dies with "fatal error: 'common/cmd.h' file not
#       found" even though the header is sitting in the source tree.
#
#   BSP_KERNEL_BUILD_CONFIG=build.config.gki.aarch64.ums9230_
#       Selects the per-SoC defines.  For audio/sprd/codec/sprd/sc2730/codec,
#       -DCONFIG_SND_SOC_UNISOC_CODEC_SC2730 is emitted ONLY under the
#       ums9230_ build config, so without it the codec compiles with no
#       configuration at all.  The trailing underscore is the vendor's padding
#       convention, not a typo.
#
# Deliberately NOT set, because a wrong value is worse than an unset one: it
# would select a DIFFERENT SoC's defines rather than simply selecting none.
#   BSP_DTBO, BSP_MODULE_DISP_VERSION, BSP_BOARD_NAME, BSP_MODULE_GPU_VERSION,
#   BSP_BOARD_CAMERA_MODULE_*, BSP_BOARD_PRODUCT_USING_VDSP.
#
# These MUST be real environment variables, not a bash array.  An earlier version
# of this script collected them into BSP_VARS=(...) and passed "${BSP_VARS[@]}"
# on the make command line.  bash does not export arrays through the
# environment, so inside the xargs subshells the array was empty, the expansion
# produced no arguments at all, and the variables silently never reached make.
# Verified with a standalone test: an exported array reads back as "count=0" in
# a child `bash -c`, while an exported scalar arrives intact.  Because the
# subshell is not running under `set -u`, the empty expansion was not even an
# error -- the build just ran with no BSP variables, which is why wlan_combo,
# wcn_bsp, bluetooth and fm all kept failing with their original errors.
# Exporting scalars means make also passes them down its own sub-makes, so the
# values reach the Kbuild at every depth without relying on MAKEFLAGS.
export BSP_KERNEL_VERSION=kernel5.15
export BSP_KERNEL_BUILD_CONFIG=build.config.gki.aarch64.ums9230_

export -f build_one
# RESULTS must be exported too: xargs runs build_one in a separate `bash -c`,
# and an unexported variable is empty there, so the per-unit result lines were
# being appended to "" ("No such file or directory" on stderr) and the counts
# at the end came out 0/0.
export KSRC KBUILD MODOUT UNIT_TIMEOUT RESULTS

cd "${KSRC}" || exit 1

# Every leaf directory holding a Kbuild is a build unit.  Exclude the
# display/dispc duplicate: it builds sprd-drm.ko from the same sources as the
# in-tree drivers/unisoc_platform/sprd_disp, which `make modules` already
# produced into modules-intree, so building both would ship two modules with
# the same name.
# UNITS_FILE lets you hand the script an explicit list of units, which is how a
# single unit gets exercised through the REAL builder -- per-unit overrides, all
# three build mechanisms, the BSP vars and the counting -- instead of through a
# hand-rolled make invocation that proves only that the source compiles.
if [ -n "${UNITS_FILE:-}" ]; then
	mapfile -t UNITS <"${UNITS_FILE}"
else
	mapfile -t UNITS < <(find kernel_modules -name Kbuild -printf '%h\n' \
		| sort -u | grep -v '/display/dispc$' | grep -v '/mali/csf/ipa_control$' \
		| grep -v '/gpu/midgard/mali$' | grep -v '/gpu/gondul/mali$' \
		| grep -v '/gpu/natt/mali/csf$')
fi

echo "discovered ${#UNITS[@]} external module units (parallel=${JOBS}, timeout=${UNIT_TIMEOUT}s each)"
echo "skipping kernel_modules/kernel5.15/display/dispc -- duplicate of in-tree sprd-drm"
echo "skipping */mali/csf/ipa_control -- a Kbuild fragment included by its parent csf/Kbuild, not a unit"
echo "skipping gpu/{midgard,gondul}/mali -- platform dirs are pike2/sharkle and sharkl5Pro;"
echo "  this board is qogirl6 and its GPU is natt (DT: sprd,mali-natt)"

printf '%s\n' "${UNITS[@]}" \
	| xargs -P "${JOBS}" -I{} bash -c 'build_one "$1"' _ {}

# NOTE: no "|| echo 0" here.  grep -c prints 0 AND exits 1 when there is no
# match, so the || would append a second 0 and make the variable "0\n0", which
# then blows up the $(( )) arithmetic below.  The assignment captures grep's
# stdout regardless of its exit status, so ${x:-0} is the correct default.
ok_count=$(grep -c '^OK ' "${RESULTS}" 2>/dev/null); ok_count=${ok_count:-0}
fail_count=$(grep -c '^FAIL ' "${RESULTS}" 2>/dev/null); fail_count=${fail_count:-0}
echo "=== vendor module units built OK : ${ok_count} / $((ok_count + fail_count)) ==="
echo "=== vendor module units FAILED   : ${fail_count} ==="
if [ "${fail_count}" -gt 0 ]; then
	echo "--- failed units ---"
	grep '^FAIL ' "${RESULTS}" | sed 's/^FAIL /   /'
	echo "--- first error line of each failed unit's log ---"
	grep '^FAIL ' "${RESULTS}" | sed 's/^FAIL //' | while read -r d; do
		tag=$(echo "${d}" | tr '/' '_')
		echo "  [${d}]"
		grep -m3 -E "error:|Error [0-9]|No such file|fatal:" \
			"${MODOUT}/${tag}.log" 2>/dev/null | sed 's/^/      /'
	done
fi
# Where the .ko files actually are.  Mechanism 1 (the BSP_MODULES_OUT wrappers)
# stages them under ${MODOUT}, but mechanism 2 (the "all:" wrappers, e.g. the
# GPU) passes M=$(SRC) to kbuild and so writes the module NEXT TO THE SOURCE.
# Counting only ${MODOUT} reported ".ko produced: 0" for a unit that had just
# built mali_kbase.ko successfully, and the same blind spot made the
# board-critical assertion below miss it.  The packaging step already globs both
# (find kernel_modules -name '*.ko'); this makes the script agree.
produced=$( { find "${MODOUT}" -name '*.ko'; find "${KSRC}/kernel_modules" -name '*.ko'; } | wc -l)
echo "=== .ko produced: ${produced} ==="

# Board-critical assertion.
#
# A wall of "FAILED" lines is not a verdict: most of the 50 failures are other
# panels (focaltech, himax), other GPU generations (midgard, natt), other PMICs
# (sc2721) and other SoCs' camera flash ICs, none of which this board has.  The
# check that matters is whether the modules the X6525 actually loads got built.
# The list below was derived by intersecting the failing units against the
# device's own /proc/modules and /vendor/lib/modules over adb, so it is measured
# rather than guessed.
CRITICAL_MODULES="sprd_wlan_combo mali_kbase snd-soc-sprd-codec-sc2730 sprdbt_tty sprd_fm"
missing_critical=""
for m in ${CRITICAL_MODULES}; do
	if [ -z "$( { find "${MODOUT}" -name "${m}.ko"; find "${KSRC}/kernel_modules" -name "${m}.ko"; } | head -1)" ]; then
		missing_critical="${missing_critical} ${m}.ko"
	fi
done
if [ -n "${missing_critical}" ]; then
	echo "=== ASSERTION FAILED: board-critical modules not built:"
	echo "   ${missing_critical}"
	echo "   These are loaded by the X6525.  The kernel will not boot without"
	echo "   them, so this is a hard failure, not a warning."
	exit 1
fi
echo "=== board-critical modules present: ${CRITICAL_MODULES} ==="
