#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# One-liner for the recommended, rootless install:
#
#   * builds the applet,
#   * installs it into ~/.local/lib/plugins/plasma/applets/,
#   * installs ~/.config/environment.d/60-obsidiannote-qt-plugin-path.conf so
#     QT_PLUGIN_PATH covers that directory at every future login,
#   * mirrors the change into the running session and restarts plasmashell,
#   * proves with scripts/verify-install.sh that a *fresh login* would find it.
#
# Every scripts/build.sh flag is accepted and forwarded, e.g.
#   scripts/install-user.sh --clean --no-tests
#
# For the root-owned alternative (into Qt's own plugin dir, no environment
# tweak) use:  scripts/build.sh --system
# To remove everything again:  scripts/uninstall.sh

set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/build.sh" --user "$@"
