#include <QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include "config.h"
#include "connectionstab.h"

// Widget tests for ConnectionsTab (Task 002-4). Unlike the guiless format-helper
// tests in tst_ui, these instantiate the real form and drive its QLineEdits and
// buttons, so this executable runs under the `offscreen` QPA platform (set in
// CMakeLists) — no display required. Fields are addressed by objectName (their
// Decision-4 config key) so the tests exercise the same widgets the operator
// sees, not a parallel abstraction.
class TestConnectionsTab : public QObject
{
    Q_OBJECT

private:
    // A Config backed by a throwaway .ini, prefilled with recognizable values so
    // prefill/accessor assertions are unambiguous.
    static std::unique_ptr<Config> makeConfig(QTemporaryDir &dir)
    {
        auto config = std::make_unique<Config>(dir.path() +
                                                QStringLiteral("/ProPager.ini"));
        config->setSlackBotToken(QStringLiteral("xoxb-abc"));
        config->setSlackAppToken(QStringLiteral("xapp-def"));
        config->setSlackListenChannel(QStringLiteral("C123"));
        config->setSlackIgnoreNumbers({QStringLiteral("11"), QStringLiteral("22")});
        config->setPropresenterHost(QStringLiteral("10.0.0.5"));
        config->setPropresenterPort(9000);
        config->setBatchWaitTime(7);
        config->setBatchMaxCount(4);
        config->setExpireTime(30);
        return config;
    }

private slots:
    // Every field prefills from the injected Config.
    void prefillsFromConfig()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        QCOMPARE(tab.slackBotToken(), QString("xoxb-abc"));
        QCOMPARE(tab.slackAppToken(), QString("xapp-def"));
        QCOMPARE(tab.slackListenChannel(), QString("C123"));
        QCOMPARE(tab.proPresHost(), QString("10.0.0.5"));
        QCOMPARE(tab.proPresPort(), 9000);
        QCOMPARE(tab.batchWaitTime(), 7);
        QCOMPARE(tab.batchMaxCount(), 4);
        QCOMPARE(tab.expireTime(), 30);
    }

    // Tokens render masked; the eye toggle flips echo mode and back.
    void tokensMaskedWithRevealToggle()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        auto *bot = tab.findChild<QLineEdit *>(QStringLiteral("slack/bot-token"));
        QVERIFY(bot);
        QCOMPARE(bot->echoMode(), QLineEdit::Password);

        auto *reveal =
            tab.findChild<QAbstractButton *>(QStringLiteral("slack/bot-token/reveal"));
        QVERIFY(reveal);
        reveal->click();
        QCOMPARE(bot->echoMode(), QLineEdit::Normal);
        reveal->click();
        QCOMPARE(bot->echoMode(), QLineEdit::Password);
    }

    // A connection-field edit sets the section connection-dirty; a behavior-field
    // edit does not.
    void connectionDirtyTracksConnectionFieldsOnly()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        QVERIFY(!tab.proPresConnectionDirty());
        tab.findChild<QLineEdit *>(QStringLiteral("propresenter/host"))
            ->setText(QStringLiteral("192.168.1.2"));
        QVERIFY(tab.proPresConnectionDirty());

        // A behavior field never contributes to the connection-dirty gate.
        ConnectionsTab tab2(*config);
        tab2.findChild<QLineEdit *>(QStringLiteral("propresenter/expire-time"))
            ->setText(QStringLiteral("99"));
        QVERIFY(!tab2.proPresConnectionDirty());

        // Slack side.
        QVERIFY(!tab.slackConnectionDirty());
        tab.findChild<QLineEdit *>(QStringLiteral("slack/listen-channel"))
            ->setText(QStringLiteral("C999"));
        QVERIFY(tab.slackConnectionDirty());

        // A Slack behavior field (ignore-numbers) does not dirty the connection.
        ConnectionsTab tab3(*config);
        tab3.findChild<QLineEdit *>(QStringLiteral("slack/ignore-numbers"))
            ->setText(QStringLiteral("99, 88"));
        QVERIFY(!tab3.slackConnectionDirty());
    }

    // commitBaseline resets the section so it is no longer connection-dirty.
    void commitBaselineClearsDirty()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        tab.findChild<QLineEdit *>(QStringLiteral("propresenter/host"))
            ->setText(QStringLiteral("192.168.1.2"));
        QVERIFY(tab.proPresConnectionDirty());
        tab.commitBaseline(ConnectionsTab::Section::ProPresenter);
        QVERIFY(!tab.proPresConnectionDirty());
    }

    // reloadFrom refreshes fields from Config and resets the dirty baseline.
    void reloadFromRefreshesAndResetsBaseline()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        tab.findChild<QLineEdit *>(QStringLiteral("slack/bot-token"))
            ->setText(QStringLiteral("changed"));
        QVERIFY(tab.slackConnectionDirty());

        config->setSlackBotToken(QStringLiteral("xoxb-new"));
        tab.reloadFrom(*config);
        QCOMPARE(tab.slackBotToken(), QString("xoxb-new"));
        QVERIFY(!tab.slackConnectionDirty());
    }

    // Buttons emit the integrator signals; the tab performs no side effects.
    void buttonsEmitSignals()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        int saveCount = 0;
        ConnectionsTab::Section saveSection = ConnectionsTab::Section::ProPresenter;
        connect(&tab, &ConnectionsTab::saveRequested,
                [&](ConnectionsTab::Section s) {
                    saveSection = s;
                    ++saveCount;
                });
        tab.findChild<QAbstractButton *>(QStringLiteral("slack/save"))->click();
        QCOMPARE(saveCount, 1);
        QVERIFY(saveSection == ConnectionsTab::Section::Slack);

        int reconnectCount = 0;
        ConnectionsTab::Section reconnectSection = ConnectionsTab::Section::ProPresenter;
        connect(&tab, &ConnectionsTab::reconnectRequested,
                [&](ConnectionsTab::Section s) {
                    reconnectSection = s;
                    ++reconnectCount;
                });
        tab.findChild<QAbstractButton *>(QStringLiteral("slack/reconnect"))->click();
        QCOMPARE(reconnectCount, 1);
        QVERIFY(reconnectSection == ConnectionsTab::Section::Slack);

        int testCount = 0;
        connect(&tab, &ConnectionsTab::testRequested, [&]() { ++testCount; });
        tab.findChild<QAbstractButton *>(QStringLiteral("propresenter/test"))
            ->click();
        QCOMPARE(testCount, 1);

        int ppSaveCount = 0;
        ConnectionsTab::Section ppSaveSection = ConnectionsTab::Section::Slack;
        connect(&tab, &ConnectionsTab::saveRequested,
                [&](ConnectionsTab::Section s) {
                    ppSaveSection = s;
                    ++ppSaveCount;
                });
        tab.findChild<QAbstractButton *>(QStringLiteral("propresenter/save"))
            ->click();
        QCOMPARE(ppSaveCount, 1);
        QVERIFY(ppSaveSection == ConnectionsTab::Section::ProPresenter);
    }

    // showValidation renders required-missing as error and malformed as warning,
    // inline per field, visually distinct; a later call clears stale markers.
    void showValidationRendersTwoTiersAndClears()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        Config::ValidationResult result;
        result.requiredMissing.append(
            {QStringLiteral("slack/bot-token"),
             QStringLiteral("Slack bot token is required")});
        result.shapeWarnings.append(
            {QStringLiteral("propresenter/port"),
             QStringLiteral("Port must be between 1 and 65535")});
        tab.showValidation(result);

        auto *botMsg =
            tab.findChild<QLabel *>(QStringLiteral("slack/bot-token/msg"));
        QVERIFY(botMsg);
        QCOMPARE(botMsg->text(), QString("Slack bot token is required"));
        QCOMPARE(botMsg->property("severity").toString(), QString("error"));

        auto *portMsg =
            tab.findChild<QLabel *>(QStringLiteral("propresenter/port/msg"));
        QVERIFY(portMsg);
        QCOMPARE(portMsg->text(), QString("Port must be between 1 and 65535"));
        QCOMPARE(portMsg->property("severity").toString(), QString("warning"));

        // A follow-up empty result clears stale markers.
        tab.showValidation({});
        QVERIFY(botMsg->text().isEmpty());
        QVERIFY(portMsg->text().isEmpty());
        QVERIFY(botMsg->property("severity").toString().isEmpty());
    }

    // The config-path label reflects Config::configPath().
    void showsConfigPath()
    {
        QTemporaryDir dir;
        auto config = makeConfig(dir);
        ConnectionsTab tab(*config);

        auto *pathLabel = tab.findChild<QLabel *>(QStringLiteral("config-path"));
        QVERIFY(pathLabel);
        QVERIFY(pathLabel->text().contains(config->configPath()));
    }
};

QTEST_MAIN(TestConnectionsTab)
#include "tst_connectionstab.moc"
