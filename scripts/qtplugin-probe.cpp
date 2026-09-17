// SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Install-verification helper: resolve a Plasma applet id exactly the way
// plasmashell does (Plasma::PluginLoader -> KPluginMetaData::findPluginById on
// the "plasma/applets" namespace, which searches QCoreApplication
// ::libraryPaths() == QT_PLUGIN_PATH + QT_INSTALL_PLUGINS).
//
// Exit status: 0 = applet found, 1 = not found, 2 = usage error.
// Build/run through scripts/verify-install.sh.

#include <KPluginMetaData>

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2) {
        out << "usage: qtplugin-probe <applet-id>\n";
        return 2;
    }

    const QString id = QString::fromLocal8Bit(argv[1]);
    out << "  QT_PLUGIN_PATH = " << qEnvironmentVariable("QT_PLUGIN_PATH", QStringLiteral("<unset>")) << "\n";
    out << "  libraryPaths() = " << app.libraryPaths().join(QLatin1Char(':')) << "\n";

    const KPluginMetaData md = KPluginMetaData::findPluginById(QStringLiteral("plasma/applets"), id);
    if (md.isValid()) {
        out << "  FOUND     " << id << " -> " << md.fileName() << "  (name: \"" << md.name() << "\")\n";
        return 0;
    }

    out << "  NOT-FOUND " << id << "\n";
    return 1;
}
