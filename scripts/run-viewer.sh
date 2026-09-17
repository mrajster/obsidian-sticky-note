#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Preview the "Obsidian Note" applet in plasmoidviewer, WITHOUT installing it
# and without restarting plasmashell. Runs the freshly built plugin straight
# out of build/bin by pointing QT_PLUGIN_PATH at it.
#
# Usage:
#   scripts/run-viewer.sh                  preview the build-tree plugin
#   scripts/run-viewer.sh --installed      preview the copy under ~/.local instead
#   scripts/run-viewer.sh --sample         write/refresh a sample .md and preview it
#   scripts/run-viewer.sh --build          run scripts/build.sh --no-install first
#   scripts/run-viewer.sh -f vertical      form factor: horizontal|vertical|mediacenter|planar|application
#   scripts/run-viewer.sh -l topedge       location: floating|desktop|fullscreen|topedge|bottomedge|leftedge|rightedge
#   scripts/run-viewer.sh -- --size 500x700    everything after -- goes to plasmoidviewer
#
# Note: plasmoidviewer ships in the "plasma-sdk" package.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SRC_DIR}/build"

APPLET_ID="io.github.mrajster.obsidiannote"
SAMPLE_FILE="${TMPDIR:-/tmp}/obsidian-note-sample.md"

USE_INSTALLED=0
MAKE_SAMPLE=0
DO_BUILD=0
FORM_FACTOR=""
LOCATION=""
EXTRA=()

die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;34m==>\033[0m \033[1m%s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }

usage() {
    cat <<'USAGE'
Preview the "Obsidian Note" applet in plasmoidviewer, WITHOUT installing it
and without restarting plasmashell. Runs the freshly built plugin straight
out of build/bin by pointing QT_PLUGIN_PATH at it.

Usage:
  scripts/run-viewer.sh                  preview the build-tree plugin
  scripts/run-viewer.sh --installed      preview the copy under ~/.local instead
  scripts/run-viewer.sh --sample         write/refresh a sample .md and preview it
  scripts/run-viewer.sh --build          run scripts/build.sh --no-install first
  scripts/run-viewer.sh -f vertical      form factor: horizontal|vertical|mediacenter|planar|application
  scripts/run-viewer.sh -l topedge       location: floating|desktop|fullscreen|topedge|bottomedge|leftedge|rightedge
  scripts/run-viewer.sh -- --size 500x700    everything after -- goes to plasmoidviewer

Note: plasmoidviewer ships in the "plasma-sdk" package.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --installed)  USE_INSTALLED=1 ;;
        --sample)     MAKE_SAMPLE=1 ;;
        --build)      DO_BUILD=1 ;;
        -f|--formfactor) shift; [[ $# -gt 0 ]] || die "-f needs an argument"; FORM_FACTOR="$1" ;;
        -l|--location)   shift; [[ $# -gt 0 ]] || die "-l needs an argument"; LOCATION="$1" ;;
        --)           shift; EXTRA=("$@"); break ;;
        -h|--help)    usage; exit 0 ;;
        *)            die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

command -v plasmoidviewer >/dev/null 2>&1 || \
    die "plasmoidviewer not found -- install the 'plasma-sdk' package"

if [[ ${DO_BUILD} -eq 1 ]]; then
    "${SCRIPT_DIR}/build.sh" --no-install --no-tests
fi

if [[ ${USE_INSTALLED} -eq 1 ]]; then
    PLUGIN_ROOT=""
    for cand in "${HOME}/.local/lib/plugins" "${HOME}/.local/lib64/plugins"; do
        if [[ -f "${cand}/plasma/applets/${APPLET_ID}.so" ]]; then PLUGIN_ROOT="${cand}"; break; fi
    done
    [[ -n "${PLUGIN_ROOT}" ]] || die "no installed plugin found -- run scripts/build.sh first"
else
    PLUGIN_ROOT="${BUILD_DIR}/bin"
    [[ -f "${PLUGIN_ROOT}/plasma/applets/${APPLET_ID}.so" ]] || \
        die "no built plugin at ${PLUGIN_ROOT}/plasma/applets/${APPLET_ID}.so -- run scripts/build.sh --no-install (or pass --build)"
fi

if [[ ${MAKE_SAMPLE} -eq 1 ]]; then
    step "Writing sample note"
    cat > "${SAMPLE_FILE}" <<'SAMPLE'
---
tags: [demo, groceries]
created: 2026-09-16
---

# Obsidian Note demo

A paragraph with **bold**, *italic*, ~~struck~~ text, an [external
link](https://kde.org) and a [[Wikilink Target|wikilink]].

## Groceries

- [ ] milk
- [x] bread
  - [ ] sourdough, nested one level
* [X] eggs
1. [ ] numbered task

## Not clickable (inside a fence)

```sh
- [ ] this must stay literal text, no checkbox glyph
```

Inline code stays literal too: `- [ ] not a task`.

| item | qty |
| ---- | --- |
| milk | 2   |
| eggs | 12  |

> A blockquote, for good measure.
SAMPLE
    info "${SAMPLE_FILE}"
    info "point the widget at it: toolbar folder button, or the File config page"
fi

ARGS=(-a "${APPLET_ID}")
if [[ -n "${FORM_FACTOR}" ]]; then ARGS+=(-f "${FORM_FACTOR}"); fi
if [[ -n "${LOCATION}" ]]; then ARGS+=(-l "${LOCATION}"); fi
if [[ ${#EXTRA[@]} -gt 0 ]]; then ARGS+=("${EXTRA[@]}"); fi

step "Launching plasmoidviewer"
info "plugin root : ${PLUGIN_ROOT}"
info "command     : plasmoidviewer ${ARGS[*]}"
if [[ ${MAKE_SAMPLE} -eq 1 ]]; then info "sample note : ${SAMPLE_FILE}"; fi

export QT_PLUGIN_PATH="${PLUGIN_ROOT}${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
# Surface QML warnings/errors on the terminal instead of swallowing them.
export QT_LOGGING_RULES="${QT_LOGGING_RULES:+${QT_LOGGING_RULES};}qt.qml.binding.removal.info=true"

exec plasmoidviewer "${ARGS[@]}"
