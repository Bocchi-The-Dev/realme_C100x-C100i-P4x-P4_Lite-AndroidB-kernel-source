#!/bin/bash
# ci/box-build.sh - build the kernel on a self-hosted box, mirroring CI exactly.
#
# WHY THIS EXISTS
# ---------------
# The GitHub-hosted runner costs ~22m40s per cycle, of which ~17m is the kernel
# Image and only ~2m is the vendor-module build that most changes actually touch.
# A self-hosted box with a persistent out/ turns a vendor-module change into a
# sub-minute rebuild, which matters when there are 61 missing modules and ~30
# failing units left to grind through.
#
# The box is the SAME SIZE as the hosted runner (4 vCPU, ~15-16 GB RAM), so this
# is not a speed win per build -- it is an incrementality win.  Nothing here is
# allowed to diverge from .github/workflows/main.yml, because a build that
# succeeds here and fails in CI (or worse, the reverse) is worse than no box at
# all.  Every build command below is copied from the workflow verbatim; if you
# change one, change the other.
#
# Usage:
#   ci/box-build.sh              # bootstrap if needed, then build everything
#   ci/box-build.sh config       # generate out/.config only (fast, ~1 min)
#   ci/box-build.sh image        # build the Image only
#   ci/box-build.sh modules      # in-tree + vendor modules (the fast loop)
#   ci/box-build.sh coverage     # coverage report + modules.load + depmod index
#
# Assumes: Ubuntu 24.04, curl, git, ~40 GB free, passwordless sudo.  Installs
#          the same apt packages as CI plus the LLVM toolchain.

set -uo pipefail

REPO_URL="https://github.com/Bocchi-The-Dev/realme_C100x-C100i-P4x-P4_Lite-AndroidB-kernel-source.git"
CLANG_URL="https://github.com/ravindu644/Android-Kernel-Tutorials/releases/download/toolchains/clang-r450784e.tar.gz"
CLANG_DIR="${CLANG_DIR:-/home/runner/kernel-toolchain}"
WORKDIR="${WORKDIR:-/home/runner/kernel515}"
STAGE="${1:-all}"

log() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------- bootstrap
# Idempotent: safe to run on a fresh box or an already-provisioned one.
ensure_deps() {
	# Copied from the workflow's "Install build dependencies" step.  Needed
	# because the box is a bare image: without libelf-dev the build dies early
	# and unhelpfully at
	#   tools/bpf/resolve_btfids/main.c: fatal error: 'libelf.h' file not found
	# which reads like a kernel source problem and is not one.
	local missing=()
	local p
	for p in flex bison bc cpio libssl-dev libelf-dev libncurses-dev \
	         zlib1g-dev libyaml-dev lz4 zstd device-tree-compiler \
	         python3 rsync kmod dwarves zip unzip; do
		dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p")
	done
	if [ "${#missing[@]}" -eq 0 ]; then
		echo "build dependencies already installed"
		return 0
	fi
	log "installing build dependencies: ${missing[*]}"
	sudo apt-get update -qq
	sudo apt-get install -y -qq "${missing[@]}"
}

ensure_toolchain() {
	if [ -x "${CLANG_DIR}/bin/clang" ] && [ -x "${CLANG_DIR}/bin/ld.lld" ] \
	   && [ -x "${CLANG_DIR}/bin/llvm-ar" ]; then
		echo "toolchain already present at ${CLANG_DIR}"
		return 0
	fi
	log "installing clang r450784e (matches CI byte-for-byte)"
	mkdir -p "${CLANG_DIR}"
	curl -sSL -o /tmp/clang.tar.gz "${CLANG_URL}"
	tar xzf /tmp/clang.tar.gz -C "${CLANG_DIR}"
	rm -f /tmp/clang.tar.gz
	# CI asserts clang and ld.lld exist after extraction; do the same, because
	# LLVM=1 silently falls back to a broken build without ld.lld.
	test -x "${CLANG_DIR}/bin/clang"    || { echo "clang missing";    exit 1; }
	test -x "${CLANG_DIR}/bin/ld.lld"   || { echo "ld.lld missing";   exit 1; }
	test -x "${CLANG_DIR}/bin/llvm-ar"  || { echo "llvm-ar missing";  exit 1; }
	test -x "${CLANG_DIR}/bin/llvm-nm"  || { echo "llvm-nm missing";  exit 1; }
	"${CLANG_DIR}/bin/clang" --version | head -1
}

