/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * Fenced / indented code (and $$math$$). 0.875em monospace on a 1.5em BODY line
 * box: Obsidian's code baseline comes from the body font's line box, not the
 * mono font's (contract §0.3). Long lines wrap -- nothing may overflow sideways.
 */
Item {
    id: code

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    readonly property string codeText: code.block.codeText || ""
    readonly property int lineCount: code.codeText === "" ? 0 : Math.max(1, text.lineCount)
    readonly property real firstBaselineY: code.metrics.codeBlockPadY
        + code.metrics.baselineInBox(code.metrics.fmBody, code.metrics.codeLinePitch)

    width: code.availableWidth
    height: Math.max(code.metrics.codeBlockMinHeight,
                     2 * code.metrics.codeBlockPadY + code.lineCount * code.metrics.codeLinePitch)

    Rectangle {
        anchors.fill: parent
        radius: code.metrics.codeBlockRadius
        color: code.metrics.codeBackgroundColor
    }

    Text {
        id: text

        x: code.metrics.codeBlockPadX
        // With a FixedHeight pitch larger than the font, Qt puts each baseline
        // at line top + ascent; move the first one onto the CSS baseline.
        y: code.firstBaselineY - text.baselineOffset
        width: Math.max(0, code.width - 2 * code.metrics.codeBlockPadX)

        text: code.codeText
        textFormat: Text.PlainText
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        lineHeightMode: Text.FixedHeight
        lineHeight: code.metrics.codeLinePitch
        font.family: code.metrics.monoFamily
        font.pointSize: code.metrics.pt(code.metrics.codeFontSize)
        color: code.metrics.codeTextColor
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        out.push(code.metrics.geometryEntry("pre", code.block, code, origin, {
            textStartX: code.metrics.codeBlockPadX,
            firstLineBaselineY: code.firstBaselineY,
            lineCount: code.lineCount,
            fontPx: code.metrics.em,
            lineHeightPx: code.metrics.codeLinePitch
        }));
    }
}
