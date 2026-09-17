#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# PROVE that the installed applet is discoverable after a fresh login, without
# relying on whatever QT_PLUGIN_PATH the current session happens to carry.
#
# How the proof works
#   * scripts/qtplugin-probe.cpp resolves an applet id with
#     KPluginMetaData::findPluginById("plasma/applets", id) -- the exact call
#     Plasma::PluginLoader makes. It only sees QCoreApplication::libraryPaths().
#   * The probe is run inside `env -i` with the minimal variable set a systemd
#     user manager has at login, then systemd's OWN generator
#     (30-systemd-environment-d-generator) is executed and its output imported,
#     exactly as the user manager does at login. No QT_PLUGIN_PATH is inherited
#     from this shell.
#   * A bogus applet id is probed the same way and MUST fail, so a probe that
#     succeeds for everything cannot pass this script.
#
# Usage:
#   scripts/verify-install.sh                 verify the default applet id
#   scripts/verify-install.sh --id ID         verify another applet id
#   scripts/verify-install.sh --keep          keep the temp dir (for debugging)
#
# Exit status: 0 = all assertions held, 1 = the install is not discoverable.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

APPLET_ID="io.github.mrajster.obsidiannote"
BOGUS_ID="io.github.mrajster.obsidiannote.definitely-not-installed"
KEEP=0

usage() { sed -n '4,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;34m==>\033[0m \033[1m%s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
ok()   { printf '    \033[1;32mPASS\033[0m %s\n' "$*"; }
bad()  { printf '    \033[1;31mFAIL\033[0m %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --id)   shift; [[ $# -gt 0 ]] || die "--id needs an argument"; APPLET_ID="$1" ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

command -v g++ >/dev/null 2>&1        || die "'g++' not found -- cannot build the probe"
command -v pkg-config >/dev/null 2>&1 || die "'pkg-config' not found"

GEN=""
for cand in /usr/lib/systemd/user-environment-generators/30-systemd-environment-d-generator \
            /lib/systemd/user-environment-generators/30-systemd-environment-d-generator \
            /usr/lib/systemd/user-environment-generators/*environment-d*; do
    if [[ -x "${cand}" ]]; then GEN="${cand}"; break; fi
done
[[ -n "${GEN}" ]] || die "systemd-environment-d-generator not found -- cannot simulate a login environment"

TMPDIR_RUN="$(mktemp -d -t obsidiannote-verify-XXXXXX)"
cleanup() { [[ ${KEEP} -eq 1 ]] || rm -rf "${TMPDIR_RUN}"; }
trap cleanup EXIT

step "Building the discovery probe"
PROBE="${TMPDIR_RUN}/qtplugin-probe"
# pkg-config emits several words; keep them as separate argv entries.
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core)"
g++ -std=c++20 -fPIC -O1 -o "${PROBE}" "${SRC_DIR}/scripts/qtplugin-probe.cpp" \
    "${qt_flags[@]}" \
    -I/usr/include/KF6 -I/usr/include/KF6/KCoreAddons -lKF6CoreAddons \
    || die "probe build failed (need qt6-base + kcoreaddons development headers)"
info "probe   : ${PROBE}"
info "generator: ${GEN}"

# The runner executed inside the pristine environment. It imports the systemd
# environment.d generator output the same way the user manager does, then runs
# the probe. Nothing else is inherited.
RUNNER="${TMPDIR_RUN}/clean-login-run.sh"
cat > "${RUNNER}" <<'RUNNER_EOF'
#!/usr/bin/env bash
set -uo pipefail
gen="$1"; probe="$2"; applet="$3"; import="$4"
if [[ "${import}" == "import" ]]; then
    while IFS= read -r line; do
        [[ "${line}" == *=* ]] || continue
        export "${line%%=*}=${line#*=}"
    done < <("${gen}")
fi
"${probe}" "${applet}"
RUNNER_EOF
chmod +x "${RUNNER}"

# The variable set a systemd --user manager actually starts with at login.
clean_run() { # clean_run <import|noimport> <applet-id>
    env -i \
        HOME="${HOME}" \
        USER="${USER:-$(id -un)}" \
        LOGNAME="${LOGNAME:-$(id -un)}" \
        SHELL=/bin/sh \
        PATH=/usr/local/bin:/usr/bin:/bin \
        XDG_RUNTIME_DIR="/run/user/$(id -u)" \
        LANG="${LANG:-C.UTF-8}" \
        /usr/bin/env bash "${RUNNER}" "${GEN}" "${PROBE}" "$2" "$1"
}

failures=0

step "Control A -- pristine login env, environment.d NOT applied"
info "(this is what the machine looked like before the fix; expected: NOT-FOUND)"
if clean_run noimport "${APPLET_ID}"; then
    bad "applet resolved without any QT_PLUGIN_PATH -- Qt's default search path already covers it"
    info "that is fine for a system-wide install; skipping the drop-in assertions"
    ok  "system-wide install is discoverable out of the box"
    SYSTEM_WIDE=1
else
    ok  "not found without environment.d, as expected"
    SYSTEM_WIDE=0
fi

step "Test 1 -- pristine login env + systemd environment.d generator"
info "(this is exactly what plasma-plasmashell.service inherits after a fresh login)"
if clean_run import "${APPLET_ID}"; then
    ok "applet FOUND after a simulated fresh login"
else
    bad "applet NOT found after a simulated fresh login -- the install is not persistent"
    failures=$((failures + 1))
fi

step "Test 2 -- differential control, bogus applet id"
info "(must fail; otherwise Test 1 proves nothing)"
if clean_run import "${BOGUS_ID}"; then
    bad "bogus id '${BOGUS_ID}' resolved -- the probe is vacuous"
    failures=$((failures + 1))
else
    ok "bogus id correctly NOT found"
fi

step "Test 3 -- environment.d append semantics"
info "(a pre-existing QT_PLUGIN_PATH must be preserved, not clobbered)"
pre_existing="/nonexistent/pre-existing/plugin/dir"
out="$(env -i HOME="${HOME}" USER="${USER:-$(id -un)}" PATH=/usr/bin:/bin \
        QT_PLUGIN_PATH="${pre_existing}" "${GEN}" | grep '^QT_PLUGIN_PATH=' || true)"
if [[ ${SYSTEM_WIDE} -eq 1 && -z "${out}" ]]; then
    ok "no drop-in installed (system-wide route) -- nothing to append to"
elif [[ "${out}" == *":${pre_existing}"* ]]; then
    ok "generator output: ${out}"
else
    bad "pre-existing QT_PLUGIN_PATH was clobbered; generator said: ${out:-<nothing>}"
    failures=$((failures + 1))
fi

step "Summary"
if [[ ${failures} -eq 0 ]]; then
    ok "install is discoverable after a fresh login (${failures} failures)"
    exit 0
fi
bad "${failures} assertion(s) failed"
exit 1
