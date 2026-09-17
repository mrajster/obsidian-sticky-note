/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Headless QML harness. Two modes:

    1. Smoke test (no arguments; what ctest runs). Instantiates the REAL
       NoteView.qml and NoteEditor.qml against a real MarkdownNote bound to the
       torture fixture, runs Harness.qml's assertions, and fails on any QML
       warning/error emitted while doing so.

    2. Geometry dump (parity with Obsidian's reading view):

         obsnote_qmlharness --file <note.md> --width <contentWidthPx>
                            --font-px <basePx> [--inline-title 0|1]
                            --dump <out.json> [--png <out.png>]
                            [--properties 0|1] [--text-family <family>]

       Contract spelling is accepted too:
         --dump-geometry <note.md> --base-px <px> --out <json>
         [--inline-title] [--no-properties]

       Instantiates the REAL NoteView offscreen (window width = content width +
       2 * containerPadding), waits for the layout to settle, walks the block
       delegates and writes the geometry JSON consumed by
       tests/parity/compare.py. Coordinates are CSS-like px relative to the
       content box top-left INSIDE the container padding. QML warnings still
       fail the run (exit 1), but the JSON/PNG are written first.

    Run with QT_QPA_PLATFORM=offscreen; needs no display and no plasmashell.
*/

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
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
 *  - writeTextFile()/grabPng()/finish(): output side of the geometry dump.
 */
class HarnessHelper : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    int exitCode = -1;

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

    /** C++ class name of any QObject (lets the dump recognise TextEdit/Text/Loader). */
    Q_INVOKABLE QString className(QObject *object) const
    {
        return object ? QString::fromUtf8(object->metaObject()->className()) : QString();
    }

    /** Atomic UTF-8 write; true on success. */
    Q_INVOKABLE bool writeTextFile(const QString &path, const QString &text) const
    {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "DUMP: cannot open %s: %s\n", qPrintable(path), qPrintable(f.errorString()));
            return false;
        }
        f.write(text.toUtf8());
        return f.commit();
    }

    /** QQuickWindow::grabWindow() -> PNG; true on success. */
    Q_INVOKABLE bool grabPng(QObject *windowObject, const QString &path) const
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        if (!window || path.isEmpty()) {
            return false;
        }
        const QImage image = window->grabWindow();
        if (image.isNull()) {
            std::fprintf(stderr, "DUMP: grabWindow() returned a null image\n");
            return false;
        }
        if (!image.save(path, "PNG")) {
            std::fprintf(stderr, "DUMP: cannot save %s\n", qPrintable(path));
            return false;
        }
        std::fprintf(stderr, "DUMP: wrote %s (%dx%d)\n", qPrintable(path), image.width(), image.height());
        return true;
    }

    /** Plain stdout line from QML without going through qWarning (which fails the run). */
    Q_INVOKABLE void log(const QString &line) const
    {
        std::fprintf(stderr, "%s\n", qPrintable(line));
    }

    Q_INVOKABLE void finish(int code)
    {
        exitCode = code;
        QCoreApplication::quit();
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

static int reportProblems()
{
    if (g_problems.isEmpty()) {
        return 0;
    }
    std::fprintf(stderr, "\nFAIL: %lld QML warning(s)/error(s):\n", qint64(g_problems.size()));
    for (const QString &p : std::as_const(g_problems)) {
        std::fprintf(stderr, "  %s\n", qPrintable(p));
    }
    return 1;
}

namespace
{
struct DumpOptions {
    bool enabled = false;
    QString file;
    double width = 700;
    double fontPx = 16;
    bool inlineTitle = false;
    bool properties = true;
    QString out;
    QString png;
    QString textFamily = QStringLiteral("Noto Sans");
};

bool parseBool(const QString &v)
{
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || v.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
}

/** Returns false (and prints why) on a malformed command line. */
bool parseDumpOptions(const QStringList &args, DumpOptions &o)
{
    auto takeValue = [&](qsizetype &i, QString &into) {
        if (i + 1 >= args.size()) {
            std::fprintf(stderr, "ERROR: %s needs a value\n", qPrintable(args.at(i)));
            return false;
        }
        into = args.at(++i);
        return true;
    };
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        QString v;
        bool numOk = true;
        if (a == QLatin1String("--file") || a == QLatin1String("--dump-geometry")) {
            if (!takeValue(i, o.file)) {
                return false;
            }
            o.enabled = true;
        } else if (a == QLatin1String("--dump") || a == QLatin1String("--out")) {
            if (!takeValue(i, o.out)) {
                return false;
            }
            o.enabled = true;
        } else if (a == QLatin1String("--png")) {
            if (!takeValue(i, o.png)) {
                return false;
            }
        } else if (a == QLatin1String("--width")) {
            if (!takeValue(i, v)) {
                return false;
            }
            o.width = v.toDouble(&numOk);
        } else if (a == QLatin1String("--font-px") || a == QLatin1String("--base-px")) {
            if (!takeValue(i, v)) {
                return false;
            }
            o.fontPx = v.toDouble(&numOk);
        } else if (a == QLatin1String("--inline-title")) {
            // "--inline-title 0|1", or the bare contract flag "--inline-title".
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1String("--"))) {
                o.inlineTitle = parseBool(args.at(++i));
            } else {
                o.inlineTitle = true;
            }
        } else if (a == QLatin1String("--properties")) {
            if (!takeValue(i, v)) {
                return false;
            }
            o.properties = parseBool(v);
        } else if (a == QLatin1String("--no-properties")) {
            o.properties = false;
        } else if (a == QLatin1String("--text-family")) {
            if (!takeValue(i, o.textFamily)) {
                return false;
            }
        } else {
            std::fprintf(stderr, "ERROR: unknown argument %s\n", qPrintable(a));
            return false;
        }
        if (!numOk) {
            std::fprintf(stderr, "ERROR: %s: not a number: %s\n", qPrintable(a), qPrintable(v));
            return false;
        }
    }
    if (o.enabled) {
        if (o.file.isEmpty() || o.out.isEmpty()) {
            std::fprintf(stderr, "ERROR: geometry dump needs both --file <md> and --dump <json>\n");
            return false;
        }
        if (o.width <= 0 || o.fontPx <= 0) {
            std::fprintf(stderr, "ERROR: --width and --font-px must be positive\n");
            return false;
        }
        o.file = QFileInfo(o.file).absoluteFilePath();
        o.out = QFileInfo(o.out).absoluteFilePath();
        if (!o.png.isEmpty()) {
            o.png = QFileInfo(o.png).absoluteFilePath();
        }
        if (!QFileInfo::exists(o.file)) {
            std::fprintf(stderr, "ERROR: no such file: %s\n", qPrintable(o.file));
            return false;
        }
    }
    return true;
}