ensure_clone() {
	if [ -d "${WORKDIR}/.git" ]; then
		echo "clone already present at ${WORKDIR}"
	else
		log "cloning"
		mkdir -p "$(dirname "${WORKDIR}")"
		git clone --depth 1 "${REPO_URL}" "${WORKDIR}"
	fi
}

# ------------------------------------------------------------------ stages
# Copied verbatim from .github/workflows/main.yml.
do_config() {
	mkdir -p out

	# UTS_RELEASE is derived from the git describe, so it embeds the HEAD
	# commit: 5.15.189-g<sha>.  Every commit therefore changes the release
	# string, and the release string is in both vmlinux (linux_banner) and every
	# module's vermagic.  kbuild has no idea: it rebuilds only what changed, so
	# an incremental build after a commit leaves modules carrying the PREVIOUS
	# release string alongside ones carrying the new one, and the kernel rejects
	# the stale ones with "version magic ... should be".
	#
	# That is not hypothetical.  It happened here: 178 of 371 modules were
	# unloadable after a commit-and-rebuild, caught by ci/check-vermagic.sh, with
	# modpost clean and coverage at 94/133.  Nothing else in the build noticed,
	# because a wrong vermagic is a perfectly valid module as far as every
	# build-time check is concerned.
	#
	# So: if the release string moved, the output tree is stale in a way that
	# cannot be repaired incrementally, and the only correct response is to
	# start over.  Within one commit, incremental builds stay fast.
	local want now have=""
	want="$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
	have="$(cat out/.nova-release 2>/dev/null || true)"
	if [ -n "$have" ] && [ "$have" != "$want" ]; then
		echo "release moved ${have} -> ${want}: purging out/ (stale vermagic)"
		rm -rf out
		mkdir -p out
	fi
	printf '%s' "$want" > out/.nova-release

	./scripts/kconfig/merge_config.sh -m -O out \
		arch/arm64/configs/gki_defconfig \
		arch/arm64/configs/sprd_gki_sharkl6.fragment \
		arch/arm64/configs/nova_ums9230.fragment
	make LLVM=1 LLVM_IAS=1 ARCH=arm64 O=out olddefconfig
	grep -E "CONFIG_ARM_SPRD_CPUFREQ_V2|CONFIG_ARCH_SPRD" out/.config
	grep -E "CONFIG_LTO_CLANG_THIN|CONFIG_LTO_CLANG_FULL|CONFIG_CFI_CLANG" out/.config
	log "board-critical config"
	for sym in UNISOC_DISP DRM_SPRD_DPU0 DRM_SPRD_DSI UNISOC_GSP \
		  ARM_SPRD_CPUFREQ_V2 TRUSTY_TUI TRUSTY_VIRTIO_IPC \
		  FUEL_GAUGE_SC27XX PSTORE_RAM PSTORE_CONSOLE PSTORE_PMSG \
		  PSTORE_DEFLATE_COMPRESS; do
		val=$(grep -E "^CONFIG_${sym}=" out/.config | sed -E "s/^CONFIG_[A-Z0-9_]+=//")
		printf '  %-24s =%s\n' "$sym" "${val:-<absent>}"
		case "$val" in
			y|m) ;;
			*) echo "CONFIG_${sym} did not resolve to y/m (got '${val:-absent}')"; return 1;;
		esac
	done
	echo "kernelrelease: $(make LLVM=1 ARCH=arm64 O=out -s kernelrelease)"
}

