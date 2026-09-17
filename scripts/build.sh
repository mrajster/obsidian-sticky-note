#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Configure, build, test, install and VERIFY the "Obsidian Note" Plasma applet
# (io.github.mrajster.obsidiannote).
#
# A Plasma applet is a Qt plugin: plasmashell only finds it in a directory on
# Qt's plugin search path. There are exactly two ways to get that right, and
# this script implements both:
#
#   --user    (default)  install into $HOME/.local and also install a
#                        ~/.config/environment.d drop-in that puts
#                        $HOME/.local/lib/plugins on QT_PLUGIN_PATH for every
#                        future login. No root. Survives logout/reboot.
#   --system             install into Qt's own plugin directory
#                        (qtpaths6 --query QT_INSTALL_PLUGINS). Needs sudo, but
#                        needs no environment tweak at all.
#
# Usage:
#   scripts/build.sh                 configure + build + ctest + install + verify
#   scripts/build.sh --user          user-local install (default)
#   scripts/build.sh --system        system-wide install into QT_INSTALL_PLUGINS (sudo)
#   scripts/build.sh --clean         wipe build/ first, then the above
#   scripts/build.sh --no-tests      skip ctest
#   scripts/build.sh --no-install    build (and test) only, do not install
#   scripts/build.sh --no-dropin     user install without the environment.d drop-in
#   scripts/build.sh --no-verify     skip scripts/verify-install.sh
#   scripts/build.sh --debug         CMAKE_BUILD_TYPE=Debug (default: Release)
#   scripts/build.sh --restart       restart plasmashell after installing
#   scripts/build.sh --uninstall     hand over to scripts/uninstall.sh, then exit
#   scripts/build.sh --prefix DIR    install prefix (default: $HOME/.local)
#
# Exits non-zero on the first failing step.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SRC_DIR}/build"

APPLET_ID="io.github.mrajster.obsidiannote"
DROPIN_NAME="60-obsidiannote-qt-plugin-path.conf"
DROPIN_DIR="${XDG_CONFIG_HOME:-${HOME}/.config}/environment.d"
DROPIN="${DROPIN_DIR}/${DROPIN_NAME}"

MODE="user"
PREFIX=""
BUILD_TYPE="Release"
DO_CLEAN=0
DO_TESTS=1
DO_INSTALL=1
DO_DROPIN=1
DO_VERIFY=1
DO_RESTART=0
DO_UNINSTALL=0

die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
step() { printf '\n\033[1;34m==>\033[0m \033[1m%s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }

usage() { sed -n '4,34p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)       MODE="user" ;;
        --system)     MODE="system" ;;
        --clean)      DO_CLEAN=1 ;;
        --no-tests)   DO_TESTS=0 ;;
        --no-install) DO_INSTALL=0 ;;
        --no-dropin)  DO_DROPIN=0 ;;
        --no-verify)  DO_VERIFY=0 ;;
        --debug)      BUILD_TYPE="Debug" ;;
        --restart)    DO_RESTART=1 ;;
        --uninstall)  DO_UNINSTALL=1 ;;
        --prefix)     shift; [[ $# -gt 0 ]] || die "--prefix needs an argument"; PREFIX="$1" ;;
        -h|--help)    usage; exit 0 ;;
        *)            die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

if [[ ${DO_UNINSTALL} -eq 1 ]]; then
    args=()
    [[ "${MODE}" == "system" ]] && args+=(--system)
    [[ ${DO_RESTART} -eq 1 ]] && args+=(--restart)
    [[ -n "${PREFIX}" ]] && args+=(--prefix "${PREFIX}")
    exec "${SCRIPT_DIR}/uninstall.sh" "${args[@]}"
fi

# ---------------------------------------------------------------------------
# Where Qt actually looks for plugins.
# ---------------------------------------------------------------------------
qtpaths_bin=""
for cand in qtpaths6 qtpaths /usr/lib/qt6/bin/qtpaths6 /usr/lib/qt6/bin/qtpaths; do
    if command -v "${cand}" >/dev/null 2>&1; then qtpaths_bin="${cand}"; break; fi
