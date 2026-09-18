/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * A flat, ordered list of render blocks (MarkdownBlocks::parse output) stacked
 * with zero spacing: every vertical gap is carried by the block itself
 * (BlockDelegate: gap token above, list-item pads below), exactly like
 * collapsed CSS margins resolved ahead of time by the C++ side.
 *
 * Used for the note body, and recursively (via Loader) for blockquote and
 * callout bodies.
 */
Column {
    id: list

    required property var blocks
    required property ObsidianMetrics metrics
    required property real availableWidth
    property bool inCallout: false
    /** Colours that are not a plain metrics colour (CalloutBlock's box tint); null = metrics only. */
    property NotePalette notePalette: null

    /** A checkbox inside this list (or any nested list) was clicked. */
    signal taskToggleRequested(int sourceLine, string expectedLineText)

    readonly property alias repeater: rep

    width: list.availableWidth
    spacing: 0

    Repeater {
        id: rep

        model: list.blocks

        delegate: BlockDelegate {
            metrics: list.metrics
            notePalette: list.notePalette
            availableWidth: list.availableWidth
            onTaskToggleRequested: (line, expected) => list.taskToggleRequested(line, expected)
        }
    }

    // NOT a Column child: positioners cull zero-size children, so a 0x0 overlay
    // inside the Column is never drawn, and a sized one would add height.
    // It lives beside the Column instead, tracking its position.
    IndentGuides {
        id: guides

        parent: list.parent
        x: list.x
        y: list.y
        z: list.z + 1
        blocks: list.blocks
        metrics: list.metrics
        repeater: rep
        stack: list
        originY: 0
    }

    function isListKind(kind: string): bool {
        return kind === "bullet" || kind === "ordered" || kind === "task";
    }

    /** Index of the last block in list item @p i's subtree. */
    function subtreeEnd(i: int): int {
        const bs = list.blocks || [];
        const depth = bs[i].depth || 0;
        let j = i + 1;
        while (j < bs.length) {
            const b = bs[j];
            const inside = list.isListKind(b.kind) ? (b.depth || 0) > depth : (!!b.inItem && (b.depth || 0) >= depth);
            if (!inside) {
                break;
            }
            ++j;
        }
        return j - 1;
    }

    /** Column-local bottom of list item @p i's subtree, including only its own pads. */
    function subtreeBottom(i: int): real {
        const bs = list.blocks || [];
        const last = list.subtreeEnd(i);
        const d = rep.itemAt(last);
        if (!d) {
            return 0;
        }
        const lastBlock = bs[last];
        const depth = bs[i].depth || 0;
        const closingInside = (lastBlock.depth || 0) - depth + 1;
        const foreign = Math.max(0, (lastBlock.listPadAfter || 0) - closingInside);
        return d.y + d.height - foreign * list.metrics.listItemPad;
    }

    function dumpGeometry(out: var, origin: Item) {
        const bs = list.blocks || [];
        for (let i = 0; i < rep.count; ++i) {
            const d = rep.itemAt(i);
            if (!d || !d.blockItem || typeof d.blockItem.dumpGeometry !== "function") {
                continue;
            }
            const extra = {};
            if (list.isListKind(bs[i].kind)) {
                const bottom = list.mapToItem(origin, 0, list.subtreeBottom(i)).y;
                extra.subtreeBottom = bottom;
            }
            d.blockItem.dumpGeometry(out, origin, extra);
        }
    }
}
