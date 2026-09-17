#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Remove the "Obsidian Note" Plasma applet AND the environment.d drop-in that
# the user-local install added. Works without a build directory.
#
# Usage:
#   scripts/uninstall.sh               remove the user-local install
#   scripts/uninstall.sh --system      also remove the system-wide copy (sudo)
#   scripts/uninstall.sh --keep-dropin leave ~/.config/environment.d/*.conf alone
#   scripts/uninstall.sh --restart     restart plasmashell afterwards
#   scripts/uninstall.sh --prefix DIR  user prefix (default: $HOME/.local)
#   scripts/uninstall.sh --dry-run     print what would be removed
#
# Exits 0 even when nothing was installed.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SRC_DIR}/build"

APPLET_ID="io.github.mrajster.obsidiannote"
DROPIN_NAME="60-obsidiannote-qt-plugin-path.conf"
DROPIN_DIR="${XDG_CONFIG_HOME:-${HOME}/.config}/environment.d"
DROPIN="${DROPIN_DIR}/${DROPIN_NAME}"

PREFIX="${HOME}/.local"
DO_SYSTEM=0
KEEP_DROPIN=0
DO_RESTART=0
DRY_RUN=0

usage() { sed -n '4,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;34m==>\033[0m \033[1m%s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)      DO_SYSTEM=1 ;;
        --keep-dropin) KEEP_DROPIN=1 ;;
        --restart)     DO_RESTART=1 ;;
        --dry-run)     DRY_RUN=1 ;;
        --prefix)      shift; [[ $# -gt 0 ]] || die "--prefix needs an argument"; PREFIX="$1" ;;
        -h|--help)     usage; exit 0 ;;
        *)             die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

removed=0
rm_path() { # rm_path <path> [sudo]
    local p="$1" use_sudo="${2:-}"
    [[ -e "${p}" || -L "${p}" ]] || return 0
    if [[ ${DRY_RUN} -eq 1 ]]; then
        info "would remove ${p}"
    elif [[ "${use_sudo}" == "sudo" ]]; then
        sudo rm -rf -- "${p}" && info "removed ${p} (root)"
    else
        rm -rf -- "${p}" && info "removed ${p}"
    fi
    removed=1
}

step "Removing the applet plugin"
for so in "${PREFIX}/lib/plugins/plasma/applets/${APPLET_ID}.so" \
          "${PREFIX}/lib64/plugins/plasma/applets/${APPLET_ID}.so"; do
    rm_path "${so}"
done
# A stale kpackagetool6 copy, if an earlier install left one behind.
for d in "${PREFIX}/share/plasma/plasmoids/${APPLET_ID}" \
         "${HOME}/.local/share/plasma/plasmoids/${APPLET_ID}"; do
    rm_path "${d}"
done

if [[ ${DO_SYSTEM} -eq 1 ]]; then
    step "Removing the system-wide copy (needs root)"
    qt_plugins="$( (command -v qtpaths6 >/dev/null 2>&1 && qtpaths6 --query QT_INSTALL_PLUGINS) \
                || (command -v qtpaths >/dev/null 2>&1 && qtpaths --query QT_INSTALL_PLUGINS) \
                || echo /usr/lib/qt6/plugins )"
    rm_path "${qt_plugins}/plasma/applets/${APPLET_ID}.so" sudo
fi

if [[ ${KEEP_DROPIN} -eq 0 ]]; then
    step "Removing the environment.d drop-in"
    rm_path "${DROPIN}"
else
    step "Keeping ${DROPIN} (--keep-dropin)"
fi

if [[ -f "${BUILD_DIR}/install_manifest.txt" ]]; then
    step "Sweeping leftovers from build/install_manifest.txt"
    # CMake writes install_manifest.txt WITHOUT a trailing newline, so a plain
    # `while read` exits before the body ever sees the final entry -- and the
    # drop-in happens to be that final entry. The `|| [[ -n ... ]]` guard makes
    # the last unterminated line count.
    while IFS= read -r f || [[ -n "${f}" ]]; do
        [[ -n "${f}" ]] || continue
        # --keep-dropin must survive the manifest sweep too: the drop-in is
        # recorded in the manifest, so without this the sweep would delete the
        # very file the flag promises to keep.
        if [[ ${KEEP_DROPIN} -eq 1 && "${f}" == "${DROPIN}" ]]; then
            info "keeping ${f} (--keep-dropin)"
            continue
        fi
        rm_path "${f}"
    done < "${BUILD_DIR}/install_manifest.txt"
fi

# Prune the directories we created, but only while they are empty.
if [[ ${DRY_RUN} -eq 0 ]]; then
    for d in "${PREFIX}/lib/plugins/plasma/applets" "${PREFIX}/lib/plugins/plasma" \
             "${PREFIX}/lib64/plugins/plasma/applets" "${PREFIX}/lib64/plugins/plasma"; do
        rmdir -- "${d}" 2>/dev/null && info "removed empty ${d}" || true
    done
fi

step "Live session"
if [[ ${DRY_RUN} -eq 1 ]]; then
    info "dry run -- session untouched"
elif [[ ${KEEP_DROPIN} -eq 0 ]] && command -v systemctl >/dev/null 2>&1; then
    cur="$(systemctl --user show-environment 2>/dev/null | sed -n 's/^QT_PLUGIN_PATH=//p' || true)"
    ours="${PREFIX}/lib/plugins"
    if [[ -n "${cur}" && ":${cur}:" == *":${ours}:"* ]]; then
        rest="$(printf '%s' ":${cur}:" | sed "s|:${ours}:|:|g; s|^:||; s|:$||")"
        if [[ -n "${rest}" ]]; then
            systemctl --user set-environment "QT_PLUGIN_PATH=${rest}"
            info "QT_PLUGIN_PATH in this session is now: ${rest}"
        else
            systemctl --user unset-environment QT_PLUGIN_PATH
            info "QT_PLUGIN_PATH unset for this session"
        fi
    else
        info "nothing of ours in the session QT_PLUGIN_PATH"
    fi
fi

if [[ ${DO_RESTART} -eq 1 && ${DRY_RUN} -eq 0 ]]; then
    step "Restarting plasmashell"
    if systemctl --user --quiet is-active plasma-plasmashell.service 2>/dev/null; then
        systemctl --user restart plasma-plasmashell.service
        info "systemctl --user restart plasma-plasmashell.service"
    elif command -v kquitapp6 >/dev/null 2>&1 && command -v kstart >/dev/null 2>&1; then
        kquitapp6 plasmashell || true; sleep 1; (kstart plasmashell >/dev/null 2>&1 &)
        info "kquitapp6 plasmashell && kstart plasmashell"
    else
        info "no known restart mechanism -- log out and back in"
    fi
fi

step "Done"
[[ ${removed} -eq 1 ]] || info "nothing was installed"
