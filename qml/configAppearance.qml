/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).
    The note colour GridView picker is ported from upstream's configAppearance.qml.

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

KCM.GridViewKCM {
    id: kcm

    property string cfg_color
    property alias cfg_fontSize: fontSizeSpin.value
    // NOTE: not "property alias cfg_fontFamily: fontCombo.currentText" — ComboBox.currentText
    // is read-only, and the config dialog assigns every cfg_ value as an initial property
    // when it creates this page, which would abort page creation. Kept writable and synced
    // to the combo box below instead.
    property string cfg_fontFamily: ""
    property alias cfg_monospaceInEditMode: monoCheck.checked

    property string cfg_colorDefault: "yellow"
    property int cfg_fontSizeDefault: 0
    property string cfg_fontFamilyDefault: ""
    property bool cfg_monospaceInEditModeDefault: true

    // Keys owned by other pages or by the widget itself. The config dialog passes every key
    // (and its ...Default) as an initial property, so each must exist here or Qt warns
    // "Setting initial properties failed". The dialog also writes every cfg_ property back on
    // Apply, so saveConfig() refreshes these from the live configuration first: the write-back
    // is then a no-op and cannot revert state the widget changed while the dialog was open.
    readonly property var foreignKeys: ["notePath", "createIfMissing", "autosaveInterval", "showFileName",
        "showInlineTitle", "showProperties", "cursorPosition", "scrollY", "pinOpen"]
    property string cfg_notePath
    property string cfg_notePathDefault
    property bool cfg_createIfMissing
    property bool cfg_createIfMissingDefault
    property int cfg_autosaveInterval
    property int cfg_autosaveIntervalDefault
    property bool cfg_showFileName
    property bool cfg_showFileNameDefault
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

    readonly property bool inPanel: [PlasmaCore.Types.TopEdge, PlasmaCore.Types.RightEdge, PlasmaCore.Types.BottomEdge, PlasmaCore.Types.LeftEdge].includes(Plasmoid.location)

    /** [{ text: <shown>, value: <stored font family, "" = follow the theme> }, …] */
    readonly property var fontModel: [{
        text: i18nc("@item:inlistbox font family", "Theme default"),
        value: ""
    }].concat(Qt.fontFamilies().map(family => ({ text: family, value: family })))

    extraFooterTopPadding: true

    header: Kirigami.FormLayout {

        QQC2.SpinBox {
            id: fontSizeSpin

            Kirigami.FormData.label: i18nc("@label:spinbox", "Text font size:")

            from: 4
            to: 128

            textFromValue: value => i18n("%1pt", value)
            valueFromText: text => {
                const points = parseInt(text, 10);
                return isNaN(points) ? fontSizeSpin.from : points;
            }
        }

        QQC2.ComboBox {
            id: fontCombo

            Kirigami.FormData.label: i18nc("@label:listbox", "Text font:")

            Layout.fillWidth: true

            model: kcm.fontModel
            textRole: "text"
            valueRole: "value"

            function syncFromConfig(): void {
                const index = indexOfValue(kcm.cfg_fontFamily);
                currentIndex = index >= 0 ? index : 0;
            }

            onActivated: index => {
                kcm.cfg_fontFamily = valueAt(index);
            }

            Component.onCompleted: syncFromConfig()

            Connections {
                target: kcm
                function onCfg_fontFamilyChanged(): void {
                    fontCombo.syncFromConfig();
                }
            }
        }

        QQC2.CheckBox {
            id: monoCheck

            text: i18nc("@option:check", "Use a monospace font while editing")
        }
    }

    view.implicitCellWidth: Kirigami.Units.gridUnit * 8
    view.implicitCellHeight: view.implicitCellWidth

    view.model: {
        let model = ["white", "black", "red", "orange", "yellow", "green", "blue", "pink", "translucent"];
        if (!kcm.inPanel) {
            model.push("translucent-light");
        }
        return model;
    }
    /** The model entry that represents a stored colour; in a panel translucent-light shows as translucent. */
    function shownColor(color: string): string {
        return kcm.inPanel && color === "translucent-light" ? "translucent" : color;
    }

    view.currentIndex: view.model.indexOf(kcm.shownColor(kcm.cfg_color))
    // Guarded (upstream assigns unconditionally): only write when the index names a different
    // colour than the one stored. Otherwise a -1 index writes undefined into a string, and in a
    // panel a stored "translucent-light" is rewritten to "translucent", re-evaluating
    // currentIndex from inside its own change handler (a binding loop).
    view.onCurrentIndexChanged: {
        const color = view.model[view.currentIndex];
        if (color !== undefined && color !== kcm.shownColor(kcm.cfg_color)) {
            kcm.cfg_color = color;
        }
    }

    view.delegate: QQC2.ItemDelegate {
        id: delegate

        required property string modelData

        width: GridView.view.cellWidth
        height: GridView.view.cellHeight
        highlighted: GridView.isCurrentItem

        text: {
            switch (modelData) {
            case "white": return i18n("A white sticky note");
            case "black": return i18n("A black sticky note");
            case "red": return i18n("A red sticky note");
            case "orange": return i18n("An orange sticky note");
            case "yellow": return i18n("A yellow sticky note");
            case "green": return i18n("A green sticky note");
            case "blue": return i18n("A blue sticky note");
            case "pink": return i18n("A pink sticky note");
            case "translucent": return i18n("A transparent sticky note");
            case "translucent-light": return i18n("A transparent sticky note with light text");
            }
            return "";
        }

        contentItem: Item { // wrapper to prevent automatic resizing
            KSvg.SvgItem {
                id: thumbnail

                anchors.centerIn: parent
                width: delegate.availableHeight
                height: delegate.availableHeight
                imagePath: "widgets/notes"
                elementId: delegate.modelData + "-notes"

                QQC2.Label {
                    id: thumbnailLabel
                    anchors.centerIn: parent
                    // this isn't a frameSVG, the default SVG margins take up around 7% of the frame size, so we use that
                    width: Math.round(parent.width - thumbnail.width * 0.07) - 2 * Kirigami.Units.smallSpacing
                    height: Math.round(parent.height - thumbnail.height * 0.07)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    text: delegate.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap

                    //this is deliberately _NOT_ the theme color as we are over a known bright background
                    //an unknown colour over a known colour is a bad move as you end up with white on yellow
                    color: {
                        if (kcm.inPanel && delegate.modelData === "translucent") {
                            return Kirigami.Theme.textColor;
                        } else if (delegate.modelData === "black" || delegate.modelData === "translucent-light") {
                            return "#dfdfdf";
                        } else {
                            return "#202020";
                        }
                    }
                }
            }
        }

        onClicked: {
            kcm.cfg_color = modelData;
        }
    }
}
