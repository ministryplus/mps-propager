#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include "config.h"

// Unit tests for the Config layer (Task 001-2). Every test injects an explicit
// .ini path inside a QTemporaryDir so the user's real config is never touched.
class TestConfig : public QObject
{
    Q_OBJECT

private:
    // Write raw INI text to a path (used to simulate a user-edited config).
    static void writeIni(const QString &path, const QString &contents)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << contents;
        f.close();
    }

private slots:
    // Unset keys fall back to the documented Decision-10 defaults.
    void defaults_whenKeysUnset()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        QCOMPARE(config.slackBotToken(), QString(""));
        QCOMPARE(config.slackAppToken(), QString(""));
        QCOMPARE(config.slackListenChannel(), QString(""));
        QCOMPARE(config.slackIgnoreNumbers(), (QStringList{"5555", "7777"}));

        QCOMPARE(config.propresenterHost(), QString("127.0.0.1"));
        QCOMPARE(config.propresenterPort(), 55184);
        QCOMPARE(config.batchWaitTime(), 10);
        QCOMPARE(config.batchMaxCount(), 3);
        QCOMPARE(config.expireTime(), 45);
    }

    // On first run the template .ini is created with a commented line for
    // every key, and none of the removed legacy keys appear.
    void firstRun_createsCommentedTemplate()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");
        QVERIFY(!QFile::exists(ini));

        Config config(ini);
        config.load();

        QVERIFY2(QFile::exists(ini), "load() must create the .ini on first run");

        QFile f(ini);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(f.readAll());
        f.close();

        // A commented mention of every Decision-10 key.
        for (const QString &key : {QStringLiteral("bot-token"),
                                   QStringLiteral("app-token"),
                                   QStringLiteral("listen-channel"),
                                   QStringLiteral("ignore-numbers"),
                                   QStringLiteral("host"),
                                   QStringLiteral("port"),
                                   QStringLiteral("batch-wait-time"),
                                   QStringLiteral("batch-max-count"),
                                   QStringLiteral("expire-time")}) {
            QVERIFY2(text.contains(key), qPrintable("template missing key: " + key));
        }

        // Removed legacy config must not reappear (Decisions 3 & 4).
        QVERIFY2(!text.contains("password", Qt::CaseInsensitive),
                 "template must not contain a password key (Decision 4)");
        QVERIFY2(!text.contains("[network]", Qt::CaseInsensitive),
                 "template must not contain a [network] section (Decision 3)");
        QVERIFY2(!text.contains("simpleauth", Qt::CaseInsensitive),
                 "template must not contain OAuth/simpleauth keys (Decision 3)");
    }

    // An edited value survives a restart (new Config on the same path reads it
    // back) — proves load() does not clobber an existing file.
    void roundTrip_editedValuesPersist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        // Simulate a user filling in the config.
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue("slack/bot-token", "xoxb-test-token");
            s.setValue("slack/listen-channel", "C06Q284BDRT");
            s.setValue("slack/ignore-numbers", QStringList{"1234", "9999"});
            s.setValue("propresenter/host", "10.0.0.5");
            s.setValue("propresenter/port", 9999);
            s.setValue("propresenter/expire-time", 60);
            s.sync();
        }

        Config config(ini);
        config.load();

        QCOMPARE(config.slackBotToken(), QString("xoxb-test-token"));
        QCOMPARE(config.slackListenChannel(), QString("C06Q284BDRT"));
        QCOMPARE(config.slackIgnoreNumbers(), (QStringList{"1234", "9999"}));
        QCOMPARE(config.propresenterHost(), QString("10.0.0.5"));
        QCOMPARE(config.propresenterPort(), 9999);
        QCOMPARE(config.expireTime(), 60);
        // A key left unset still falls back to its default.
        QCOMPARE(config.batchMaxCount(), 3);
    }

    // load() must not overwrite an existing user config with the template.
    void load_doesNotClobberExistingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");
        writeIni(ini, "[slack]\nbot-token=keepme\n");

        Config config(ini);
        config.load();

        QCOMPARE(config.slackBotToken(), QString("keepme"));
    }

    // configPath() reports the resolved .ini path; configDir() is its parent.
    void paths_reportIniAndParentDir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        QCOMPARE(QFileInfo(config.configPath()).canonicalFilePath(),
                 QFileInfo(ini).canonicalFilePath());
        QCOMPARE(QFileInfo(config.configDir()).canonicalFilePath(),
                 QFileInfo(QFileInfo(ini).absolutePath()).canonicalFilePath());
    }

    // load() creates the config directory if it does not yet exist.
    void load_createsMissingConfigDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subdir = dir.filePath("ProPager");
        const QString ini = subdir + "/ProPager.ini";
        QVERIFY(!QDir(subdir).exists());

        Config config(ini);
        config.load();

        QVERIFY2(QDir(subdir).exists(), "load() must create the config directory");
        QVERIFY(QFile::exists(ini));
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