done
if [[ -n "${qtpaths_bin}" ]]; then
    QT_PLUGINS_DIR="$("${qtpaths_bin}" --query QT_INSTALL_PLUGINS)"
else
    QT_PLUGINS_DIR="/usr/lib/qt6/plugins"
    warn "qtpaths not found -- assuming Qt plugins live in ${QT_PLUGINS_DIR}"
fi

if [[ "${MODE}" == "system" ]]; then
    [[ -n "${PREFIX}" ]] || PREFIX="/usr"
    USE_QT_SYS_PATHS=ON
    PLUGIN_ROOT="${QT_PLUGINS_DIR}"
else
    [[ -n "${PREFIX}" ]] || PREFIX="${HOME}/.local"
    USE_QT_SYS_PATHS=OFF
    PLUGIN_ROOT="${PREFIX}/lib/plugins"
fi

# The drop-in is needed exactly when the install target is outside Qt's own
# search path. Never needed for --system.
if [[ "${PLUGIN_ROOT}" == "${QT_PLUGINS_DIR}" ]]; then
    DROPIN_OPT=OFF
elif [[ ${DO_DROPIN} -eq 1 ]]; then
    DROPIN_OPT=ON
else
    DROPIN_OPT=OFF
fi

step "Checking prerequisites"
command -v cmake >/dev/null 2>&1 || die "'cmake' not found in PATH"
# Only choose a generator for a *fresh* build dir: CMake refuses to switch the
# generator of an existing one, and --clean is the documented way to change it.
existing_gen=""
if [[ ${DO_CLEAN} -eq 0 && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    existing_gen="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n1)"
fi
if [[ -n "${existing_gen}" ]]; then
    GENERATOR=()
    GENERATOR_LABEL="${existing_gen} (reused from build/CMakeCache.txt)"
elif command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja); GENERATOR_LABEL="Ninja"
else
    GENERATOR=(); GENERATOR_LABEL="Unix Makefiles"
fi
info "cmake       $(cmake --version | head -n1 | awk '{print $3}')"
info "generator   ${GENERATOR_LABEL}"
info "source      ${SRC_DIR}"
info "build       ${BUILD_DIR}"
info "mode        ${MODE}"
info "prefix      ${PREFIX}"
info "plugin root ${PLUGIN_ROOT}"
info "Qt searches ${QT_PLUGINS_DIR}"
info "env drop-in ${DROPIN_OPT}$( [[ ${DROPIN_OPT} == ON ]] && printf ' -> %s' "${DROPIN}" )"

# Fail early and legibly when a sibling agent has not landed its files yet.
missing=()
for f in CMakeLists.txt metadata.json main.xml \
         markdownnote.cpp markdownnote.h taskmarkdown.cpp taskmarkdown.h \
         qml/main.qml qml/NoteView.qml qml/NoteEditor.qml qml/ShortcutMenuItem.qml \
         qml/config.qml qml/configNote.qml qml/configAppearance.qml \
         scripts/environment.d.conf.in; do
    [[ -f "${SRC_DIR}/${f}" ]] || missing+=("${f}")
done
if [[ ${#missing[@]} -gt 0 ]]; then
    die "missing source file(s): ${missing[*]}"
fi

if [[ ${DO_CLEAN} -eq 1 && -d "${BUILD_DIR}" ]]; then
    step "Cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

step "Configuring (${BUILD_TYPE})"
if [[ ${DO_TESTS} -eq 1 ]]; then TESTING=ON; else TESTING=OFF; fi
# Every path-affecting option is passed explicitly so that switching between
# --user and --system never picks up a stale CMake cache entry.
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" "${GENERATOR[@]}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DKDE_INSTALL_USE_QT_SYS_PATHS="${USE_QT_SYS_PATHS}" \
    -DINSTALL_ENVIRONMENT_DROPIN="${DROPIN_OPT}" \
    -DBUILD_TESTING="${TESTING}"

step "Building"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

if [[ ${DO_TESTS} -eq 1 ]]; then
    step "Running tests"
    if [[ -f "${SRC_DIR}/tests/CMakeLists.txt" ]]; then
        ctest --test-dir "${BUILD_DIR}" --output-on-failure
    else
        info "tests/CMakeLists.txt absent -- skipping"
    fi
fi

if [[ ${DO_INSTALL} -eq 0 ]]; then
    step "Done (not installed; --no-install)"
    info "plugin: ${BUILD_DIR}/bin/plasma/applets/${APPLET_ID}.so"
    info "preview without installing: scripts/run-viewer.sh"
    exit 0
fi

step "Installing into ${PREFIX}"
if [[ "${MODE}" == "system" ]]; then
    if [[ "$(id -u)" -eq 0 ]]; then
        cmake --install "${BUILD_DIR}"
    else
        command -v sudo >/dev/null 2>&1 || die "--system needs root; 'sudo' not found"
        info "sudo cmake --install ${BUILD_DIR}"
        sudo cmake --install "${BUILD_DIR}"
    fi
else
    cmake --install "${BUILD_DIR}"
fi

step "Verifying what landed on disk"
APPLET_SO="${PLUGIN_ROOT}/plasma/applets/${APPLET_ID}.so"
if [[ ! -f "${APPLET_SO}" && -f "${PREFIX}/lib64/plugins/plasma/applets/${APPLET_ID}.so" ]]; then
    APPLET_SO="${PREFIX}/lib64/plugins/plasma/applets/${APPLET_ID}.so"
    PLUGIN_ROOT="${PREFIX}/lib64/plugins"
fi
[[ -f "${APPLET_SO}" ]] || die "expected plugin not found: ${APPLET_SO}"
info "applet   ${APPLET_SO}"
if [[ "${DROPIN_OPT}" == "ON" ]]; then
    [[ -f "${DROPIN}" ]] || die "environment.d drop-in was not installed: ${DROPIN}"
    info "drop-in  ${DROPIN}"
    info "         $(grep -m1 '^QT_PLUGIN_PATH=' "${DROPIN}")"
fi

# ---------------------------------------------------------------------------
# The drop-in only takes effect at the NEXT login. Mirror it into the running
# session so the widget is usable right now, without logging out.
# ---------------------------------------------------------------------------
if [[ "${DROPIN_OPT}" == "ON" ]] && command -v systemctl >/dev/null 2>&1; then
    step "Applying to the running session"
    cur="$(systemctl --user show-environment 2>/dev/null | sed -n 's/^QT_PLUGIN_PATH=//p' || true)"
    if [[ ":${cur}:" == *":${PLUGIN_ROOT}:"* ]]; then
        info "systemd user environment already has ${PLUGIN_ROOT}"
    else
        systemctl --user set-environment "QT_PLUGIN_PATH=${PLUGIN_ROOT}${cur:+:${cur}}"
        info "systemctl --user set-environment QT_PLUGIN_PATH=${PLUGIN_ROOT}${cur:+:${cur}}"
        info "(session-only; the drop-in is what makes it stick across logins)"
        if [[ ${DO_RESTART} -eq 0 ]]; then
            info "plasmashell must be restarted to pick this up: scripts/build.sh --restart"
        fi
    fi
fi

if [[ ${DO_VERIFY} -eq 1 ]]; then
    step "Proving discoverability after a fresh login"
    if ! "${SCRIPT_DIR}/verify-install.sh" --id "${APPLET_ID}"; then
        die "verification failed -- plasmashell would not find the applet after a relogin"
    fi
fi

if [[ ${DO_RESTART} -eq 1 ]]; then
    step "Restarting plasmashell"
    if systemctl --user --quiet is-active plasma-plasmashell.service 2>/dev/null; then
        systemctl --user restart plasma-plasmashell.service
        info "systemctl --user restart plasma-plasmashell.service"
    elif command -v kquitapp6 >/dev/null 2>&1 && command -v kstart >/dev/null 2>&1; then
        kquitapp6 plasmashell || true; sleep 1; (kstart plasmashell >/dev/null 2>&1 &)
        info "kquitapp6 plasmashell && kstart plasmashell"
    else
        info "no known restart mechanism found -- log out and back in"
    fi
fi

step "Done"
info "Add the widget: right-click desktop/panel -> Add Widgets -> \"Obsidian Note\""
info "Preview without touching the desktop: scripts/run-viewer.sh"
info "Remove everything again:               scripts/uninstall.sh"
