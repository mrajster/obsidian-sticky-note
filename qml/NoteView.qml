/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * Read-only rendered view of the note, laid out on Obsidian's reading-view
 * geometry (ObsidianMetrics) from the block list MarkdownNote::blocks.
 *
 * Rendering is one-way: nothing here ever produces file bytes. A checkbox click
 * becomes taskToggleRequested(sourceLine, expectedLineText) and main.qml hands
 * that to MarkdownNote::toggleTask, which re-reads the file and flips one byte.
 *
 * Scrolling rule: NO scrollbar is ever drawn, on either axis. Every text run
 * wraps to the width, and contentWidth is pinned to the view width, so
 * horizontal overflow and horizontal flicking are impossible. Wheel and touch
 * still scroll vertically when the note is taller than the widget.
 */
FocusScope {
    id: viewRoot

    /** The backend this view renders. Kept typed so qmlcachegen can check it. */
    required property MarkdownNote note

    property var blocks: viewRoot.note.blocks
    property real basePointSize: 12
    property string fontFamily: "Noto Sans"
    property string inlineTitle: ""
    property bool showInlineTitle: false
    property bool showProperties: true
    property bool readOnly: false

    /** "" when the pointer is not over a link. */
    readonly property string linkUnderCursor: clickCatcher.hoverLink

    readonly property ObsidianMetrics metrics: ObsidianMetrics {
        basePointSize: viewRoot.basePointSize
        textFamily: viewRoot.fontFamily

        textColor: Kirigami.Theme.textColor
        mutedTextColor: Kirigami.Theme.disabledTextColor
        linkColor: Kirigami.Theme.linkColor
        backgroundColor: Kirigami.Theme.backgroundColor
        codeBackgroundColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.07)
        codeTextColor: Kirigami.Theme.textColor
        tagTextColor: Kirigami.Theme.linkColor
        tagBackgroundColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.linkColor, 0.15)
        markBackgroundColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.neutralTextColor, 0.35)
        borderColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
        accentColor: Kirigami.Theme.highlightColor
        positiveColor: Kirigami.Theme.positiveTextColor
        neutralColor: Kirigami.Theme.neutralTextColor
        negativeColor: Kirigami.Theme.negativeTextColor
    }

    /** The block stack (for tests and geometry dumps). */
    readonly property Item contentColumn: blockList

    /**
     * Left click on something that is not a link or a checkbox: the user wants
     * to edit. sourceLine is the 0-based source line of the top-level block
     * under the click (-1: above the first block or below the last one) and
     * blockTopY that block's top edge in viewRoot coordinates, so the editor
     * can put the same line at the same height instead of jumping.
     */
    signal editRequested(int sourceLine, real blockTopY)
    /** Left click on a link; main.qml decides what the link means. */
    signal linkClicked(string link)
    /** Right click; main.qml owns the menu, so it has to pop it up. */
    signal contextMenuRequested()
    /** A toggleable checkbox was clicked. */
    signal taskToggleRequested(int sourceLine, string expectedLineText)

    /** Blocks actually shown: frontmatter dropped when properties are hidden. */
    readonly property var visibleBlocks: {
        const bs = viewRoot.blocks || [];
        if (viewRoot.showProperties) {
            return bs;
        }
        const out = [];
        for (let i = 0; i < bs.length; ++i) {
            if (bs[i].kind === "properties") {
                continue;
            }
            if (out.length === 0) {
                // The first shown block never carries a leading gap.
                out.push(Object.assign({}, bs[i], { gap: "none" }));
            } else {
                out.push(bs[i]);
            }
        }
        return out;
    }

    /** Current vertical scroll offset, to be stored in Plasmoid.configuration.scrollY. */
    function saveScroll(): real {
        return flick.contentY;
    }

    /** Re-apply a previously stored offset, clamped to the current content height. */
    function restoreScroll(y: real) {
        if (y <= 0) {
            return;
        }
        const maxY = Math.max(0, flick.contentHeight - flick.height);
        flick.contentY = Math.min(y, maxY);
    }

    /** Content-box y of the first block's top (tests: must equal the editor's text top). */
    function firstBlockTop(): real {
        const top = content.y + blockList.y;
        const d = blockList.repeater.count > 0 ? blockList.repeater.itemAt(0) : null;
        return top + (d ? d.gap : 0) - flick.contentY;
    }

    /**
     * Geometry dump in the shape of tests/geometry/obsidian-*-rects.json:
     * coordinates relative to the content box inside the 2em padding.
     */
    function dumpGeometry(): var {
        const out = [];
        if (titleLoader.item) {
            out.push(viewRoot.metrics.geometryEntry("inline-title", { text: viewRoot.inlineTitle, sourceLine: -1, depth: 0 },
                                                    titleLoader.item, content, {
                textStartX: 0,
                firstLineBaselineY: titleLoader.item.firstBaselineY,
                lineCount: titleLoader.item.lineCount,
                fontPx: titleLoader.item.fontPx,
                lineHeightPx: titleLoader.item.lineHeightPx
            }));
        }
        blockList.dumpGeometry(out, content);
        for (let i = 0; i < out.length; ++i) {
            out[i].i = i;
        }
        return out;
    }

    /** The topmost item under (x, y) of @p root carrying marker property @p marker. */
    function findUnder(root: Item, x: real, y: real, marker: string): Item {
        let item = root;
        let px = x;
        let py = y;
        let found = null;
        while (item) {
            if (item[marker] === true) {
                found = item;
            }
            const child = item.childAt(px, py);
            if (!child) {
                break;
            }
            const p = item.mapToItem(child, px, py);
            px = p.x;
            py = p.y;
            item = child;
        }
        return found;
    }

    /** {sourceLine, topY} of the top-level block under viewRoot point (x, y); sourceLine -1 when none. */
    function blockAt(x: real, y: real): var {
        const p = viewRoot.mapToItem(blockList, x, y);
        const rep = blockList.repeater;
        const bs = viewRoot.visibleBlocks || [];
        for (let i = 0; i < rep.count; ++i) {
            const d = rep.itemAt(i);
            if (!d || i >= bs.length || p.y < d.y || p.y >= d.y + d.height) {
                continue;
            }
            const line = bs[i].sourceLine !== undefined ? bs[i].sourceLine : -1;
            return { sourceLine: line, topY: blockList.mapToItem(viewRoot, 0, d.y + d.gap).y };
        }
        return { sourceLine: -1, topY: 0 };
    }

    /** Link under a point in viewRoot coordinates ("" when none). */
    function linkAt(x: real, y: real): string {
        const p = viewRoot.mapToItem(content, x, y);
        const text = viewRoot.findUnder(content, p.x, p.y, "isInlineText");
        if (!text) {
            return "";
        }
        const q = content.mapToItem(text, p.x, p.y);
        return text.linkAt(q.x, q.y);
    }

    Flickable {
        id: flick

        anchors.fill: parent
        clip: true
        interactive: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        // Pinned: horizontal flicking is impossible, text wraps instead.
        contentWidth: width
        contentHeight: content.height + 2 * viewRoot.metrics.containerPadding

        // Keep the whole viewport hit-testable so click-to-edit also works in
        // the empty space under a short (or completely empty) note.
        MouseArea {
            id: clickCatcher

            property string hoverLink: ""

            width: flick.width
            height: Math.max(flick.contentHeight, flick.height)
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            cursorShape: clickCatcher.hoverLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor

            onPositionChanged: mouse => {
                const p = clickCatcher.mapToItem(viewRoot, mouse.x, mouse.y);
                clickCatcher.hoverLink = viewRoot.linkAt(p.x, p.y);
            }
            onExited: clickCatcher.hoverLink = ""

            onPressed: event => {
                // Take the keyboard focus on ANY press. Without this the
                // enclosing FocusScope never becomes the window's active focus
                // item, so its Keys handler -- Ctrl+E to edit, Esc to dismiss --
                // is unreachable: nothing else in a desktop containment ever
                // hands keyboard focus to a read-only view.
                viewRoot.forceActiveFocus();
                if (event.button === Qt.RightButton) {
                    // Must be accepted before popping up or the containment
                    // menu steals the event.
                    event.accepted = true;
                    viewRoot.contextMenuRequested();
                }
            }

            onClicked: event => {
                if (event.button !== Qt.LeftButton) {
                    return;
                }
                const p = clickCatcher.mapToItem(viewRoot, event.x, event.y);
                const link = viewRoot.linkAt(p.x, p.y);
                if (link !== "") {
                    viewRoot.linkClicked(link);
                } else {
                    const hit = viewRoot.blockAt(p.x, p.y);
                    viewRoot.editRequested(hit.sourceLine, hit.topY);
                }
            }
        }

        Item {
            id: content

            x: viewRoot.metrics.containerPadding
            y: viewRoot.metrics.containerPadding
            width: Math.max(0, flick.width - 2 * viewRoot.metrics.containerPadding)
            height: blockList.y + blockList.height

            Loader {
                id: titleLoader

                active: viewRoot.showInlineTitle && viewRoot.inlineTitle !== ""
                width: content.width

                sourceComponent: InlineText {
                    objectName: "inline-title"
                    html: "<p style=\"margin:0\">" + viewRoot.metrics.escapeHtml(viewRoot.inlineTitle) + "</p>"
                    decorations: []
                    metrics: viewRoot.metrics
                    fontPx: viewRoot.metrics.inlineTitleSize
                    fontWeight: viewRoot.metrics.headingWeight(1)
                    fontMetrics: viewRoot.metrics.headingMetrics(1)
                    letterSpacing: viewRoot.metrics.headingLetterSpacing(1)
                    lineHeightPx: viewRoot.metrics.inlineTitleLineHeight
                    struck: false
                    availableWidth: content.width
                }
            }

            BlockList {
                id: blockList

                y: titleLoader.item ? titleLoader.item.height + viewRoot.metrics.inlineTitleMarginBottom : 0
                blocks: viewRoot.visibleBlocks
                metrics: viewRoot.metrics
                availableWidth: content.width

                onTaskToggleRequested: (line, expected) => viewRoot.taskToggleRequested(line, expected)
            }
        }
    }
}
