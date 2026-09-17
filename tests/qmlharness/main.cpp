/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Headless QML smoke test. Instantiates the REAL NoteView.qml and
    NoteEditor.qml against a real MarkdownNote bound to the torture fixture,
    and fails on any QML warning/error emitted while doing so.

    Run with QT_QPA_PLATFORM=offscreen; needs no display and no plasmashell.
*/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>

static QStringList g_problems;

/**
 * Everything the QML assertions need that QML itself cannot do:
 *
 *  - readSource(): hands Harness.qml the EXACT text of the shipped qml/main.qml
 *    and metadata.json, so the assertions below can be made against the real
 *    applet source instead of a copy of it. (main.qml cannot be instantiated
 *    here: it needs a running plasmashell.)
 *  - pressAt()/keyPress(): deliver a REAL QMouseEvent / QKeyEvent to the real
 *    QQuickWindow. That is the only honest way to test "the user clicked
 *    somewhere else in the window" -- the exact situation a desktop (Planar)
 *    containment puts the editor in, where nothing takes the keyboard focus
 *    away and no popup is ever deactivated.
 */
class HarnessHelper : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /** Source-tree root, derived from the fixture path CMake compiled in. */
    static QString sourceRoot()
    {
        return QFileInfo(QFileInfo(QStringLiteral(TORTURE_FIXTURE)).absolutePath()).absolutePath();
    }

    Q_INVOKABLE QString readSource(const QString &relativePath) const
    {
        QFile f(QDir(sourceRoot()).filePath(relativePath));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(f.readAll());
    }

    /** Synthesised left-button press+release at window-local (x, y). */
    Q_INVOKABLE void pressAt(QObject *windowObject, qreal x, qreal y) const
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        if (!window) {
            return;
        }
        const QPointF local(x, y);
        const QPointF global = QPointF(window->mapToGlobal(local.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
    }

    /** Synthesised key press+release, delivered to the window's active focus item. */
    Q_INVOKABLE void keyPress(QObject *windowObject, int key, int modifiers, const QString &text) const
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        if (!window) {
            return;
        }
        const auto mods = static_cast<Qt::KeyboardModifiers>(modifiers);
        QKeyEvent press(QEvent::KeyPress, key, mods, text);
        QCoreApplication::sendEvent(window, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods, text);
        QCoreApplication::sendEvent(window, &release);
    }

    /** Name of the item that currently owns the keyboard focus ("" when none). */
    Q_INVOKABLE QString activeFocusItemName(QObject *windowObject) const
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        if (!window || !window->activeFocusItem()) {
            return QString();
        }
        return QString::fromUtf8(window->activeFocusItem()->metaObject()->className());
    }
};

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const QString where = ctx.file ? QStringLiteral("%1:%2").arg(QString::fromUtf8(ctx.file)).arg(ctx.line) : QString();
    const QString line = where.isEmpty() ? msg : QStringLiteral("%1: %2").arg(where, msg);

    // Diagnostics that are NOT this applet's.
    //
    // The two Plasma ones below were each pinned down with a control run: a bare
    // PlasmaComponents3.ScrollView containing nothing but an oversized
    // Rectangle -- no NoteView, no NoteEditor, none of our QML -- reproduces
    // both of them verbatim on Plasma 6.7.5 / Qt 6.11.2. They come from
    // ScrollView.qml deriving its padding from ScrollBar.visible while
    // ScrollBar.visible is derived from the padded content size.
    static const QStringList ignore = {
        QStringLiteral("Could not find any platform plugin"),
        QStringLiteral("propertyCache.append"),
        QStringLiteral("QStandardPaths"),
        QStringLiteral("Populating font family aliases"),
        // upstream: org.kde.plasma.components/ScrollView.qml + ScrollBar.qml
        QStringLiteral("Binding loop detected for property \"visible\""),
        // upstream: emitted while the Plasma component set initialises
        QStringLiteral("installEventFilter"),
        // The torture fixture deliberately contains a real remote image
        // ("![alt text](https://example.com/i.png)"). Text.MarkdownText fetching
        // it is correct behaviour; whether the fetch succeeds is a property of
        // the network, not of this applet.
        QStringLiteral("Error transferring"),
    };
    for (const QString &n : ignore) {
        if (msg.contains(n)) {
            return;
        }
    }

    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_problems.append(line);
    }
    std::fprintf(stderr, "%s\n", qPrintable(line));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInstallMessageHandler(messageHandler);

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    HarnessHelper helper;
    engine.rootContext()->setContextProperty(QStringLiteral("torturePath"), QStringLiteral(TORTURE_FIXTURE));
    engine.rootContext()->setContextProperty(QStringLiteral("harnessHelper"), &helper);
    engine.rootContext()->setContextProperty(QStringLiteral("mainQmlSource"), helper.readSource(QStringLiteral("qml/main.qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("noteViewSource"), helper.readSource(QStringLiteral("qml/NoteView.qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("noteEditorSource"), helper.readSource(QStringLiteral("qml/NoteEditor.qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("metadataJsonSource"), helper.readSource(QStringLiteral("metadata.json")));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        std::fprintf(stderr, "FATAL: root object creation failed\n");
        QCoreApplication::exit(2);
    });

    engine.loadFromModule("obsnoteharness", "Harness");

    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "FATAL: no root object\n");
        return 2;
    }

    QObject *root = engine.rootObjects().constFirst();

    int rc = 0;
    // Let bindings settle, the Text lay out, and the KDirWatch reload fire.
    QTimer::singleShot(1500, &app, [&]() {
        const QVariant verdict = root->property("verdict");
        std::fprintf(stderr, "VERDICT: %s\n", qPrintable(verdict.toString()));
        if (!root->property("ok").toBool()) {
            std::fprintf(stderr, "FAIL: harness assertions did not pass\n");
            rc = 3;
        }
        QCoreApplication::quit();
    });

    app.exec();

    if (!g_problems.isEmpty()) {
        std::fprintf(stderr, "\nFAIL: %lld QML warning(s)/error(s):\n", qint64(g_problems.size()));
        for (const QString &p : std::as_const(g_problems)) {
            std::fprintf(stderr, "  %s\n", qPrintable(p));
        }
        return 1;
    }
    if (rc == 0) {
        std::fprintf(stderr, "\nPASS: NoteView + NoteEditor instantiated against the torture fixture with no QML diagnostics.\n");
    }
    return rc;
}

#include "main.moc"
