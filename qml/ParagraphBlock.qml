/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/** Body paragraph: 1em text on a 1.5em line grid. */
Item {
    id: paragraph

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    readonly property alias inlineText: text

    width: paragraph.availableWidth
    height: text.height

    InlineText {
        id: text

        html: paragraph.block.inlineHtml || ""
        decorations: paragraph.block.decorations || []
        marks: paragraph.block.marks || []
        metrics: paragraph.metrics
        fontPx: paragraph.metrics.em
        fontWeight: Font.Normal
        letterSpacing: 0
        lineHeightPx: paragraph.metrics.lhBody
        struck: !!paragraph.block.struck
        color: paragraph.block.struck ? paragraph.metrics.mutedTextColor : paragraph.metrics.textColor
        availableWidth: paragraph.width
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        out.push(paragraph.metrics.geometryEntry("p", paragraph.block, paragraph, origin, {
            textStartX: 0,
            firstLineBaselineY: text.firstBaselineY,
            lineCount: text.lineCount,
            fontPx: text.fontPx,
            lineHeightPx: text.lineHeightPx
        }));
        paragraph.metrics.dumpPills(out, paragraph.block, text, origin);
    }
}
