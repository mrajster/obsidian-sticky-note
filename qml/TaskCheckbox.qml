/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Shapes

/**
 * Obsidian's task checkbox: 1em square, 1 px border, 0.25em radius.
 *
 * It only ever asks for a toggle. The write itself is MarkdownNote::toggleTask
 * (re-reads the file, validates the expected line, flips one character).
 */
Item {
    id: checkbox

    required property bool checked
    required property bool interactive
    required property ObsidianMetrics metrics

    /**
     * This box's exact layout position in a frame whose origin sits on a whole
     * device pixel (the parent passes its BlockList position in). The item
     * itself keeps the exact CSS rect; only what it PAINTS is snapped, the way
     * Chromium paints: the box on the rounded position, the check mask placed
     * from the exact one (so the glyph can sit a pixel apart from the box).
     * Unsnapped, a 1 px border smears over two rows on the GPU renderer.
     */
    property real exactX: checkbox.x
    property real exactY: checkbox.y
    readonly property real paintX: Math.round(checkbox.exactX) - checkbox.exactX
    readonly property real paintY: Math.round(checkbox.exactY) - checkbox.exactY

    signal toggled()

    /** Marker for NoteView's hit test. */
    readonly property bool isTaskCheckbox: true

    width: checkbox.metrics.checkboxSize
    height: checkbox.metrics.checkboxSize

    Rectangle {
        x: checkbox.paintX
        y: checkbox.paintY
        width: checkbox.width
        height: checkbox.height
        radius: checkbox.metrics.checkboxRadius
        border.width: checkbox.checked ? 0 : checkbox.metrics.checkboxBorder
        border.color: checkbox.metrics.mutedTextColor
        color: checkbox.checked ? checkbox.metrics.accentColor : "transparent"
    }

    /**
     * Obsidian's check mark: the filled glyph of app.css
     * `input[type=checkbox]:checked:after` -- a 12x10 px SVG (viewBox 0 0 12 8,
     * path translated by (-4,-6)) used as a mask at `mask-size: 65%`,
     * `mask-position: 52% 52%` over the 1em box, in the background colour.
     *
     * Chromium rasterises that mask into a whole-pixel rectangle (for the 16 px
     * box: 10x9 px at (3,4)) and fits the viewBox into it; measured against
     * Obsidian's own pixels that snapped fit is the glyph it paints. The path is
     * given in its SVG coordinates and mapped the same way, so it scales with
     * the font size.
     */
    readonly property var checkGlyph: [
        "M", 8.1043257, 14.0367999,
        "L", 4.52468714, 10.5420499,
        "C", 4.32525014, 10.3497722, 4.32525014, 10.0368095, 4.52468714, 9.8424863,
        "L", 5.24777413, 9.1439454,
        "C", 5.44721114, 8.95166768, 5.77142411, 8.95166768, 5.97086112, 9.1439454,
        "L", 8.46638057, 11.5903727,
        "L", 14.0291389, 6.1442083,
        "C", 14.2285759, 5.95193057, 14.5527889, 5.95193057, 14.7522259, 6.1442083,
        "L", 15.4753129, 6.84377194,
        "C", 15.6747499, 7.03604967, 15.6747499, 7.35003511, 15.4753129, 7.54129009,
        "L", 8.82741268, 14.0367999,
        "C", 8.62797568, 14.2290777, 8.3037627, 14.2290777, 8.1043257, 14.0367999,
        "Z"
    ]
    readonly property string checkPath: {
        const s = checkbox.width;
        const maskW = 0.65 * s;                 // mask-size: 65%
        const maskH = maskW * 10 / 12;          // the SVG's 12x10 intrinsic ratio
        // mask-position: 52% 52%, rounded to a device pixel from the exact
        // box position, expressed relative to where the box was painted.
        const rx = Math.round(checkbox.exactX + (s - maskW) * 0.52) - Math.round(checkbox.exactX);
        const ry = Math.round(checkbox.exactY + (s - maskH) * 0.52) - Math.round(checkbox.exactY);
        const rw = Math.round(maskW);
        const rh = Math.round(maskH);
        const k = Math.min(rw / 12, rh / 8);    // viewBox 0 0 12 8, xMidYMid meet
        const cx = rx + (rw - 12 * k) / 2;
        const cy = ry + (rh - 8 * k) / 2;
        const g = checkbox.checkGlyph;
        let out = "";
        let n = 0;
        for (let i = 0; i < g.length; ++i) {
            if (typeof g[i] === "string") {
                out += (out === "" ? "" : " ") + g[i];
                n = 0;
            } else {
                // translate(-4,-6), then the fit above.
                const v = n % 2 === 0 ? cx + k * (g[i] - 4) : cy + k * (g[i] - 6);
                out += (n % 2 === 0 ? " " : ",") + v.toFixed(4);
                ++n;
            }
        }
        return out;
    }

    Shape {
        x: checkbox.paintX
        y: checkbox.paintY
        width: checkbox.width
        height: checkbox.height
        visible: checkbox.checked
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: -1
            fillColor: checkbox.metrics.backgroundColor
            fillRule: ShapePath.WindingFill

            PathSvg { path: checkbox.checkPath }
        }
    }

    MouseArea {
        id: hit

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        enabled: checkbox.interactive
        cursorShape: Qt.PointingHandCursor

        // Keep the keyboard focus inside the view (Ctrl+E / Esc) even though
        // this press never reaches NoteView's background MouseArea.
        onPressed: checkbox.forceActiveFocus()
        onClicked: checkbox.toggled()
    }
}
