/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    NotesHelper::noteIcon() (ported from the Plasma notes applet) must hand the
    context menu a real icon for every note colour the config accepts: a
    non-null QIcon whose pixmap is non-null and has the requested size. Where
    the active theme ships the "<color>-notes" element, the pixmap must also
    actually be painted (not fully transparent).

    Breeze has no "translucent-notes" / "translucent-light-notes" element (its
    only translucent element is spelled "transluscent-notes"), so for those two
    the stock applet's icon is a correctly sized, transparent pixmap. That is
    upstream behaviour and is asserted as such rather than "fixed".

    Inside plasmashell the Plasma theme has already pointed KSvg's shared image
    set at plasma/desktoptheme; a bare test process has to do the same, so a
    Plasma::Theme is instantiated first, exactly as the host does.
*/

#include "noteshelper.h"

#include <KSvg/Svg>
#include <Plasma/Theme>

#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QTest>

class TestNotesHelper : public QObject
{
    Q_OBJECT

private:
    Plasma::Theme *m_theme = nullptr;

private Q_SLOTS:
    void initTestCase()
    {
        m_theme = new Plasma::Theme(this);
    }

    void noteIcon_data()
    {
        QTest::addColumn<QString>("color");
        const QStringList colors{
            QStringLiteral("white"), QStringLiteral("black"), QStringLiteral("red"), QStringLiteral("orange"),
            QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("pink"),
            QStringLiteral("translucent"), QStringLiteral("translucent-light"),
        };
        QCOMPARE(colors.size(), 10);
        for (const QString &c : colors) {
            QTest::newRow(qPrintable(c)) << c;
        }
    }

    void noteIcon()
    {
        QFETCH(QString, color);
        NotesHelper helper;
        const QIcon icon = helper.noteIcon(color);
        QVERIFY(!icon.isNull());

        const QSize size{32, 32};
        const QPixmap pixmap = icon.pixmap(size);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.deviceIndependentSize().toSize(), size);

        KSvg::Svg svg;
        svg.setImagePath(QStringLiteral("widgets/notes"));
        svg.setContainsMultipleImages(true);
        QVERIFY2(svg.isValid(), "widgets/notes not found in the Plasma theme");
        const bool themeHasElement = svg.hasElement(color + QStringLiteral("-notes"));
        const bool isTranslucent = color.startsWith(QStringLiteral("translucent"));
        // Every opaque paper colour must exist in the theme.
        QVERIFY2(themeHasElement || isTranslucent, qPrintable(QStringLiteral("theme lacks %1-notes").arg(color)));

        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        int painted = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 0) {
                    ++painted;
                }
            }
        }
        if (!themeHasElement) {
            QCOMPARE(painted, 0);
            return;
        }
        QVERIFY2(painted > 0, qPrintable(QStringLiteral("icon for %1 is fully transparent").arg(color)));
    }
};

QTEST_MAIN(TestNotesHelper)
#include "tst_noteshelper.moc"
