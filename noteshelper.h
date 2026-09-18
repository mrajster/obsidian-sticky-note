/*
 *    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
 *    SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
 *    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
 *
 *    Ported from the Plasma "notes" applet (kdeplasma-addons, applets/notes);
 *    only noteIcon() is kept, the note loading helpers are not needed here.
 *
 *    SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QIcon>
#include <QObject>
#include <qqmlregistration.h>

class NotesHelper : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit NotesHelper(QObject *parent = nullptr);

    ~NotesHelper() override = default;

    Q_INVOKABLE QIcon noteIcon(const QString &color) const;
};