do_dtbs() {
	# Every dtbo-y list in arch/arm64/boot/dts/sprd/Makefile sits inside
	# 'ifeq ($(BSP_BUILD_DT_OVERLAY),y)'.  Neither this script nor
	# ci/build-vendor-modules.sh ever set it, so "make Image" produced ZERO
	# .dtbo files -- none of the 39 overlays this tree can build, including
	# ums9230-1h10_go-overlay.dtbo, whose compatible is the one matching this
	# board.
	#
	# It went unnoticed because CONFIG_OF=y, so device-tree support really is
	# compiled in, and because the board carries separate dtb_a/dtbo_a
	# partitions: the 5.15 kernel was booting on the STOCK 5.4 DTB with nothing
	# reporting a problem.  Cross-referencing our overlay against the live 5.4
	# device tree then showed the two are not interchangeable -- 13 of our 24
	# real nodes do not exist in the stock DTB at all.
	#
	# Running on the stock DTB is still defensible: it is what the 5.4 kernel
	# used, and the drivers we ship bind against it.  So this changes nothing
	# about what gets flashed.  What it fixes is that the overlays are now
	# actually built and therefore diffable, which is what made the
	# cross-reference possible in the first place.
	make LLVM=1 LLVM_IAS=1 ARCH=arm64 O=out \
		BSP_BUILD_FAMILY=qogirl6 \
		BSP_BUILD_DT_OVERLAY=y \
		BSP_BUILD_ANDROID_OS=y \
		-j"$(nproc)" dtbs
	echo "overlays built: $(find out/arch/arm64/boot/dts/sprd -name '*.dtbo' 2>/dev/null | wc -l)"
	ls -la out/arch/arm64/boot/dts/sprd/ums9230-1h10_go-overlay.dtbo 2>/dev/null \
		|| echo "WARNING: this board's overlay (ums9230-1h10_go) did not build"
}

do_image() {
	make LLVM=1 LLVM_IAS=1 ARCH=arm64 O=out BSP_BUILD_FAMILY=qogirl6 \
		-j"$(nproc)" Image
	ls -la out/arch/arm64/boot/Image
}

do_modules() {
	# The fast loop.  `make Image` builds zero =m modules, so this is the step
	# that makes the artifact set loadable at all.
	make LLVM=1 LLVM_IAS=1 ARCH=arm64 O=out -j"$(nproc)" modules
	echo "in-tree modules: $(find out -name '*.ko' -not -path 'out/vendor_modules/*' | wc -l)"

	KSRC="${WORKDIR}" \
	KBUILD="${WORKDIR}/out" \
	MODOUT="${WORKDIR}/out/vendor_modules" \
	JOBS="$(nproc)" \
	UNIT_TIMEOUT=300 \
		./ci/build-vendor-modules.sh
}

do_coverage() {
	mkdir -p pkg/modules-intree pkg/modules-vendor pkg/loadlist
	find out -name '*.ko' -not -path 'out/vendor_modules/*' \
		-exec cp --parents {} pkg/modules-intree/ \; 2>/dev/null || true
	find out/vendor_modules -name '*.ko' \
		-exec cp --parents {} pkg/modules-vendor/ \; 2>/dev/null || true
	find kernel_modules -name '*.ko' \
		-exec cp --parents {} pkg/modules-vendor/ \; 2>/dev/null || true
	# Assert first that every module will actually load.  A vermagic mismatch
	# is invisible to every other check here -- the modules compile, modpost is
	# clean, coverage is high -- and only shows up as 'version magic ... should
	# be' once the set is on the device.  Run before the coverage report so a
	# broken set cannot be reported as a good one.
	./ci/check-vermagic.sh out out/vendor_modules kernel_modules

	./ci/check-module-coverage.sh ci/modules.load.stock \
		pkg/modules-intree pkg/modules-vendor
	# Derive modules.load and the depmod index from what we actually built.
	KVER="$(make LLVM=1 ARCH=arm64 O=out -s kernelrelease 2>/dev/null | tr -d '+')"
	KVER="${KVER}" KBUILD_OUT="${WORKDIR}/out" \
		./ci/gen-modules-load.sh ci/modules.load.stock out/.config \
			pkg/loadlist pkg/modules-intree pkg/modules-vendor
}

# -------------------------------------------------------------------- main
ensure_deps
ensure_toolchain
ensure_clone
cd "${WORKDIR}" || exit 1
export PATH="${CLANG_DIR}/bin:${PATH}"

case "${STAGE}" in
	config)   do_config ;;
	image)    do_image ;;
	dtbs)     do_dtbs ;;
	modules)  do_modules ;;
	coverage) do_coverage ;;
	all)      do_config && do_dtbs && do_image && do_modules && do_coverage ;;
	*) echo "unknown stage '${STAGE}'"; exit 2 ;;
esac