int runGeometryDump(QGuiApplication &app, const DumpOptions &o)
{
    QQmlApplicationEngine engine;
    HarnessHelper helper;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("harnessHelper"), &helper);
    ctx->setContextProperty(QStringLiteral("dumpFile"), o.file);
    ctx->setContextProperty(QStringLiteral("dumpWidth"), o.width);
    ctx->setContextProperty(QStringLiteral("dumpFontPx"), o.fontPx);
    ctx->setContextProperty(QStringLiteral("dumpInlineTitle"), o.inlineTitle);
    ctx->setContextProperty(QStringLiteral("dumpShowProperties"), o.properties);
    ctx->setContextProperty(QStringLiteral("dumpTextFamily"), o.textFamily);
    ctx->setContextProperty(QStringLiteral("dumpOut"), o.out);
    ctx->setContextProperty(QStringLiteral("dumpPng"), o.png);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [&helper]() {
        std::fprintf(stderr, "FATAL: GeometryDump root object creation failed\n");
        helper.finish(2);
    });

    // Prefer the copy compiled into the module (if tests/CMakeLists.txt lists
    // it); otherwise load the source-tree file under the module's resource URL
    // so the implicit module import still resolves NoteView and MarkdownNote.
    const QString moduleUrl = QStringLiteral("qrc:/qt/qml/obsnoteharness/GeometryDump.qml");
    if (QFile::exists(QStringLiteral(":/qt/qml/obsnoteharness/GeometryDump.qml"))) {
        engine.load(QUrl(moduleUrl));
    } else {
        QFile src(QFileInfo(QStringLiteral(TORTURE_FIXTURE)).absolutePath() + QStringLiteral("/qmlharness/GeometryDump.qml"));
        if (!src.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "FATAL: cannot read %s\n", qPrintable(src.fileName()));
            return 2;
        }
        engine.loadData(src.readAll(), QUrl(moduleUrl));
    }
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "FATAL: no GeometryDump root object\n");
        reportProblems();
        return 2;
    }

    // Hard ceiling so a layout that never settles cannot hang ctest.
    QTimer::singleShot(20000, &app, [&helper]() {
        std::fprintf(stderr, "FATAL: geometry dump timed out waiting for the layout to settle\n");
        helper.finish(4);
    });

    app.exec();

    const int problems = reportProblems();
    if (helper.exitCode != 0) {
        return helper.exitCode < 0 ? 2 : helper.exitCode;
    }
    return problems;
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInstallMessageHandler(messageHandler);

    QGuiApplication app(argc, argv);

    // The offscreen QPA has no platform theme: QIcon searches only ":/icons"
    // with no theme name, so every Kirigami.Icon (callout icons, fold
    // chevrons) renders blank in the dump PNG. Plasma always has a freedesktop
    // icon theme; point QIcon at the XDG icon dirs and use Breeze when it is
    // installed. The palette stays Qt's deterministic default (no user colour
    // scheme leaks in), and no layout metric depends on icons.
    if (QIcon::themeName().isEmpty()) {
        QStringList paths = QIcon::themeSearchPaths();
        const QStringList iconDirs =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"), QStandardPaths::LocateDirectory);
        for (const QString &dir : iconDirs) {
            if (!paths.contains(dir)) {
                paths.append(dir);
            }
        }
        QIcon::setThemeSearchPaths(paths);
        QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
        for (const QString &dir : iconDirs) {
            if (QFileInfo::exists(dir + QStringLiteral("/breeze/index.theme"))) {
                QIcon::setThemeName(QStringLiteral("breeze"));
                break;
            }
        }
    }

#ifdef TRANSLATION_DOMAIN
    // Plasma gives every applet its translation domain; i18nc() calls in the
    // view warn "Domain is not set" without it. Mirror the applet environment.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral(TRANSLATION_DOMAIN));
#endif

    DumpOptions dumpOptions;
    if (!parseDumpOptions(app.arguments(), dumpOptions)) {
        return 64;
    }
    if (dumpOptions.enabled) {
        return runGeometryDump(app, dumpOptions);
    }

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
    // Let bindings settle, the view lay out, and the KDirWatch reload fire.
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

    if (reportProblems() != 0) {
        return 1;
    }
    if (rc == 0) {
        std::fprintf(stderr, "\nPASS: NoteView + NoteEditor instantiated against the torture fixture with no QML diagnostics.\n");
    }
    return rc;
}

#include "main.moc"
