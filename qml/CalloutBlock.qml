/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-FileCopyrightText: 2022 Lucide Contributors
    SPDX-FileCopyrightText: 2013-2022 Cole Bemis
    SPDX-License-Identifier: GPL-2.0-or-later AND ISC

    The code in this file is GPL-2.0-or-later. The icon path data
    (calloutIconPaths) is Lucide's, the icon set Obsidian draws callouts with,
    and is ISC-licensed (see LICENSES/ISC.txt).
*/

import QtQuick
import QtQuick.Shapes

/**
 * "> [!type] Title": padded rounded box, icon + title row, body BlockList.
 *
 *   box    = padTop + title (1.3em line) + body + padBottom
 *   body   = children (first child carries a 1em gap) + a trailing 1em
 *   title  = icon (1.125em) at padLeft, 0.25em gap, 600-weight text
 *
 * Foldable callouts ("-"/"+") fold and unfold in the view only; that is a
 * display state and never touches the file.
 */
Item {
    id: callout

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth
    property NotePalette notePalette: null

    signal toggleRequested(int sourceLine, string expectedLineText)

    property bool folded: !!callout.block.folded

    readonly property string calloutType: callout.block.calloutType || "note"
    readonly property bool foldable: !!callout.block.foldable
    readonly property var children_: callout.block.children || []
    readonly property real innerX: callout.metrics.calloutPadLeft
    readonly property real innerWidth: Math.max(0, callout.width - callout.metrics.calloutPadLeft - callout.metrics.calloutPadRight)
    readonly property real bodyHeight: callout.folded || !body.item ? 0 : body.item.height + callout.metrics.em

    /** Theme colour per callout family (colours are Plasma's, not Obsidian's). */
    readonly property color typeColor: {
        switch (callout.calloutType) {
        case "success":
        case "tip":
            return callout.metrics.positiveColor;
        case "warning":
        case "question":
            return callout.metrics.neutralColor;
        case "failure":
        case "danger":
        case "bug":
            return callout.metrics.negativeColor;
        case "quote":
        case "example":
            return callout.metrics.mutedTextColor;
        default:
            return callout.metrics.accentColor;
        }
    }

    /**
     * Obsidian 1.13.7 draws each callout family with a Lucide icon
     * (app.css --callout-icon): an 18 px SVG, viewBox 24, stroke 1.75 user
     * units (--icon-m-stroke-width), round caps and joins, in the callout
     * colour. The paths are drawn here directly, so the glyph, its size and its
     * tint match Obsidian whatever icon theme Plasma uses.
     */
    readonly property var calloutIconPaths: ({
        // lucide-pencil
        "note": "M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z M15 5l4 4",
        // lucide-clipboard-list
        "abstract": "M9 2h6a1 1 0 0 1 1 1v2a1 1 0 0 1 -1 1h-6a1 1 0 0 1 -1 -1v-2a1 1 0 0 1 1 -1z M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2 M12 11h4 M12 16h4 M8 11h.01 M8 16h.01",
        // lucide-info
        "info": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0 M12 16v-4 M12 8h.01",
        // lucide-check-circle-2
        "todo": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0 M9 12l2 2 4-4",
        // lucide-flame
        "tip": "M12 3q1 4 4 6.5t3 5.5a1 1 0 0 1-14 0 5 5 0 0 1 1-3 1 1 0 0 0 5 0c0-2-1.5-3-1.5-5q0-2 2.5-4",
        // lucide-check
        "success": "M20 6 9 17l-5-5",
        // lucide-help-circle
        "question": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0 M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3 M12 17h.01",
        // lucide-alert-triangle
        "warning": "M21.73 18l-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3 M12 9v4 M12 17h.01",
        // lucide-x
        "failure": "M18 6 6 18 M6 6l12 12",
        // lucide-zap
        "danger": "M4 14a1 1 0 0 1-.78-1.63l9.9-10.2a.5.5 0 0 1 .86.46l-1.92 6.02A1 1 0 0 0 13 10h7a1 1 0 0 1 .78 1.63l-9.9 10.2a.5.5 0 0 1-.86-.46l1.92-6.02A1 1 0 0 0 11 14z",
        // lucide-bug
        "bug": "M12 20v-9 M14 7a4 4 0 0 1 4 4v3a6 6 0 0 1-12 0v-3a4 4 0 0 1 4-4z M14.12 3.88 16 2 M21 21a4 4 0 0 0-3.81-4 M21 5a4 4 0 0 1-3.55 3.97 M22 13h-4 M3 21a4 4 0 0 1 3.81-4 M3 5a4 4 0 0 0 3.55 3.97 M6 13H2 M8 2l1.88 1.88 M9 7.13V6a3 3 0 1 1 6 0v1.13",
        // lucide-list
        "example": "M3 5h.01 M3 12h.01 M3 19h.01 M8 5h13 M8 12h13 M8 19h13",
        // lucide-quote
        "quote": "M16 3a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2 1 1 0 0 1 1 1v1a2 2 0 0 1-2 2 1 1 0 0 0-1 1v2a1 1 0 0 0 1 1 6 6 0 0 0 6-6V5a2 2 0 0 0-2-2z M5 3a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2 1 1 0 0 1 1 1v1a2 2 0 0 1-2 2 1 1 0 0 0-1 1v2a1 1 0 0 0 1 1 6 6 0 0 0 6-6V5a2 2 0 0 0-2-2z",
        // lucide-chevron-down
        "fold": "M6 9l6 6 6-6"
    })

    readonly property string iconPath: callout.calloutIconPaths[callout.calloutType] || callout.calloutIconPaths["note"]

    /** A Lucide icon: 24-unit path data scaled into a square item, stroked in @p color. */
    component LucideIcon: Item {
        id: glyph

        property string path
        property color color
        /** In the icon's own 24-unit space, like SVG stroke-width. */
        property real strokeWidth: 1.75

        Shape {
            width: 24
            height: 24
            scale: glyph.width / 24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: glyph.color
                strokeWidth: glyph.strokeWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathSvg { path: glyph.path }
            }
        }
    }

    width: callout.availableWidth
    height: box.height

    Rectangle {
        id: box

        width: callout.width
        height: callout.metrics.calloutPadTop + callout.metrics.calloutTitleLineHeight
            + callout.bodyHeight + callout.metrics.calloutPadBottom
        radius: callout.metrics.calloutRadius
        // NotePalette.calloutTint: the callout colour at 10 %, or a contrast-safe lift where that tint
        // would push the body text under 4.5 : 1 (dark text on red paper).
        color: callout.notePalette
            ? callout.notePalette.calloutTint(callout.typeColor)
            : Qt.rgba(callout.typeColor.r, callout.typeColor.g, callout.typeColor.b, 0.1)
    }

    Item {
        id: titleRow

        objectName: "callout-title"

        x: callout.innerX
        y: callout.metrics.calloutPadTop
        width: callout.innerWidth
        height: callout.metrics.calloutTitleLineHeight

        LucideIcon {
            id: icon

            objectName: "callout-icon"

            width: callout.metrics.calloutIconSize
            height: callout.metrics.calloutIconSize
            y: (titleRow.height - height) / 2
            path: callout.iconPath
            color: callout.typeColor
        }

        InlineText {
            id: title

            x: callout.metrics.calloutIconSize + callout.metrics.calloutTitleGap
            html: callout.block.titleHtml || ""
            decorations: callout.block.titleDecorations || []
            marks: callout.block.titleMarks || []
            metrics: callout.metrics
            fontPx: callout.metrics.em
            fontWeight: callout.metrics.calloutTitleWeight
            fontMetrics: callout.metrics.fmBold
            letterSpacing: 0
            lineHeightPx: callout.metrics.calloutTitleLineHeight
            struck: false
            color: callout.typeColor
            availableWidth: Math.max(0, titleRow.width - x - (callout.foldable ? chevron.width + callout.metrics.calloutTitleGap : 0))
        }

        // .callout-fold: lucide-chevron-down, turned -90deg while collapsed.
        LucideIcon {
            id: chevron

            visible: callout.foldable
            x: title.x + Math.min(title.naturalWidth, title.width) + callout.metrics.calloutTitleGap
            width: callout.metrics.calloutIconSize
            height: callout.metrics.calloutIconSize
            y: (titleRow.height - height) / 2
            rotation: callout.folded ? -90 : 0
            path: callout.calloutIconPaths["fold"]
            color: callout.typeColor
        }

        MouseArea {
            anchors.fill: parent
            enabled: callout.foldable
            cursorShape: Qt.PointingHandCursor
            onPressed: titleRow.forceActiveFocus()
            onClicked: callout.folded = !callout.folded
        }
    }

    Loader {
        id: body

        x: callout.innerX
        y: titleRow.y + titleRow.height
        visible: !callout.folded

        Component.onCompleted: body.setSource("BlockList.qml", {
            blocks: callout.children_,
            metrics: callout.metrics,
            notePalette: callout.notePalette,
            availableWidth: callout.innerWidth,
            inCallout: true
        })
    }

    Binding {
        target: body.item
        when: body.status === Loader.Ready
        property: "availableWidth"
        value: callout.innerWidth
    }

    Binding {
        target: body.item
        when: body.status === Loader.Ready
        property: "blocks"
        value: callout.children_
    }

    Connections {
        target: body.item
        ignoreUnknownSignals: true

        function onTaskToggleRequested(sourceLine: int, expectedLineText: string) {
            callout.toggleRequested(sourceLine, expectedLineText);
        }
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        const m = callout.metrics;
        out.push(m.geometryEntry("callout", callout.block, callout, origin, { h: box.height, fontPx: m.em, lineHeightPx: m.lhBody }));
        out.push(m.geometryEntry("callout-title", callout.block, titleRow, origin, {
            textStartX: title.x,
            firstLineBaselineY: title.firstBaselineY,
            lineCount: title.lineCount,
            fontPx: m.em,
            lineHeightPx: title.lineHeightPx
        }));
        out.push(m.geometryEntry("callout-icon", callout.block, titleRow, origin, {
            w: icon.width, h: titleRow.height, fontPx: m.em, lineHeightPx: title.lineHeightPx
        }));
        if (!callout.folded && body.item) {
            out.push(m.geometryEntry("callout-content", callout.block, body.item, origin, {
                h: callout.bodyHeight, fontPx: m.em, lineHeightPx: m.lhBody
            }));
            const firstChild = out.length;
            body.item.dumpGeometry(out, origin);
            // Obsidian tags everything inside a quote/callout with the
            // container's first source line; the dump follows that convention.
            for (let k = firstChild; k < out.length; ++k) {
                out[k].sourceLine = callout.block.sourceLine;
            }
        }
    }
}
