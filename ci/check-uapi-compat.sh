#!/bin/bash
# ci/check-uapi-compat.sh - compare our UAPI against a 5.4 reference tree.
#
# WHY
# ---
# We run the STOCK 5.4 vendor userspace (camera, Mali, display/GSP HALs) on our
# 5.15 kernel. Those HALs are prebuilt and were compiled against the 5.4
# kernel's headers, so every kernel-facing struct they pass across an ioctl must
# keep the 5.4 layout. Where it does not, the ioctl either changes number (if
# the encoding includes sizeof) or is rejected outright.
#
# This is not theoretical.  The GSP DRM interface had grown `char version[32]`
# in two structs, and because both ioctls are registered with DRM_IOCTL_DEF_DRV
# -- which derives the number from DRM_IOWR(cmd, struct), where _IOWR encodes
# sizeof(struct) -- adding 32 bytes silently changed the ioctl NUMBER. A 5.4 HAL
# would have been talking to a door that no longer existed, on the display path.
# It was invisible until the two trees were diffed.
#
# WHAT IT DOES
# ------------
# 1. Vendor-named UAPI present in both trees: report any that DIFFER.  These are
#    the interfaces the vendor HALs are built against.
# 2. Vendor-named UAPI present in 5.4 but MISSING here: a removed header is a
#    gap if anything still uses it, so report its users.
# 3. Files under include/linux that define ioctls and exist in both trees:
#    report differences, separating vendor-named files from mainline ones (mainline
#    drift between 5.4 and 5.15 is expected and not actionable).
#
# Mainstream UAPI is deliberately NOT diffed wholesale: ~320 of 878 headers
# differ between 5.4 and 5.15 for ordinary upstream reasons (virtio, netlink,
# bpf, tls) and reporting them would bury the handful that matter.
#
# Usage: ci/check-uapi-compat.sh <path-to-5.4-reference-tree>
# Exits 1 if any VENDOR interface differs, 0 otherwise.

set -uo pipefail

REF="${1:?usage: check-uapi-compat.sh <path-to-5.4-reference-tree>}"
OUR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

[ -d "${REF}/include/uapi" ] || { echo "not a kernel tree: ${REF}" >&2; exit 2; }
echo "=== UAPI compatibility vs 5.4 reference ==="
echo "  reference : ${REF}"
echo "  ours      : ${OUR}"

vendor_re='sprd|unisoc|spreadtrum|gsp_|trusty'

# Known-benign differences, each with the reason it cannot affect a 5.4 HAL.
# A gate that fails on differences nobody will fix is a gate that gets ignored,
# so benign ones are listed explicitly rather than tolerated wholesale.
#
#   gsp_lite_r4p0_cfg.h  copyright header text only; no struct or #define change
#   gsp_r9p0_cfg.h       real changes (an enum rename, struct field changes) but
#                        r9p0 is neither this board's silicon -- the live DT says
#                        sprd,gsp-r8p0-qogirl6 -- nor built: the GSP Makefile
#                        compiles gsp_r8p0 only
#   sprd_audiodsp_ioctl.h  5.15 only ADDS DSPLOG_CMD_SET_RD_TIMEOUT (command 12).
#                        Commands 0-11 keep their numbers, so a 5.4 HAL that
#                        never calls 12 is unaffected.
ALLOW_BENIGN="gsp_lite_r4p0_cfg.h gsp_r9p0_cfg.h sprd_audiodsp_ioctl.h"

# --- 1 & 2: vendor-named uapi, both trees --------------------------------
( cd "${REF}/include/uapi" && find . -name '*.h' ) | sort >/tmp/_u_ref.txt
( cd "${OUR}/include/uapi" && find . -name '*.h' ) | sort >/tmp/_u_our.txt

echo
echo "--- vendor UAPI present in BOTH trees ---"
both=0; bad=0
while read -r f; do
	grep -qE "$vendor_re" <<<"${f}" || continue
	# Skip anything not present here: it is reported by the missing-header
	# section below.  Without this a file can be reported as BOTH "differs" and
	# "missing", because diff -q fails on an absent file for the same reason it
	# fails on a differing one.
	[ -f "${OUR}/include/uapi/${f}" ] || continue
	both=$((both + 1))
	base=$(basename "${f}")
	if diff -q "${REF}/include/uapi/${f}" "${OUR}/include/uapi/${f}" >/dev/null 2>&1; then
		echo "  ok        ${f}"
	elif [[ " ${ALLOW_BENIGN} " == *" ${base} "* ]]; then
		echo "  ok*       ${f}   (known benign, see ALLOW_BENIGN)"
	else
		echo "  DIFFERS   ${f}   <-- vendor interface, userspace ABI at risk"
		bad=$((bad + 1))
	fi
done </tmp/_u_ref.txt
echo "  (${both} compared, ${bad} differing, benign ones marked ok*)"

echo
echo "--- vendor UAPI in 5.4 but MISSING here ---"
missing=0
while read -r f; do
	grep -qE "$vendor_re" <<<"${f}" || continue
	users=$(grep -rlF "$(basename "${f}")" "${OUR}/drivers" "${OUR}/include" 2>/dev/null | head -3)
	if [ -n "${users}" ]; then
		echo "  MISSING   ${f}  -- still referenced by:"
		echo "${users}" | sed 's/^/               /'
		missing=$((missing + 1))
	else
		echo "  absent    ${f}  (dead header, nothing references it -- fine)"
	fi
done < <(comm -23 /tmp/_u_ref.txt /tmp/_u_our.txt)

# --- 3: ioctl definitions under include/linux ----------------------------
echo
echo "--- include/linux files defining ioctls (both trees) ---"
for pair in "${REF}:ref" "${OUR}:our"; do
	d="${pair%:*}"; tag="${pair##*:}"
	( cd "${d}/include/linux" 2>/dev/null &&
	  grep -rlE "^[[:space:]]*#define[[:space:]]+\w+.*_IO[RW]*\(" . --include=*.h 2>/dev/null |
	  sed 's#^\./##' ) | sort -u >"/tmp/_io_${tag}.txt"
done
vend=0; main_=0
while read -r f; do
	grep -qxF "${f}" /tmp/_io_our.txt 2>/dev/null || continue
	diff -q "${REF}/include/linux/${f}" "${OUR}/include/linux/${f}" >/dev/null 2>&1 && continue
	if grep -qE "$vendor_re" <<<"${f}"; then
		echo "  DIFFERS   include/linux/${f}   <-- vendor ioctl definitions"
		vend=$((vend + 1))
	else
		echo "  mainline  include/linux/${f}   (expected 5.4->5.15 drift, not actionable)"
		main_=$((main_ + 1))
	fi
done < /tmp/_io_ref.txt
echo "  (${vend} vendor, ${main_} mainline differing)"

# --- verdict -------------------------------------------------------------
echo
if [ "${bad}" -gt 0 ] || [ "${vend}" -gt 0 ]; then
	echo "=== RESULT: ${bad} vendor uapi + ${vend} vendor ioctl header(s) differ."
	echo "    A 5.4-era HAL compiled against the reference tree will not match."
	echo "    Fix the layout, or confirm the interface is unused by the HALs."
	exit 1
fi
echo "=== RESULT: all vendor kernel-facing interfaces match the 5.4 reference."
echo "    (mainline drift aside, which does not affect vendor HALs)"
exit 0
