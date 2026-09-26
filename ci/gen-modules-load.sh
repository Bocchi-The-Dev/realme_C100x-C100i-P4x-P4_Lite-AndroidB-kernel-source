#!/bin/bash
# ci/gen-modules-load.sh - generate a modules.load for the modules we actually build.
#
# WHY
# ---
# The stock /vendor/lib/modules/modules.load names 133 modules in insmod order,
# but those are 5.4 modules: vermagic
# "5.4.210-android12-9-667269-g856cdf2f247e-ab665 ... modversions" with
# CONFIG_MODVERSIONS=y, so their symbol CRCs belong to that kernel and they
# cannot load against a 5.15 Image under any circumstances.  Replacing
# /lib/modules is therefore mandatory, which makes the rewritten load list a
# deliverable rather than an afterthought.
#
# It cannot be the stock list with lines deleted, because the set of modules we
# build is not a subset of the stock set: the tree renames some of them, omits
# others entirely, and builds a good number the stock loader never named.  So the
# list is derived:
#
#   1. SEED.  Walk the stock list in order, mapping each name through
#      ci/module-aliases.stock515 and keeping the ones we actually build.
#   2. CLOSE.  Transitively add any module we build that a kept module depends
#      on (modinfo -F depends).  This is what pulls in modules the stock loader
#      never named but that our build needs, and it is also the safety net: if a
#      provider is only reachable as a dependency, it still gets loaded first.
#   3. ORDER.  Topologically sort so every module follows its dependencies.
#      Ties broken by stock order, so the output stays as close to the proven
#      order as the dependency graph allows.
#
# Deliberately NOT included: our extra modules that nothing depends on.  The
# stock loader never loaded them, and adding a second driver for a device the
# stock configuration does not claim is how you get two drivers fighting over
# one /dev node.  They are listed in the report for review instead.
#
# Usage: ci/gen-modules-load.sh <stock-modules.load> <config> <outdir> <moduledir> [moduledir ...]
# Writes <outdir>/modules.load plus <outdir>/modules-load.report.txt.

set -uo pipefail

