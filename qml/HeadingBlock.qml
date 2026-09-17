/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/** ATX / Setext heading, h1..h6 on Obsidian's size/weight/line-height/tracking scale. */
Item {
    id: heading

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    readonly property int level: Math.max(1, Math.min(6, heading.block.level || 1))

    width: heading.availableWidth
    height: text.height

    InlineText {
        id: text

        html: heading.block.inlineHtml || ""
        decorations: heading.block.decorations || []
        marks: heading.block.marks || []
        metrics: heading.metrics
        fontPx: heading.metrics.headingSize(heading.level)
        fontWeight: heading.metrics.headingWeight(heading.level)
        letterSpacing: heading.metrics.headingLetterSpacing(heading.level)
        lineHeightPx: heading.metrics.headingLineHeight(heading.level)
        fontMetrics: heading.metrics.headingMetrics(heading.level)
        struck: !!heading.block.struck
        availableWidth: heading.width
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        out.push(heading.metrics.geometryEntry("h" + heading.level, heading.block, heading, origin, {
            textStartX: 0,
            firstLineBaselineY: text.firstBaselineY,
            lineCount: text.lineCount,
            fontPx: text.fontPx,
            lineHeightPx: text.lineHeightPx
        }));
        heading.metrics.dumpPills(out, heading.block, text, origin);
    }
}
