/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * Obsidian's indentation guides: a 1 px vertical line 0.85em left of a list
 * item's text column, running the full height of that item's nested subtree.
 *
 * Positions are read from the BlockList's own delegates, so they follow wrapping
 * and resizing exactly; they are recomputed whenever the list re-stacks.
 */
Item {
    id: guides

    required property var blocks
    required property ObsidianMetrics metrics
    /** The BlockList's Repeater of BlockDelegates. */
    property Repeater repeater: null
    /** The BlockList whose re-stacking triggers a recompute. */
    property Item stack: null
    /** Offset between this item's y and the BlockList's coordinate origin. */
    property real originY: 0

    /** [{x, y, h, sourceLine}] in BlockList coordinates; sourceLine is the owning list item's. */
    property var lines: []

    width: 0
    height: 0

    function isListKind(kind: string): bool {
        return kind === "bullet" || kind === "ordered" || kind === "task";
    }

    function recompute() {
        const out = [];
        const bs = guides.blocks || [];
        const rep = guides.repeater;
        if (!rep || rep.count !== bs.length) {
            guides.lines = out;
            return;
        }
        const m = guides.metrics;
        for (let i = 0; i < bs.length; ++i) {
            const b = bs[i];
            if (!guides.isListKind(b.kind)) {
                continue;
            }
            const depth = b.depth || 0;
            // First nested list item that belongs to this item's subtree.
            let first = -1;
            let last = -1;
            for (let j = i + 1; j < bs.length; ++j) {
                const c = bs[j];
                const listChild = guides.isListKind(c.kind);
                const inside = listChild ? (c.depth || 0) > depth : (!!c.inItem && (c.depth || 0) >= depth);
                if (!inside) {
                    break;
                }
                if (listChild && first < 0) {
                    first = j;
                }
                if (first >= 0) {
                    last = j;
                }
            }
            if (first < 0) {
                continue;
            }
            const dFirst = rep.itemAt(first);
            const dLast = rep.itemAt(last);
            if (!dFirst || !dLast) {
                continue;
            }
            const lastBlock = bs[last];
            const ownPads = Math.max(0, (lastBlock.depth || 0) - depth);
            const foreign = Math.max(0, (lastBlock.listPadAfter || 0) - ownPads);
            const top = dFirst.y + dFirst.gap;
            const bottom = dLast.y + dLast.height - foreign * m.listItemPad;
            out.push({
                x: m.listTextX(depth) + m.guideOffset,
                y: top,
                h: Math.max(0, bottom - top),
                sourceLine: b.sourceLine
            });
        }
        guides.lines = out;
    }

    Timer {
        id: settle
        interval: 0
        onTriggered: guides.recompute()
    }

    Connections {
        target: guides.stack
        ignoreUnknownSignals: true

        function onHeightChanged() {
            settle.restart();
        }
        function onWidthChanged() {
            settle.restart();
        }
    }

    onBlocksChanged: settle.restart()
    Component.onCompleted: settle.restart()

    Repeater {
        model: guides.lines

        delegate: Rectangle {
            required property var modelData

            objectName: "guide"

            // Snapped: a 1 px line at a fractional x can vanish in the software
            // renderer, and Chromium snaps borders to device pixels too.
            x: Math.round(modelData.x)
            y: modelData.y - guides.originY
            width: guides.metrics.guideWidth
            // Whole pixels from the row the top rounds to down to the row the
            // bottom rounds to (Chromium's snapping). A fractional height is
            // ceil'd by the software renderer -- one row too long -- but
            // filled by pixel centres on the GPU; a whole one is the same in both.
            height: Math.max(0, Math.round(modelData.y + modelData.h) - Math.round(modelData.y))
            color: guides.metrics.borderColor
        }
    }
}