STOCK="${1:?usage: gen-modules-load.sh <stock> <.config> <outdir> <moduledir>...}"
CONFIG="${2:?missing .config}"
OUTDIR="${3:?missing outdir}"
shift 3
DIRS=("$@")
[ "${#DIRS[@]}" -gt 0 ] || { echo "no module directories given" >&2; exit 2; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ALIASES="${HERE}/module-aliases.stock515"
REPORT="${OUTDIR}/modules-load.report.txt"
mkdir -p "${OUTDIR}"
# KVER (kernelrelease) comes from the ENVIRONMENT, not a positional argument:
# every positional from $4 onwards is a module directory, so a version passed
# as $5 was silently consumed as one and the depmod stage never ran.  Only the
# depmod staging tree's name depends on it, so skipping it is harmless apart
# from losing modules.dep/alias/symbols.
[ -n "${KVER:-}" ] && echo "${KVER}" >"${OUTDIR}/.kver"

# Modules that exist only in the Transsion value-add layer.  Nothing in any
# 5.15 tree provides them, and they are not hardware drivers -- they are
# Transsion's own telemetry, gesture and tuning helpers.  Dropping them costs
# nothing that a stock user would notice.
TRANSSION_RE='^(tran_|transsion_|tnek$|switch_class$|sysdumpdb_switch$|turbo_engine$|modules_interface$)'

# ---------------------------------------------------------------- our set
declare -A HAVE          # module name -> path
declare -A DEP           # module name -> space-separated deps
declare -A STOCKPOS      # stock name -> its line number, for stable ordering

while IFS= read -r ko; do
	name="$(basename "${ko}" .ko)"
	HAVE["${name}"]="${ko}"
	DEP["${name}"]="$(modinfo -F depends "${ko}" 2>/dev/null | tr ',' ' ')"
done < <(for d in "${DIRS[@]}"; do find "${d}" -name '*.ko' 2>/dev/null; done)

resolve() {
	local n="$1" m
	[ -f "${ALIASES}" ] || { echo "${n}"; return; }
	m="$(grep -E "^${n}:" "${ALIASES}" 2>/dev/null | head -1 | cut -d: -f2)"
	[ -n "${m}" ] && echo "${m}" || echo "${n}"
}

# Is this absent stock module actually compiled into vmlinux?  If so its
# modules.load line must be dropped: there is no .ko to load, but the
# functionality IS present, so it is not a lost feature.
#
# This is an explicit table, not a heuristic, and every entry was verified
# against out/.config by hand.  Two earlier attempts at inferring this were
# wrong in ways worth recording:
#
#   * Returning the first =y CONFIG_CRYPTO_* symbol found.  That classified ANY
#     absent module as built-in the moment CONFIG_CRYPTO_AES=y, so sprd_camera
#     was reported as "present in vmlinux" -- a wrong answer delivered
#     confidently, which is worse than no answer.
#   * Matching the first token of the module name.  Every vendor module starts
#     "sprd_", so the token was SPRD, and CONFIG_SPRD_* is full of =y symbols.
#     21 modules came out "built-in" when only 10 are.
#
# So: a fixed mapping, each line checked against the config, and anything not
# listed is reported as an honest gap rather than guessed at.  If a config
# symbol here is ever not =y, the report says so instead of quietly agreeing.
BUILTIN_MAP="
sha1-ce:CONFIG_CRYPTO_SHA1
ghash-ce:CONFIG_CRYPTO_GHASH_ARM64_CE
aes-ce-ccm:CONFIG_CRYPTO_CCM
aes-neon-blk:CONFIG_CRYPTO_AES_ARM64_CE_BLK
arc4:CONFIG_CRYPTO_LIB_ARC4
ax88179_178a:CONFIG_USB_NET_AX88179_178A
sprd_usbpinmux_qogirl6:CONFIG_SPRD_USBPINMUX_QOGIRL6_CTRL
"

builtin_symbol() {
	local n="$1" line mod sym val
	while read -r line; do
		[ -n "${line}" ] || continue
		mod="${line%%:*}"
		sym="${line##*:}"
		[ "${mod}" = "${n}" ] || continue
		if [ -r "${CONFIG}" ] && grep -qE "^${sym}=y$" "${CONFIG}"; then
			echo "${sym}=y"
			return 0
		fi
		# Listed but not enabled: the module really is missing.
		return 1
	done <<<"${BUILTIN_MAP}"
	return 1
}

# ---------------------------------------------------- 1. seed from stock
declare -A WANT             # our name -> stock position
declare -A RENAME           # our name -> stock name it satisfies
i=0
dropped_builtin=0; dropped_transsion=0; dropped_other=0
: >"${OUTDIR}/.dropped"
while IFS= read -r raw; do
	i=$((i + 1))
	[ -n "${raw}" ] || continue
	stock="${raw%.ko}"
	STOCKPOS["${stock}"]="${i}"
	ours="$(resolve "${stock}")"
	if [ -n "${HAVE[${ours}]+set}" ]; then
		WANT["${ours}"]="${i}"
		[ "${ours}" != "${stock}" ] && RENAME["${ours}"]="${stock}"
	elif [[ "${stock}" =~ ${TRANSSION_RE} ]]; then
		echo "transsion|${stock}" >>"${OUTDIR}/.dropped"
		dropped_transsion=$((dropped_transsion + 1))
	elif [ -n "$(builtin_symbol "${stock}")" ]; then
		echo "builtin|${stock}" >>"${OUTDIR}/.dropped"
		dropped_builtin=$((dropped_builtin + 1))
	else
		echo "absent|${stock}" >>"${OUTDIR}/.dropped"
		dropped_other=$((dropped_other + 1))
	fi
done <"${STOCK}"

# --------------------------------------------- 2. close over dependencies
added_dep=0
changed=1
while [ "${changed}" = 1 ]; do
	changed=0
	for m in "${!WANT[@]}"; do
		for d in ${DEP[${m}]:-}; do
			[ -n "${d}" ] || continue
			[ -n "${HAVE[${d}]+set}" ] || continue
			if [ -z "${WANT[${d}]+set}" ]; then
				# Not named by the stock loader at all, but our build needs
				# it, so it must be loaded first.  Give it the seed module's
				# position minus one so the sort keeps it ahead.
				WANT["${d}"]=$(( ${WANT[${m}]} - 1 ))
				added_dep=$((added_dep + 1))
				changed=1
			fi
		done
	done
done

# -------------------------------------------------------- 3. order (deps first)
ordered=""
placed=""
pending="$(printf '%s\n' "${!WANT[@]}" | tr ' ' '\n')"
# stable: sort keys are the stock position, then name
for pass in $(seq 1 40); do
	remaining=""
	progress=0
	for m in $(printf '%s\n' "${!WANT[@]}" | while read -r x; do
			printf '%09d\t%s\n' "${WANT[${x}]}" "${x}"; done | sort -k1,1 -k2,2 | cut -f2); do
		case " ${placed} " in *" ${m} "*) continue ;; esac
		ok=1
		for d in ${DEP[${m}]:-}; do
			[ -n "${d}" ] || continue
			[ -n "${HAVE[${d}]+set}" ] || continue
			[ -n "${WANT[${d}]+set}" ] || continue
			case " ${placed} " in *" ${d} "*) ;; *) ok=0 ;; esac
		done
		if [ "${ok}" = 1 ]; then
			ordered="${ordered}${m}"$'\n'
			placed="${placed} ${m} "
			progress=1
		else
			remaining="${remaining}${m}"$'\n'
		fi
	done
	[ "${progress}" = 0 ] && break
done
# Anything left is in a dependency cycle.  Emit it in stock order rather than
# silently dropping it, and say so -- a cycle means the Kbuilds disagree about
# who provides what, which is worth knowing.
cycle=""
if [ -n "${remaining}" ]; then
	cycle="$(printf '%s' "${remaining}" | while read -r x; do
		[ -n "${x}" ] && printf '%09d\t%s\n' "${WANT[${x}]}" "${x}"; done |
		sort -k1,1 -k2,2 | cut -f2 | tr '\n' ' ')"
