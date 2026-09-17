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

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: kcm

    property alias cfg_fontSize: fontSizeSpin.value
    // NOTE: not "property alias cfg_fontFamily: fontCombo.currentText" — ComboBox.currentText
    // is read-only, and the config dialog assigns every cfg_ value as an initial property
    // when it creates this page, which would abort page creation. Kept writable and synced
    // to the combo box below instead.
    property string cfg_fontFamily: ""
    property alias cfg_monospaceInEditMode: monoCheck.checked

    property int cfg_fontSizeDefault: 0
    property string cfg_fontFamilyDefault: ""
    property bool cfg_monospaceInEditModeDefault: true

    /** [{ text: <shown>, value: <stored font family, "" = follow the theme> }, …] */
    readonly property var fontModel: [{
        text: i18nc("@item:inlistbox font family", "Theme default"),
        value: ""
    }].concat(Qt.fontFamilies().map(family => ({ text: family, value: family })))

    Kirigami.FormLayout {

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
}
