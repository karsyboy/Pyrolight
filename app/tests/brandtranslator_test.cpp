#include "brandtranslator.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QtTest>

class BrandTranslatorTest : public QObject
{
    Q_OBJECT
private slots:
    void englishFallback()
    {
        BrandTranslator translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCOMPARE(QCoreApplication::translate("SettingsView", "You must restart Moonlight for this change to take effect"),
                 QStringLiteral("You must restart Pyrolight for this change to take effect"));
        QCOMPARE(QCoreApplication::translate("Test", "Moonlight: %1 on %2").arg("host", "display"),
                 QStringLiteral("Pyrolight: host on display"));
        QCOMPARE(QCoreApplication::translate("Test", "Install the Moonlight Internet Hosting Tool."),
                 QStringLiteral("Install the Moonlight Internet Hosting Tool."));
        QVERIFY(translator.translate("Test", "No brand here").isNull());
        QCOMPARE(QCoreApplication::translate("Test", "No brand here"), QStringLiteral("No brand here"));
        QCoreApplication::removeTranslator(&translator);
    }

    void streamWindowIcon()
    {
        QSvgRenderer renderer(QFINDTESTDATA("../res/moonlight.svg"));
        QVERIFY(renderer.isValid());
        QImage icon(64, 64, QImage::Format_ARGB32_Premultiplied);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        renderer.render(&painter);
        painter.end();
        QCOMPARE(icon.pixelColor(0, 0).alpha(), 0);
        int visible = 0;
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                visible += icon.pixelColor(x, y).alpha() > 128;
            }
        }
        QVERIFY(visible > 800);
        QVERIFY(visible < 3500);
    }

    void upstreamCatalog()
    {
        BrandTranslator translator;
        QVERIFY(translator.load(QFINDTESTDATA("../languages/qml_fr.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const QString translated = QCoreApplication::translate("SettingsView", "Process gamepad input when Moonlight is in the background");
        QVERIFY(translated.contains("Pyrolight"));
        QVERIFY(!translated.contains("Moonlight"));
        QVERIFY(translated != "Process gamepad input when Pyrolight is in the background");
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_GUILESS_MAIN(BrandTranslatorTest)
#include "brandtranslator_test.moc"
