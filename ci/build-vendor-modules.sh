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
	local tag log built=0 name
	tag=$(echo "${d}" | tr '/' '_')
	log="${MODOUT}/${tag}.log"
	# The OUTPUT name, which is NOT the directory name.  For
	# kernel_modules/common/camera/core the directory is "core" but the module is
	# sprd_camera, because Kbuild sets "KO_MODULE_NAME := sprd_camera" and the
	# vendor Makefile stages to $(BSP_MODULES_OUT)/$(KO_MODULE_NAME).
	#
	# This is not a cosmetic detail.  The first version of this purge used
	# basename, so for that unit it deleted ${MODOUT}/core -- which does not exist
	# -- and left ${MODOUT}/sprd_camera alone.  A stale 35 MB sprd_camera.ko
	# therefore survived a failed rebuild, got packaged, and was counted as
	# COVERED by ci/check-module-coverage.sh while camera/core was sitting in the
	# FAILED list.  Two sources disagreeing is exactly the signal to chase.
	name=$(sed -n 's/^KO_MODULE_NAME[[:space:]]*[:?+]\{0,1\}=[[:space:]]*\([A-Za-z0-9_-]\+\).*/\1/p' \
		"${KSRC}/${d}/Kbuild" 2>/dev/null | head -1)
	[ -n "${name}" ] || name=$(basename "${d}")

	# Purge this unit's previous output BEFORE building.
	#
	# Without this, a .ko from an earlier successful attempt survives a later
	# failure and gets packaged anyway.  That is not hypothetical: a stale
	# sprd_camera.ko (35 MB, correct vermagic) left over from a manual
	# experiment made ci/check-module-coverage.sh report sprd_camera.ko as
	# COVERED while the unit was in fact failing, inflating the figure from 87 to
	# 88 covered and hiding a genuine device-critical gap.  A stale artifact that
	# reports success is the same failure mode as a swallowed error, just quieter.
	#
	# Three locations, because the three build mechanisms each write somewhere
	# different: the BSP_MODULES_OUT wrappers stage into ${MODOUT}/<name>, the
	# "all:" wrappers pass M=$(SRC) and so write beside the source, and the bare
	# kbuild fallback writes under the source tree too.
	rm -rf "${MODOUT:?}/${name}" 2>/dev/null
	# Recursive, not -maxdepth 1: mechanism 2 passes M=$(SRC) to kbuild, so the
	# .ko lands beside whichever source file it came from, which can be any depth
	# below the unit directory.
	find "${KSRC}/${d}" -name '*.ko' -delete 2>/dev/null

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
		# Purge AGAIN on failure, so a failed unit cannot leave a .ko behind.
		#
		# Purging only before the build is not enough.  It guarantees we do not
		# inherit a stale artifact, but it does not stop the build itself from
		# leaving one, and a .ko that survives a FAILED unit is worse than no
		# .ko at all: ci/check-module-coverage.sh reported sprd_camera.ko as
		# COVERED while camera/core was in the FAILED list of the same run,
		# because the packaging step globs the filesystem rather than consulting
		# the results.  Enforcing the invariant here means the filesystem can
		# never disagree with the build result, whatever the mechanism did.
		rm -rf "${MODOUT:?}/${name}" 2>/dev/null
		find "${KSRC}/${d}" -name '*.ko' -delete 2>/dev/null
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

# The camera ISP adapt layer.  cam_sys/Kbuild does
#   ADAPT_DIR := $(BSP_BOARD_CAMERA_MODULE_ISP_ADAPT_VERSION)
# and then includes -I$(KO_MODULE_PATH)/adpt/$(ADAPT_DIR)/inc.  Unset, that
# include path collapses to adpt//inc and the build dies with
#   isp_hw.h:17:10: fatal error: 'dcam_hw_adpt.h' file not found
# cam_sys/adpt/ ships qogirl6, qogirn6pro, qogirn6l, sharkl3, sharkl5 and
# sharkl5pro, and the Kbuild has an explicit branch for each.  qogirl6 is this
# board: the live device tree reports "Spreadtrum UMS9230 1H10 SoC" with
# compatible "sprd,ums9230", and every other GSP/DT signal in the tree points at
# the same SoC generation.
export BSP_BOARD_CAMERA_MODULE_ISP_ADAPT_VERSION=qogirl6

