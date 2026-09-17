/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

/**
 * Read-only rendered view of the note.
 *
 * The source-line index of every task list item is carried inside the rendered
 * document as an "obsnote:toggle/<line>" link href (contract §7), so a checkbox
 * hit test is nothing more than Text.linkAt() -- there is deliberately no
 * rendered-position to source-position mapping anywhere in this file.
 */
Item {
    id: viewRoot

    /** The backend this view renders. Kept typed so qmlcachegen can check it. */
    required property MarkdownNote note

    /** The markdown handed to Text.MarkdownText, i.e. note.renderedText. */
    property alias markdown: body.text
    property alias fontSize: body.font.pointSize
    property alias fontFamily: body.font.family

    /** "" when the pointer is not over a link. */
    readonly property string linkUnderCursor: body.hoveredLink

    /** Left click on something that is not a link: the user wants to edit. */
    signal editRequested()
    /** Left click on a link; main.qml decides what the link means. */
    signal linkClicked(string link)
    /** Right click; main.qml owns the menu, so it has to pop it up. */
    signal contextMenuRequested()

    /** Current vertical scroll offset, to be stored in Plasmoid.configuration.scrollY. */
    function saveScroll(): real {
        const flickable = scrollView.contentItem as Flickable;
        return flickable ? flickable.contentY : 0;
    }

    /** Re-apply a previously stored offset, clamped to the current content height. */
    function restoreScroll(y: real) {
        const flickable = scrollView.contentItem as Flickable;
        if (!flickable || y <= 0) {
            return;
        }
        const maxY = Math.max(0, flickable.contentHeight - flickable.height);
        flickable.contentY = Math.min(y, maxY);
    }

    PlasmaComponents3.ScrollView {
        id: scrollView

        anchors.fill: parent
        clip: true

        Text {
            id: body

            // Pinning the width to availableWidth is what keeps the ScrollBar out
            // of the width calculation, so the upstream "scrollBarNecessary"
            // delayed Binding hack is not needed here and must not be copied.
            width: scrollView.availableWidth
            // Keep the whole viewport hit-testable so click-to-edit also works
            // in the empty space under a short (or completely empty) note.
            height: Math.max(implicitHeight, scrollView.availableHeight)
            verticalAlignment: Text.AlignTop

            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            color: Kirigami.Theme.textColor
            linkColor: Kirigami.Theme.linkColor

            onLinkActivated: link => viewRoot.linkClicked(link)

            MouseArea {
                id: clickCatcher

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // hoverEnabled stays false on purpose: hover events then fall
                // through to the Text underneath and keep hoveredLink alive.
                cursorShape: body.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor

                onPressed: event => {
                    // Take the keyboard focus on ANY press. Without this the
                    // enclosing FocusScope never becomes the window's active
                    // focus item, so its Keys handler -- Ctrl+E to edit, Esc to
                    // dismiss -- is unreachable: nothing else in a desktop
                    // containment ever hands keyboard focus to a read-only Text.
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
                    const link = body.linkAt(event.x, event.y);
                    if (link !== "") {
                        viewRoot.linkClicked(link);
                    } else {
                        viewRoot.editRequested();
                    }
                }
            }
        }
    }
}
