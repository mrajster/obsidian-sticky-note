/*
 *    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
 *    SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
 *    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
 *
 *    Ported from the Plasma "notes" applet (kdeplasma-addons, applets/notes).
 *
 *    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "noteshelper.h"

#include <QIconEngine>

#include "noteiconengine.h"

NotesHelper::NotesHelper(QObject *parent)
    : QObject(parent)
{
}

QIcon NotesHelper::noteIcon(const QString &color) const
{
    return QIcon{new NoteIconEngine{color}};
}
