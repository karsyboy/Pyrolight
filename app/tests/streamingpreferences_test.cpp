#include "settings/streamingpreferences.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// StreamingPreferences only needs these platform queries to choose the normal
// default window mode. Stubbing them keeps this persistence test independent of
// SDL and the window-system integration layer.
namespace WMUtils {
bool isRunningWayland() { return false; }
bool isGpuSlow() { return false; }
}

class StreamingPreferencesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_SettingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("MoonlightProfileTests"));
        QCoreApplication::setApplicationName(QStringLiteral("MoonlightProfileTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_SettingsDirectory.path());
        m_Preferences = StreamingPreferences::get();
    }

    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
        m_Preferences->reload();
    }

    void freshInstallCreatesDefault()
    {
        QCOMPARE(m_Preferences->activeProfileId(), QStringLiteral("default"));
        QCOMPARE(m_Preferences->activeProfileName(), QStringLiteral("Default"));
        QCOMPARE(m_Preferences->profileIds(), QStringList{QStringLiteral("default")});
        QCOMPARE(m_Preferences->width, 1280);
        QCOMPARE(m_Preferences->height, 720);
        QCOMPARE(m_Preferences->fps, 60);

        QSettings settings;
        QCOMPARE(settings.value(QStringLiteral("profiles/version")).toInt(), 1);
        QCOMPARE(settings.value(QStringLiteral("profiles/default/name")).toString(),
                 QStringLiteral("Default"));
    }

    void migratesLegacySettingsExactlyOnce()
    {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("width"), 3840);
        settings.setValue(QStringLiteral("height"), 2160);
        settings.setValue(QStringLiteral("fps"), 120);
        settings.setValue(QStringLiteral("bitrate"), 400000);
        settings.setValue(QStringLiteral("autoadjustbitrate"), false);
        settings.setValue(QStringLiteral("videocfg"),
                          static_cast<int>(StreamingPreferences::VCC_FORCE_PYROWAVE));
        settings.setValue(QStringLiteral("hdr"), true);
        settings.setValue(QStringLiteral("yuv444"), true);
        settings.sync();

        m_Preferences->reload();
        QCOMPARE(m_Preferences->width, 3840);
        QCOMPARE(m_Preferences->fps, 120);
        QCOMPARE(m_Preferences->bitrateKbps, 400000);
        QCOMPARE(m_Preferences->videoCodecConfig, StreamingPreferences::VCC_FORCE_PYROWAVE);
        QVERIFY(m_Preferences->enableHdr);
        QVERIFY(m_Preferences->enableYUV444);

        settings.setValue(QStringLiteral("width"), 640);
        settings.sync();
        m_Preferences->reload();
        QCOMPARE(m_Preferences->width, 3840);
        QCOMPARE(m_Preferences->bitrateKbps, 400000);
        QCOMPARE(m_Preferences->videoCodecConfig, StreamingPreferences::VCC_FORCE_PYROWAVE);
        QVERIFY(m_Preferences->enableHdr);
        QVERIFY(m_Preferences->enableYUV444);
    }

    void crudAndActiveProfilePersistence()
    {
        m_Preferences->width = 1280;
        m_Preferences->height = 800;
        m_Preferences->fps = 90;
        m_Preferences->bitrateKbps = 50000;
        m_Preferences->autoAdjustBitrate = false;
        QVERIFY(m_Preferences->createProfile(QStringLiteral("Steam Deck")));
        const QString steamDeckId = m_Preferences->activeProfileId();

        QVERIFY(m_Preferences->duplicateProfile(steamDeckId, QStringLiteral("Steam Deck Copy")));
        const QString copyId = m_Preferences->activeProfileId();
        QVERIFY(m_Preferences->renameProfile(copyId, QStringLiteral("Handheld")));
        QCOMPARE(m_Preferences->activeProfileName(), QStringLiteral("Handheld"));
        QVERIFY(!m_Preferences->createProfile(QStringLiteral(" handheld ")));
        QVERIFY(!m_Preferences->renameProfile(QStringLiteral("default"), QStringLiteral("Other")));
        QVERIFY(!m_Preferences->deleteProfile(QStringLiteral("default")));

        QVERIFY(m_Preferences->deleteProfile(copyId));
        QCOMPARE(m_Preferences->activeProfileId(), QStringLiteral("default"));
        QVERIFY(m_Preferences->activateProfile(steamDeckId));
        m_Preferences->save();
        m_Preferences->reload();
        QCOMPARE(m_Preferences->activeProfileId(), steamDeckId);
        QCOMPARE(m_Preferences->width, 1280);
        QCOMPARE(m_Preferences->height, 800);
        QCOMPARE(m_Preferences->fps, 90);
        QCOMPARE(m_Preferences->bitrateKbps, 50000);
    }

    void switchingRestoresAllStreamingValuesAndKeepsGlobals()
    {
        QVERIFY(m_Preferences->createProfile(QStringLiteral("Desktop 4K120")));
        const QString desktopId = m_Preferences->activeProfileId();

        m_Preferences->enableMdns = false;
        m_Preferences->language = StreamingPreferences::LANG_FR;
        m_Preferences->width = 3840;
        m_Preferences->height = 2160;
        m_Preferences->fps = 120;
        m_Preferences->bitrateKbps = 400000;
        m_Preferences->autoAdjustBitrate = false;
        m_Preferences->unlockBitrate = true;
        m_Preferences->videoCodecConfig = StreamingPreferences::VCC_FORCE_PYROWAVE;
        m_Preferences->enableHdr = true;
        m_Preferences->enableYUV444 = true;
        m_Preferences->videoDecoderSelection = StreamingPreferences::VDS_FORCE_HARDWARE;
        m_Preferences->rendererSelection = StreamingPreferences::RS_VULKAN;
        m_Preferences->audioConfig = StreamingPreferences::AC_71_SURROUND;
        m_Preferences->enableVsync = false;
        m_Preferences->framePacing = true;
        m_Preferences->gameOptimizations = false;
        m_Preferences->playAudioOnHost = true;
        m_Preferences->showPerformanceOverlay = true;
        m_Preferences->muteOnFocusLoss = true;
        m_Preferences->windowMode = StreamingPreferences::WM_WINDOWED;
        m_Preferences->packetSize = 1392;
        m_Preferences->save();

        QVERIFY(m_Preferences->activateProfile(QStringLiteral("default")));
        QCOMPARE(m_Preferences->width, 1280);
        QVERIFY(m_Preferences->activateProfile(desktopId));
        QCOMPARE(m_Preferences->width, 3840);
        QCOMPARE(m_Preferences->height, 2160);
        QCOMPARE(m_Preferences->fps, 120);
        QCOMPARE(m_Preferences->bitrateKbps, 400000);
        QVERIFY(!m_Preferences->autoAdjustBitrate);
        QVERIFY(m_Preferences->enableHdr);
        QVERIFY(m_Preferences->enableYUV444);
        QCOMPARE(m_Preferences->videoCodecConfig, StreamingPreferences::VCC_FORCE_PYROWAVE);
        QVERIFY(m_Preferences->unlockBitrate);
        QCOMPARE(m_Preferences->videoDecoderSelection, StreamingPreferences::VDS_FORCE_HARDWARE);
        QCOMPARE(m_Preferences->rendererSelection, StreamingPreferences::RS_VULKAN);
        QCOMPARE(m_Preferences->audioConfig, StreamingPreferences::AC_71_SURROUND);
        QVERIFY(!m_Preferences->enableVsync);
        QVERIFY(m_Preferences->framePacing);
        QVERIFY(!m_Preferences->gameOptimizations);
        QVERIFY(m_Preferences->playAudioOnHost);
        QVERIFY(m_Preferences->showPerformanceOverlay);
        QVERIFY(m_Preferences->muteOnFocusLoss);
        QCOMPARE(m_Preferences->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(m_Preferences->packetSize, 1392);
        QVERIFY(!m_Preferences->enableMdns);
        QCOMPARE(m_Preferences->language, StreamingPreferences::LANG_FR);
    }

    void automaticBitrateRecalculatesWhileManualBitrateDoesNot()
    {
        m_Preferences->width = 3840;
        m_Preferences->height = 2160;
        m_Preferences->fps = 120;
        m_Preferences->videoCodecConfig = StreamingPreferences::VCC_FORCE_PYROWAVE;
        m_Preferences->autoAdjustBitrate = true;
        m_Preferences->bitrateKbps = 12345;
        QVERIFY(m_Preferences->createProfile(QStringLiteral("Automatic")));
        const QString automaticId = m_Preferences->activeProfileId();

        QVERIFY(m_Preferences->activateProfile(QStringLiteral("default")));
        QVERIFY(m_Preferences->activateProfile(automaticId));
        QCOMPARE(m_Preferences->bitrateKbps,
                 StreamingPreferences::getDefaultBitrate(3840, 2160, 120, false,
                                                         StreamingPreferences::VCC_FORCE_PYROWAVE,
                                                         false));

        m_Preferences->autoAdjustBitrate = false;
        m_Preferences->bitrateKbps = 412345;
        m_Preferences->save();
        QVERIFY(m_Preferences->activateProfile(QStringLiteral("default")));
        QVERIFY(m_Preferences->activateProfile(automaticId));
        QCOMPARE(m_Preferences->bitrateKbps, 412345);
    }

    void corruptActiveProfileFallsBackAndSignalsUiRefresh()
    {
        QVERIFY(m_Preferences->createProfile(QStringLiteral("Temporary")));
        QSignalSpy loadedSpy(m_Preferences, &StreamingPreferences::profileLoaded);
        QVERIFY(m_Preferences->activateProfile(QStringLiteral("default")));
        QCOMPARE(loadedSpy.count(), 1);

        QSettings settings;
        settings.remove(QStringLiteral("profiles/default"));
        settings.setValue(QStringLiteral("activeProfileId"), QStringLiteral("missing"));
        settings.sync();
        m_Preferences->reload();
        QCOMPARE(m_Preferences->activeProfileId(), QStringLiteral("default"));
        QVERIFY(m_Preferences->profileIds().contains(QStringLiteral("default")));
        QCOMPARE(m_Preferences->width, 1280);
    }

private:
    QTemporaryDir m_SettingsDirectory;
    StreamingPreferences* m_Preferences = nullptr;
};

QTEST_GUILESS_MAIN(StreamingPreferencesTest)

#include "streamingpreferences_test.moc"