# The other two camera selectors, read off the VENDOR'S OWN BUILD PATHS embedded
# in the debug strings of the modules that demonstrably work on this hardware.
#
# Pulling /vendor/lib/modules/sprd_sensor.ko and sprd_cpp.ko off the device and
# running `strings` on them shows the __FILE__ paths the 5.4 modules were built
# from:
#
#   .../SCT606T_X6525_VERSION_BUILD/.../camera/sensor/csi2/sprd/receiver_r3p1/csi_driver.c
#   .../SCT606T_X6525_VERSION_BUILD/.../camera/cpp/cpp_lite/hw/lite_r6p0/cpp_hw.c
#   ... (also cpp_k_dma.c, cpp_k_rot.c, cpp_k_scale.c, and the lite_r6p0
#        "lite_r6p0" string appears on its own)
#
# So:
#   BSP_BOARD_CAMERA_MODULE_CSI_VERSION = receiver_r3p1
#   BSP_BOARD_CAMERA_MODULE_CPP_VERSION = lite_r6p0
#
# This is worth spelling out because neither value is derivable.  Both are
# compile-time selections of register-level code, chosen by the vendor's build
# system, and a wrong pick yields a module that builds and links cleanly and
# then programs the wrong registers -- a camera that comes up but does not
# work, with nothing in dmesg to explain it.  Earlier attempts to infer them
# both failed:
#
#   - By symbol fingerprint.  The live sprd_sensor.ko exports 37 csi_* symbols
#     and matching them against each candidate scored receiver_r3pX at 34/37
#     versus 18-29 for the r2p0 family.  That only narrows it to the r3pX
#     family: the four r3pX variants define IDENTICAL symbol names, so the test
#     cannot separate them, and the score is confounded anyway because the 5.4
#     driver is an older generation that would have fewer symbols regardless.
#   - From the device tree.  sprd/csi01 and sprd/csi02 carry sprd,ip-version
#     (bytes 00 00 02 00), but no candidate variant parses that property -- it
#     is not a DT-driven choice at all.
#   - logcat and dmesg name neither version.
#
# The build paths in the shipped modules are the vendor telling us directly.
# (Incidentally they also read SCT606T_X6525, which independently confirms the
# board is the X6525 -- see the swapped vendor/build.prop that made adb report
# model X6528.)
# BSP_BOARD_CAMERA_MODULE_ISP_VERSION is deliberately STILL UNSET.
#
# Candidates in the 5.15 tree:
#   dcam_if_r4p0_isp_r6p11   DCAM-IF r4p0 + ISP r6p11
#   dcam_r6p0_isp_r6p91      DCAM r6p0    + ISP r6p91
#
# Unlike its three siblings this one could not be pinned down, and the attempts
# are recorded so they are not repeated:
#
#  - Vendor build paths.  This is how the other three were settled, and it fails
#    here for a structural reason: the 5.4 module was built from
#      camera/core/isp2.6/adpt/qogirl6/
#    and the 5.15 tree reorganised that into the dcam_*_isp_* directories, so
#    the path does not name either candidate.  (It DID confirm adpt/qogirl6,
#    which is why ISP_ADAPT_VERSION=qogirl6 is solid.)
#  - Symbol fingerprint.  The four r3pX CSI variants proved incomparable because
#    they define identical names; the same problem does not apply here, but the
#    comparison is swamped by the 5.4-vs-5.15 generation gap.
#  - String literals.  Of the strings unique to each candidate, 3 appear in the
#    device's sprd_camera.ko for dcam_if and 0 for dcam_r6p0 -- far too few to
#    mean anything across a driver generation.
#  - Filename overlap with the 5.4 build: 6/21 for dcam_if, 4/21 for dcam_r6p0.
#  - Device tree.  It carries sprd,hwdvfs-dcam-if and sprd,hwdvfs-isp nodes, so
#    a DCAM-IF block exists, but no ISP revision property anywhere.
#  - dmesg and logcat name neither.
#
# The best available signal is the presence of sprd,hwdvfs-dcam-if, which only
# the dcam_if_* candidate is named for.  That is suggestive, not conclusive, and
# a wrong pick selects register-level ISP code, so it is left unset rather than
# guessed.  Note that choosing is not the blocker either way: BOTH candidates
# fail to build until ported, with different errors --
#   dcam_if_r4p0_isp_r6p11: implicit declaration of __flush_dcache_area
#                           (no longer exported to modules in 5.15)
#   dcam_r6p0_isp_r6p91:    cast to smaller integer type 'unsigned int' from
#                           'void *' (a 32/64-bit pointer bug)
# sprd_camera.ko is in the device's modules.load, so this is a real gap, but it
# is not boot-blocking: the phone reaches the launcher without a camera driver.
export BSP_BOARD_CAMERA_MODULE_CSI_VERSION=receiver_r3p1
export BSP_BOARD_CAMERA_MODULE_CPP_VERSION=lite_r6p0

