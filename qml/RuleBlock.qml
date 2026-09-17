/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/** Thematic break: a 2 px rule. Its 2em margins are gap tokens, not part of the item. */
Item {
    id: rule

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    width: rule.availableWidth
    height: rule.metrics.hrThickness

    Rectangle {
        anchors.fill: parent
        color: rule.metrics.borderColor
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        out.push(rule.metrics.geometryEntry("hr", rule.block, rule, origin, {
            fontPx: rule.metrics.em, lineHeightPx: rule.metrics.lhBody
        }));
    }
}
