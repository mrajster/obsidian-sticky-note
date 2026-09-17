/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * One list item's own line(s): bullet, ordered or task.
 *
 * Geometry (Obsidian reading view):
 *   - 0.075em top pad (the bottom pad is carried by BlockDelegate.listPadAfter)
 *   - text column at listTextX(depth); wrapped lines hang at that same x
 *   - bullet: 0.3em dot centred 0.8em left of the text, on the first line box
 *   - ordered: "N." right-aligned one space-advance left of the text
 *   - task: NO bullet; 1em checkbox hanging 1.5em left of the text, 0.29em down
 *     (ordered task: marker kept, checkbox inline at the text column)
 */
Item {
    id: item

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    signal toggleRequested(int sourceLine, string expectedLineText)

    readonly property int depth: Math.max(1, item.block.depth || 1)
    readonly property bool isTask: item.block.kind === "task"
    readonly property bool isOrdered: item.block.kind === "ordered" || item.block.listType === "ordered"
    readonly property bool inlineCheckbox: item.isTask && item.isOrdered
    readonly property real baseTextX: item.metrics.listTextX(item.depth)
    readonly property real textX: item.baseTextX
        + (item.inlineCheckbox ? item.metrics.checkboxSize + item.metrics.checkboxGap : 0)
    readonly property real lineTop: item.metrics.listItemPad
    readonly property alias checkboxItem: checkbox
    readonly property alias inlineText: text

    /** This item's position inside its BlockList (BlockDelegate sets it); used for pixel snapping. */
    property real listX: 0
    property real listY: 0

    width: item.availableWidth
    height: item.lineTop + text.height

    Rectangle {
        id: bullet

        objectName: "bullet"

        visible: item.block.kind === "bullet" || (!item.isTask && !item.isOrdered)
        width: item.metrics.bulletSize
        height: item.metrics.bulletSize
        radius: width / 2
        x: item.baseTextX - item.metrics.bulletCenterOffset - width / 2
        y: item.lineTop + item.metrics.lhBody / 2 - height / 2
        color: item.metrics.mutedTextColor
    }

    Text {
        id: marker

        visible: item.isOrdered
        text: item.block.markerText || ""
        textFormat: Text.PlainText
        font.family: item.metrics.textFamily
        font.pointSize: item.metrics.pt(item.metrics.em)
        color: item.metrics.mutedTextColor
        x: item.baseTextX - item.metrics.orderedMarkerGap - implicitWidth
        y: item.lineTop + item.metrics.baselineInBox(item.metrics.fmBody, item.metrics.lhBody) - baselineOffset
    }

    TaskCheckbox {
        id: checkbox

        objectName: "checkbox"

        visible: item.isTask
        checked: !!item.block.checked
        interactive: item.isTask && !!item.block.toggleable
        metrics: item.metrics
        x: item.inlineCheckbox ? item.baseTextX : item.baseTextX - item.metrics.checkboxHang
        y: item.lineTop + item.metrics.checkboxTopInLine
        // Its position in the BlockList, so it can paint on whole pixels.
        exactX: item.listX + checkbox.x
        exactY: item.listY + checkbox.y

        onToggled: item.toggleRequested(item.block.sourceLine, item.block.expectedLineText || "")
    }

    InlineText {
        id: text

        x: item.textX
        y: item.lineTop
        html: item.block.inlineHtml || ""
        decorations: item.block.decorations || []
        marks: item.block.marks || []
        metrics: item.metrics
        fontPx: item.metrics.em
        fontWeight: Font.Normal
        letterSpacing: 0
        lineHeightPx: item.metrics.lhBody
        struck: !!item.block.struck
        color: item.block.struck ? item.metrics.mutedTextColor : item.metrics.textColor
        availableWidth: Math.max(0, item.width - item.textX)
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        const m = item.metrics;
        const e = m.geometryEntry(item.isTask ? "task" : "li", item.block, item, origin, {
            dx: item.textX,
            w: item.width - item.textX,
            textStartX: item.textX,
            firstLineBaselineY: item.lineTop + text.firstBaselineY,
            lineCount: text.lineCount,
            fontPx: text.fontPx,
            lineHeightPx: text.lineHeightPx
        });
        if (extra && extra.subtreeBottom !== undefined) {
            e.h = m.r2(extra.subtreeBottom - item.mapToItem(origin, 0, 0).y);
        }
        out.push(e);
        if (bullet.visible) {
            out.push(m.geometryEntry("bullet", item.block, bullet, origin, null));
        }
        if (checkbox.visible) {
            out.push(m.geometryEntry("checkbox", item.block, checkbox, origin, null));
        }
        m.dumpPills(out, item.block, text, origin);
    }
}