export -f build_one
# RESULTS must be exported too: xargs runs build_one in a separate `bash -c`,
# and an unexported variable is empty there, so the per-unit result lines were
# being appended to "" ("No such file or directory" on stderr) and the counts
# at the end came out 0/0.
export KSRC KBUILD MODOUT UNIT_TIMEOUT RESULTS

cd "${KSRC}" || exit 1

# ---------------------------------------------------------------------------
# Build order and symbol staging.
#
# The camera units resolve their provider symbols through KBUILD_EXTRA_SYMBOLS,
# which modpost reads at LINK time.  A consumer built before its provider
# therefore fails on undefined symbols no matter how correct the source is, and
# the vendor Makefiles hardcode where they expect to find each symvers file:
#
#   KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/<module>/Module.symvers
#
# Two distinct problems, both handled here:
#
# 1. IN-TREE providers are not units at all.  sprd-ion and sprd-dmabuf are built
#    by `make modules` from drivers/, so their Module.symvers lands in out/ and
#    never in $(BSP_MODULES_OUT), where the vendor Makefiles look.  We copy it
#    across.
#
# 2. CONSUMERS need PROVIDERS built first.  The dependency graph, read off the
#    KBUILD_EXTRA_SYMBOLS lines in each Makefile:
#      common/camera/core    needs camsys_pw_domain, dmabuf, flash_drv, ion, sensor
#      common/camera/cam_sys needs camsys_pw_domain, dmabuf, flash_drv, ion, sensor
#      common/camera/sensor needs camera_pd, camsys_pw_domain
#      common/camera/cpp    needs camera, camsys_pw_domain, dmabuf, ion
#    so the order is camsys_pw_domain -> sensor -> camera/camsys -> cpp, and
#    sprd_camera_pd is required by sensor but has no Kbuild in this tree at all
#    (common/camera/power only builds sprd_camsys_pw_domain; the sprd_camera_pd
#    mention is a leftover comment).  An absent symvers file makes modpost abort
#    with "could not open MODULE.symvers", so we stage an empty one and SAY SO,
#    rather than either aborting the build or pretending the dependency is met.

# Providers that are built IN TREE by `make modules` rather than as units, so
# they have no $(BSP_MODULES_OUT) directory of their own and must be staged.
#
# In-tree modules do NOT get a per-directory Module.symvers: kbuild aggregates
# every in-tree module's exports into $(KBUILD)/Module.symvers.  An earlier
# version looked for out/drivers/.../Module.symvers, does not exist, and
# reported "cannot stage" for a module that had in fact been built.  So filter
# the aggregate instead: field 3 of a symvers line is the module path, e.g.
#   0xa88a8663<TAB>sprd_ion_map_kernel<TAB>drivers/staging/android/ion/sprd/sprd-ion<TAB>EXPORT_SYMBOL
# Filtering per module keeps the undefined-symbol check strict.  Handing over
# the whole aggregate would be easier and would also make the build pass, but it
# would let modpost resolve symbols from unrelated modules and so hide real
# ordering bugs.
STAGE_PROVIDERS="sprd-ion"

# Units that must be built before others, in this order.  Read off the
# KBUILD_EXTRA_SYMBOLS lines in the consumers' Makefiles:
#   dmabufheap    -> sprd-dmabuf            (provider for core, cam_sys, cpp)
#   flash_drv     -> sprd_flash_drv         (provider for core, cam_sys)
#   camera/power  -> sprd_camsys_pw_domain  (provider for sensor, core, cam_sys, cpp)
#   camera/sensor -> sprd_sensor            (provider for core, cam_sys)
#   camera/core   -> sprd_camera            (provider for cpp!)
#   camera/cam_sys-> sprd_camsys
#   camera/cpp    -> sprd_cpp
# Note cpp depends on sprd_camera, so camera/core must precede it.
ORDERED_UNITS="
kernel_modules/kernel5.15/dmabufheap
kernel_modules/common/camera/flash/flash_drv
kernel_modules/common/camera/power
kernel_modules/common/camera/sensor
kernel_modules/common/camera/core
kernel_modules/common/camera/cam_sys
kernel_modules/common/camera/cpp
"

