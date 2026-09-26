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

	if [ -f "${KSRC}/${d}/Makefile" ]; then
		if timeout "${UNIT_TIMEOUT}" make -C "${KSRC}/${d}" \
			BSP_KERNEL_PATH="${KBUILD}" BSP_MODULES_OUT="${MODOUT}" \
			"${BSP_VARS[@]}" \
			ARCH=arm64 LLVM=1 LLVM_IAS=1 -j1 modules >"${log}" 2>&1; then
			built=1
		fi
	fi
	if [ "${built}" = 0 ]; then
		if timeout "${UNIT_TIMEOUT}" make -C "${KBUILD}" \
			M="${KSRC}/${d}" src="${KSRC}/${d}" \
			"${BSP_VARS[@]}" \
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
BSP_VARS=(
	"BSP_KERNEL_VERSION=kernel5.15"
	"BSP_KERNEL_BUILD_CONFIG=build.config.gki.aarch64.ums9230_"
)
export BSP_VARS

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
mapfile -t UNITS < <(find kernel_modules -name Kbuild -printf '%h\n' \
	| sort -u | grep -v '/display/dispc$')

echo "discovered ${#UNITS[@]} external module units (parallel=${JOBS}, timeout=${UNIT_TIMEOUT}s each)"
echo "skipping kernel_modules/kernel5.15/display/dispc -- duplicate of in-tree sprd-drm"

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
echo "=== .ko produced: $(find "${MODOUT}" -name '*.ko' | wc -l) ==="
