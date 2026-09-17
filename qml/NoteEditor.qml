/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Window
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

/**
 * Raw markdown editor. Plain text only -- RichText here would HTML-mangle the
 * vault file, and nothing in this applet is ever allowed to write anything
 * derived from QTextDocument::toMarkdown().
 */
FocusScope {
    id: editorRoot

    property alias text: area.text
    property alias cursorPosition: area.cursorPosition
    property alias length: area.length
    property alias font: area.font
    property alias canUndo: area.canUndo
    property alias canRedo: area.canRedo
    property alias canPaste: area.canPaste
    property alias selectedText: area.selectedText

    /** True while the actual TextArea holds the keyboard focus. */
    readonly property bool editorActiveFocus: area.activeFocus

    /**
     * Arms the click-out watch. The applet binds this to EDIT mode.
     *
     * WHY THIS EXISTS (the desktop click-out bug): losing activeFocus is NOT a
     * usable "the user clicked away" signal on a desktop (Planar) containment.
     * Everything the user is likely to click out onto -- the wallpaper, another
     * widget's MouseArea, a plain icon -- takes no keyboard focus at all, so the
     * TextArea keeps activeFocus forever and the note never leaves EDIT mode.
     * Inside a panel popup it works by accident (the popup window deactivates),
     * which is exactly why the bug only ever showed up on the desktop.
     */
    property bool watchOutsidePress: false

    /**
     * The bounds that count as "inside" for the click-out watch. The applet sets
     * this to its whole full representation so that pressing the toolbar or an
     * InlineMessage action button does NOT count as clicking out (an implicit
     * commit fired from a Reload button press is how a conflict banner silently
     * overwrites the file it was asked to load).
     */
    property Item outsideBoundsItem: editorRoot

    signal escapePressed()
    signal savePressed()
    /** Right click; main.qml owns the menu. */
    signal contextMenuRequested()
    /** A press landed in this window, outside outsideBoundsItem. Commit and leave EDIT. */
    signal outsidePressed()

    function forceEditorFocus() {
        area.forceActiveFocus();
    }

    /** Scene-coordinate hit test against outsideBoundsItem. Exposed so it is testable. */
    function isOutside(sceneX: real, sceneY: real): bool {
        const target = editorRoot.outsideBoundsItem ? editorRoot.outsideBoundsItem : editorRoot;
        const p = target.mapFromItem(null, sceneX, sceneY);
        return p.x < 0 || p.y < 0 || p.x > target.width || p.y > target.height;
    }

    function handleWindowPress(sceneX: real, sceneY: real) {
        if (editorRoot.watchOutsidePress && editorRoot.isOutside(sceneX, sceneY)) {
            editorRoot.outsidePressed();
        }
    }

    // ---- the click-out watch -------------------------------------------------
    //
    // A zero-cost, window-wide press probe. It is reparented onto the window's
    // contentItem so it sees presses that never reach the applet at all, and it
    // is `enabled` only while EDIT mode is live, so it is completely inert the
    // rest of the time.
    //
    // The probe MUST be a PointHandler, not a TapHandler: PointHandler is the
    // one pointer handler that only ever takes a PASSIVE grab, so the press and
    // the release both still propagate to whatever is underneath (another
    // widget, the containment, the wallpaper). A TapHandler -- even with
    // gesturePolicy DragThreshold -- consumes the release and the item below
    // never sees a click at all; the harness asserts this, because swallowing
    // every click on the desktop while the note is in EDIT mode would be a far
    // worse bug than the one this fixes.
    Item {
        id: outsideWatcher

        parent: editorRoot.Window.contentItem
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        // Above everything else in the window; handlers of the topmost item are
        // offered the point first, before any item can accept the press.
        z: 99999
        enabled: editorRoot.watchOutsidePress && editorRoot.Window.contentItem !== null

        PointHandler {
            id: outsideProbe

            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            // Never take over a grab from anything: this handler only watches.
            grabPermissions: PointerHandler.TakeOverForbidden

            onActiveChanged: {
                if (outsideProbe.active) {
                    editorRoot.handleWindowPress(outsideProbe.point.scenePosition.x,
                                                 outsideProbe.point.scenePosition.y);
                }
            }
        }
    }

    // Thin forwarders so the context menu in main.qml can drive the TextArea
    // without reaching through this FocusScope's internals.
    function undo() { area.undo(); }
    function redo() { area.redo(); }
    function cut() { area.cut(); }
    function copy() { area.copy(); }
    function paste() { area.paste(); }
    function selectAll() { area.selectAll(); }
    function deselect() { area.deselect(); }

    PlasmaComponents3.ScrollView {
        id: editScroll

        anchors.fill: parent
        clip: true

        PlasmaComponents3.TextArea {
            id: area

            focus: true

            // MANDATORY: RichText would rewrite the user's vault file as HTML.
            textFormat: TextEdit.PlainText
            // Kills the widgets/lineedit FrameSvg so the note looks like a note.
            background: null
            color: Kirigami.Theme.textColor
            wrapMode: TextEdit.Wrap
            persistentSelection: true

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    editorRoot.escapePressed();
                    event.accepted = true;
                } else if (event.matches(StandardKey.Save)) {
                    editorRoot.savePressed();
                    event.accepted = true;
                }
            }

            onPressed: event => {
                if (event.button === Qt.RightButton) {
                    // Accept first, otherwise the containment menu wins.
                    event.accepted = true;
                    area.forceActiveFocus();
                    editorRoot.contextMenuRequested();
                }
            }
        }
    }
}
