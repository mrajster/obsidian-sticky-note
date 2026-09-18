/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

KCM.SimpleKCM {
    id: kcm

    property alias cfg_notePath: pathField.text
    property alias cfg_createIfMissing: createCheck.checked
    property alias cfg_autosaveInterval: autosaveSpin.value
    property alias cfg_showFileName: fileNameCheck.checked

    property string cfg_notePathDefault: ""
    property bool cfg_createIfMissingDefault: true
    property int cfg_autosaveIntervalDefault: 10000
    property bool cfg_showFileNameDefault: true

    // Keys owned by other pages or by the widget itself. The config dialog passes every key
    // (and its ...Default) as an initial property, so each must exist here or Qt warns
    // "Setting initial properties failed". The dialog also writes every cfg_ property back on
    // Apply, so saveConfig() refreshes these from the live configuration first: the write-back
    // is then a no-op and cannot revert state the widget changed while the dialog was open.
    readonly property var foreignKeys: ["color", "fontSize", "fontFamily", "monospaceInEditMode",
        "showInlineTitle", "showProperties", "cursorPosition", "scrollY", "pinOpen"]
    property string cfg_color
    property string cfg_colorDefault
    property int cfg_fontSize
    property int cfg_fontSizeDefault
    property string cfg_fontFamily
    property string cfg_fontFamilyDefault
    property bool cfg_monospaceInEditMode
    property bool cfg_monospaceInEditModeDefault
    property bool cfg_showInlineTitle
    property bool cfg_showInlineTitleDefault
    property bool cfg_showProperties
    property bool cfg_showPropertiesDefault
    property int cfg_cursorPosition
    property int cfg_cursorPositionDefault
    property real cfg_scrollY
    property real cfg_scrollYDefault
    property bool cfg_pinOpen
    property bool cfg_pinOpenDefault

    function saveConfig() {
        for (const key of kcm.foreignKeys) {
            kcm["cfg_" + key] = Plasmoid.configuration[key];
        }
    }

    Kirigami.FormLayout {

        RowLayout {
            Kirigami.FormData.label: i18nc("@label:textbox", "Markdown file:")
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: pathField

                Layout.fillWidth: true
                // a full note path is long; do not let the form squeeze it to nothing
                Layout.minimumWidth: Kirigami.Units.gridUnit * 20

                placeholderText: i18nc("@info:placeholder", "/home/user/Vault/Note.md")
            }

            QQC2.Button {
                id: pathButton

                icon.name: "document-open"
                text: i18nc("@action:button", "Choose Markdown File…")
                display: QQC2.AbstractButton.IconOnly

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                onClicked: pathDialog.open()
            }
        }

        QQC2.CheckBox {
            id: createCheck

            text: i18nc("@option:check", "Create the file if it does not exist")
        }

        QQC2.SpinBox {
            id: autosaveSpin

            Kirigami.FormData.label: i18nc("@label:spinbox", "Autosave delay:")

            from: 0
            to: 600000
            stepSize: 1000

            textFromValue: value => value === 0 ? i18nc("@item autosave is disabled", "Off")
                                                : i18nc("@item autosave delay in seconds", "%1 s", Math.round(value / 1000))
            valueFromText: text => {
                const seconds = parseInt(text, 10);
                return isNaN(seconds) ? 0 : seconds * 1000;
            }
        }

        QQC2.CheckBox {
            id: fileNameCheck

            text: i18nc("@option:check", "Show file name in the toolbar")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true

            type: Kirigami.MessageType.Warning
            visible: pathField.text !== "" && !pathField.text.endsWith(".md")
            text: i18n("The file does not end in .md — Obsidian will not index it.")
        }

        FileDialog {
            id: pathDialog

            title: i18nc("@title:window", "Choose Markdown File")
            nameFilters: [i18n("Markdown files (*.md *.markdown)"), i18n("All files (*)")]
            fileMode: FileDialog.OpenFile

            // Start in the folder of the note that is configured right now (i.e. the
            // vault folder the note lives in). An empty URL leaves the initial
            // directory unset, so the dialog falls back to its own default location.
            currentFolder: {
                const path = pathField.text.trim();
                const slash = path.lastIndexOf("/");
                if (slash <= 0) {
                    return "";
                }
                // Percent-encode every segment so a vault folder containing a space,
                // a "#" or a "?" does not get mangled when the string is parsed as a URL.
                return "file://" + path.substring(0, slash).split("/").map(encodeURIComponent).join("/");
            }

            onAccepted: {
                // decodeURIComponent() so that a vault folder containing spaces is not
                // stored percent-encoded (a plain .replace() would keep "%20").
                pathField.text = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
            }
        }
    }
}
