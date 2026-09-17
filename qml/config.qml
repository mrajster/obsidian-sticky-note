/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18nc("@title", "File")
        icon: "text-markdown"
        source: "configNote.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Appearance")
        icon: "preferences-desktop-font"
        source: "configAppearance.qml"
    }
}