stage_intree_symvers() {
	local m dst n aggregate
	aggregate="${KBUILD}/Module.symvers"
	for m in ${STAGE_PROVIDERS}; do
		dst="${MODOUT}/${m}"
		mkdir -p "${dst}"
		if [ ! -f "${aggregate}" ]; then
			echo "  !! ${aggregate} missing -- cannot stage ${m}"
			: >"${dst}/Module.symvers"
			continue
		fi
		# Field 3 is the module path; match the bare name at the end of it.
		awk -F'\t' -v m="${m}" '$3 == m || $3 ~ ("/" m "$")' \
			"${aggregate}" >"${dst}/Module.symvers"
		n=$(wc -l <"${dst}/Module.symvers")
		if [ "${n}" -gt 0 ]; then
			echo "  staged ${m}/Module.symvers (${n} exported symbols, from the in-tree aggregate)"
		else
			echo "  !! ${m} contributed no symbols to ${aggregate}"
			echo "     Is ${m}.ko actually built? Check: find out -name '${m}.ko'"
		fi
	done
}

# Log, loudly, any provider symvers the vendor Makefiles want that we still do
# not have.  An empty file keeps modpost from aborting; the message is the point.
report_missing_symvers() {
	local u name missing=0
	for u in ${ORDERED_UNITS}; do
		[ -f "${KSRC}/${u}/Makefile" ] || continue
		for name in $(grep -oE '\$\(BSP_MODULES_OUT\)/[A-Za-z0-9_-]+' \
			"${KSRC}/${u}/Makefile" 2>/dev/null | sed 's#.*/##' | sort -u); do
			if [ ! -f "${MODOUT}/${name}/Module.symvers" ]; then
				mkdir -p "${MODOUT}/${name}"
				: >"${MODOUT}/${name}/Module.symvers"
				echo "  !! ${u##*/}: no ${name}/Module.symvers -- staged EMPTY."
				echo "     If it reports undefined symbols, ${name} is a real gap."
				missing=$((missing + 1))
			fi
		done
	done
	[ "${missing}" -eq 0 ] && echo "  all provider Module.symvers present"
}

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

# Phase 1: the ordered chain, one at a time.  Sequential because each stage
# consumes the previous stage's Module.symvers, which modpost needs at link time.
# Parallelising this is what produced "undefined symbol" failures.
if [ -n "${ORDERED_UNITS}" ]; then
	log "phase 1: ordered units (provider symbols must exist first)"
	stage_intree_symvers
	for u in ${ORDERED_UNITS}; do
		# Only units we actually discovered; an excluded one is not a failure.
		found=0
		for d in "${UNITS[@]}"; do
			[ "${d}" = "${u}" ] && { found=1; break; }
		done
		if [ "${found}" = 1 ]; then
			echo "  ordered: ${u}"
			build_one "${u}"
		else
			echo "  ordered: ${u} -- not a discovered unit, skipping"
		fi
	done
fi

# Phase 2: everything else, in parallel as before.
log "phase 2: remaining units in parallel (parallel=${JOBS})"
mapfile -t REST < <(printf '%s\n' "${UNITS[@]}" | grep -vxF -f <(printf '%s\n' ${ORDERED_UNITS}))
if [ "${#REST[@]}" -gt 0 ]; then
	printf '%s\n' "${REST[@]}" \
		| xargs -P "${JOBS}" -I{} bash -c 'build_one "$1"' _ {}
fi

report_missing_symvers

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

# Final consistency check: the number of .ko must be consistent with the number
# of units reported ok.  Cheap, and it turns "the filesystem disagrees with the
# build" from a silent lie into a visible failure.
stale=0
for d in $(grep '^FAIL ' "${RESULTS}" | sed 's/^FAIL //'); do
	n=$(sed -n 's/^KO_MODULE_NAME[[:space:]]*[:?+]\{0,1\}=[[:space:]]*\([A-Za-z0-9_-]\+\).*/\1/p' \
		"${KSRC}/${d}/Kbuild" 2>/dev/null | head -1)
	[ -n "${n}" ] || n=$(basename "${d}")
	if [ -e "${MODOUT}/${n}.ko" ] || [ -d "${MODOUT}/${n}" ]; then
		echo "  !! FAILED unit ${d} still has output named ${n}"
		stale=$((stale + 1))
	fi
done
if [ "${stale}" -gt 0 ]; then
	echo "=== ${stale} failed unit(s) left output behind -- filesystem disagrees"
	echo "    with the build result.  This must be 0."
	exit 1
fi
echo "=== consistency: no failed unit left a .ko behind ==="

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
