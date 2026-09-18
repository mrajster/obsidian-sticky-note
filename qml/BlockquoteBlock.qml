/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * "> quote": 2 px left border, 1.5em padding, children laid out as a nested
 * BlockList. No leading or trailing space of its own (the children's first/last
 * margins collapse to 0, which the C++ gap tokens already encode).
 */
Item {
    id: quote

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth
    property NotePalette notePalette: null

    signal toggleRequested(int sourceLine, string expectedLineText)

    readonly property real bodyX: quote.metrics.blockquoteBorder + quote.metrics.blockquotePadding
    readonly property real bodyWidth: Math.max(0, quote.width - quote.bodyX)
    readonly property var children_: quote.block.children || []

    width: quote.availableWidth
    height: body.item ? body.item.height : 0

    Rectangle {
        width: quote.metrics.blockquoteBorder
        height: quote.height
        color: quote.metrics.accentColor
    }

    // Recursion (a quote can hold a quote) goes through a URL Loader so the
    // BlockList -> BlockDelegate -> BlockquoteBlock type cycle is broken.
    Loader {
        id: body

        x: quote.bodyX

        Component.onCompleted: body.setSource("BlockList.qml", {
            blocks: quote.children_,
            metrics: quote.metrics,
            notePalette: quote.notePalette,
            availableWidth: quote.bodyWidth
        })
    }

    Binding {
        target: body.item
        when: body.status === Loader.Ready
        property: "availableWidth"
        value: quote.bodyWidth
    }

    Binding {
        target: body.item
        when: body.status === Loader.Ready
        property: "blocks"
        value: quote.children_
    }

    Connections {
        target: body.item
        ignoreUnknownSignals: true

        function onTaskToggleRequested(sourceLine: int, expectedLineText: string) {
            quote.toggleRequested(sourceLine, expectedLineText);
        }
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        const m = quote.metrics;
        const first = body.item && body.item.repeater.count > 0 ? body.item.repeater.itemAt(0) : null;
        const firstText = first && first.blockItem && first.blockItem.inlineText ? first.blockItem.inlineText : null;
        const e = {
            textStartX: quote.bodyX,
            lineCount: firstText ? firstText.lineCount : 0,
            fontPx: m.em,
            lineHeightPx: m.lhBody
        };
        if (firstText) {
            e.firstLineBaselineY = firstText.mapToItem(quote, 0, firstText.firstBaselineY).y;
        }
        out.push(m.geometryEntry("blockquote", quote.block, quote, origin, e));
        if (body.item) {
            const firstChild = out.length;
            body.item.dumpGeometry(out, origin);
            // Obsidian tags everything inside a quote/callout with the
            // container's first source line; the dump follows that convention.
            for (let k = firstChild; k < out.length; ++k) {
                out[k].sourceLine = quote.block.sourceLine;
            }
        }
    }
}