fi

# ------------------------------------------------------------- write output
{
	printf '%s\n' "${ordered}" | sed '/^$/d' | sed 's/$/.ko/'
	if [ -n "${cycle}" ]; then
		for m in ${cycle}; do echo "${m}.ko"; done
	fi
} >"${OUTDIR}/modules.load"

# extras we build that the load list does not mention
extras=0
: >"${OUTDIR}/.extras"
for m in "${!HAVE[@]}"; do
	[ -n "${WANT[${m}]+set}" ] && continue
	echo "${m}" >>"${OUTDIR}/.extras"
	extras=$((extras + 1))
done

{
	echo "=== modules.load generation report ==="
	echo "  stock entries      : $(grep -c . "${STOCK}")"
	echo "  modules we build   : ${#HAVE[@]}"
	echo "  in modules.load     : $(grep -c . "${OUTDIR}/modules.load")"
	echo
	echo "  seeded from stock   : $(( ${#WANT[@]} - added_dep ))"
	echo "  added as dependency: ${added_dep}"
	echo "  renamed             : ${#RENAME[@]}"
	[ "${#RENAME[@]}" -gt 0 ] && for m in "${!RENAME[@]}"; do
		echo "      ${RENAME[${m}]}.ko -> ${m}.ko"
	done
	echo
	echo "  dropped, built into vmlinux (no .ko to load, functionality present): ${dropped_builtin}"
	grep '^builtin|' "${OUTDIR}/.dropped" | cut -d'|' -f2 | sed 's/^/      /'
	echo "  dropped, Transsion value-add (absent from every 5.15 tree): ${dropped_transsion}"
	grep '^transsion|' "${OUTDIR}/.dropped" | cut -d'|' -f2 | sed 's/^/      /'
	echo "  dropped, ABSENT with no built-in equivalent (real feature gaps): ${dropped_other}"
	grep '^absent|' "${OUTDIR}/.dropped" | cut -d'|' -f2 | sed 's/^/      /'
	echo
	echo "  built but deliberately NOT in modules.load: ${extras}"
	echo "    (not named by the stock loader and not required as a dependency;"
	echo "     adding a second driver for a device the stock config does not"
	echo "     claim is how two drivers end up fighting over one /dev node)"
	grep -c . "${OUTDIR}/.extras" >/dev/null && sort "${OUTDIR}/.extras" | sed 's/^/      /' | head -40
	[ "${extras}" -gt 40 ] && echo "      ... and $((extras - 40)) more"
	if [ -n "${cycle}" ]; then
		echo
		echo "  WARNING: dependency cycle among: ${cycle}"
		echo "    Emitted in stock order.  A cycle means two Kbuilds each claim"
		echo "    to provide something the other provides."
	fi
} >"${REPORT}"

# ------------------------------------------------- companion index files
# A vendor replacement needs more than modules.load.  modprobe and depmod-based
# paths read modules.dep, modules.alias and modules.symbols, and the stock
# /lib/modules ships all of them, so regenerate rather than leave stale ones
# pointing at 5.4 modules.
#
# depmod insists on <basedir>/lib/modules/<uname -r>/, and the modules are
# spread across two trees with nested paths, so stage them as symlinks under
# that one directory.  modules.order and modules.builtin are copied from the
# kernel build so depmod does not warn and so builtin modules are excluded from
# modules.dep -- which is what keeps the built-in entries in the drop report
# consistent with what depmod believes.
KVER="$(cat "${OUTDIR}/.kver" 2>/dev/null || true)"
if [ -n "${KVER}" ] && command -v depmod >/dev/null 2>&1; then
	D="${OUTDIR}/.depmod/lib/modules/${KVER}"
	rm -rf "${OUTDIR}/.depmod"; mkdir -p "${D}"
	for f in "${!HAVE[@]}"; do
		[ -e "${D}/${f}.ko" ] || ln -s "$(readlink -f "${HAVE[$f]}")" "${D}/${f}.ko"
	done
	# KBUILD_OUT is where the kernel build tree is.  Guessing it relative to
	# OUTDIR is how the first version silently did nothing: ${OUTDIR}/../../out
	# resolves to pkg/out, which does not exist, so the copy was skipped and
	# depmod was left warning about the three files.
	for extra in modules.order modules.builtin modules.builtin.modinfo; do
		[ -n "${KBUILD_OUT:-}" ] && [ -r "${KBUILD_OUT}/${extra}" ] &&
			cp -f "${KBUILD_OUT}/${extra}" "${D}/" 2>/dev/null
	done
	depmod -b "${OUTDIR}/.depmod" "${KVER}" 2>/dev/null
	for f in modules.dep modules.alias modules.symbols modules.softdep; do
		[ -r "${D}/${f}" ] && cp -f "${D}/${f}" "${OUTDIR}/" && echo "  also wrote ${f}"
	done
	rm -rf "${OUTDIR}/.depmod"
fi

rm -f "${OUTDIR}/.dropped" "${OUTDIR}/.extras" "${OUTDIR}/.kver"
cat "${REPORT}"
