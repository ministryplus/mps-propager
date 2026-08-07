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

    // Footgun 1: a trailing inline comment on a value line ("port=55184 ; ...")
    // must not leak into the value. QSettings keeps everything after '='; the
    // getters strip a trailing ';'/'#' comment so an uncommented template line
    // (or a hand-annotated config) reads as the bare value.
    void inlineComments_strippedFromValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");
        writeIni(ini,
                 "[slack]\n"
                 "bot-token=xoxb-abc ; the bot token\n"
                 "app-token=xapp-def   # the app token\n"
                 "listen-channel=C06Q284BDRT ; Slack channel ID\n"
                 "ignore-numbers=5555, 7777 ; not forwarded\n"
                 "[propresenter]\n"
                 "host=10.0.0.5 ; the box\n"
                 "port=55184 ; PP port\n"
                 "batch-wait-time=10 ; seconds\n"
                 "batch-max-count=3 # max\n"
                 "expire-time=45 ; seconds\n");

        Config config(ini);
        config.load();

        QCOMPARE(config.slackBotToken(), QString("xoxb-abc"));
        QCOMPARE(config.slackAppToken(), QString("xapp-def"));
        QCOMPARE(config.slackListenChannel(), QString("C06Q284BDRT"));
        QCOMPARE(config.slackIgnoreNumbers(), (QStringList{"5555", "7777"}));
        QCOMPARE(config.propresenterHost(), QString("10.0.0.5"));
        QCOMPARE(config.propresenterPort(), 55184);
        QCOMPARE(config.batchWaitTime(), 10);
        QCOMPARE(config.batchMaxCount(), 3);
        QCOMPARE(config.expireTime(), 45);
    }

    // Footgun 2: the first-run template must not put explanatory text on the
    // same line as a key (any line with '='), so uncommenting a key never drags
    // a trailing comment into the value.
    void template_keyLinesHaveNoInlineComments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        QFile f(ini);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        f.close();

        for (const QString &line : lines) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq < 0)
                continue; // section header or standalone prose comment
            const QString afterValue = line.mid(eq + 1);
            QVERIFY2(!afterValue.contains(QLatin1Char(';')) &&
                         !afterValue.contains(QLatin1Char('#')),
                     qPrintable("inline comment on key line: " + line));
        }
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

    // --- Task 002-1: write + reload + validate ----------------------------

    // Helper: does a ValidationResult carry an entry for `key` in the given
    // tier? Tiers carry the offending key so the tab/banner/tray can render.
    static bool hasEntry(const QList<Config::ValidationEntry> &entries,
                         const QString &key)
    {
        for (const Config::ValidationEntry &e : entries)
            if (e.key == key)
                return true;
        return false;
    }

    // Every setter writes through QSettings + sync(); after reload() the
    // matching getter reads the written value back — no new Config needed.
    void setters_roundTripThroughReload()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        config.setSlackBotToken("xoxb-written");
        config.setSlackAppToken("xapp-written");
        config.setSlackListenChannel("C0WRITTEN");
        config.setSlackIgnoreNumbers(QStringList{"1111", "2222"});
        config.setPropresenterHost("192.168.1.50");
        config.setPropresenterPort(12345);
        config.setBatchWaitTime(20);
        config.setBatchMaxCount(7);
        config.setExpireTime(90);

        config.reload();

        QCOMPARE(config.slackBotToken(), QString("xoxb-written"));
        QCOMPARE(config.slackAppToken(), QString("xapp-written"));
        QCOMPARE(config.slackListenChannel(), QString("C0WRITTEN"));
        QCOMPARE(config.slackIgnoreNumbers(), (QStringList{"1111", "2222"}));
        QCOMPARE(config.propresenterHost(), QString("192.168.1.50"));
        QCOMPARE(config.propresenterPort(), 12345);
        QCOMPARE(config.batchWaitTime(), 20);
        QCOMPARE(config.batchMaxCount(), 7);
        QCOMPARE(config.expireTime(), 90);
    }

    // Decision 2: the first in-app Save collapses the commented template to a
    // bare key=value file (QSettings IniFormat drops comments). Accepted and
    // expected — the .ini is now app-owned.
    void setters_firstSaveCollapsesTemplateToBareKeys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load(); // writes the commented template

        // Sanity: the template starts life commented.
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
            QVERIFY(QString::fromUtf8(f.readAll()).contains(QLatin1Char(';')));
            f.close();
        }

        config.setSlackBotToken("xoxb-collapsed");
        config.reload();

        QFile f(ini);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        f.close();

        // The written key line is bare key=value with no leading ';'.
        bool found = false;
        for (const QString &line : lines) {
            if (line.trimmed().startsWith(QLatin1String("bot-token="))) {
                found = true;
                QVERIFY2(!line.trimmed().startsWith(QLatin1Char(';')),
                         "written key line must not be commented");
                QVERIFY(line.contains(QLatin1String("xoxb-collapsed")));
            }
        }
        QVERIFY2(found, "bot-token= line must be present after Save");
        // Value round-trips through the getter.
        QCOMPARE(config.slackBotToken(), QString("xoxb-collapsed"));
    }

    // Tier 1: the three required Slack keys are reported when unset and cleared
    // once set; hasBlockingErrors() tracks requiredMissing.
    void validate_flagsEachRequiredMissing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        Config::ValidationResult missing = config.validate();
        QVERIFY(hasEntry(missing.requiredMissing, "slack/bot-token"));
        QVERIFY(hasEntry(missing.requiredMissing, "slack/app-token"));
        QVERIFY(hasEntry(missing.requiredMissing, "slack/listen-channel"));
        QVERIFY(missing.hasBlockingErrors());

        config.setSlackBotToken("xoxb-ok");
        config.setSlackAppToken("xapp-ok");
        config.setSlackListenChannel("C0OK");
        config.reload();

        Config::ValidationResult set = config.validate();
        QVERIFY(set.requiredMissing.isEmpty());
        QVERIFY(!set.hasBlockingErrors());
    }

    // Tier 2: present-but-malformed values each raise a warn-only shape entry.
    void validate_shapeWarningsFireForMalformed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        config.setSlackBotToken("nope");    // no xoxb- prefix
        config.setSlackAppToken("nope");    // no xapp- prefix
        config.setSlackListenChannel("X");  // not a C… id
        config.setPropresenterPort(70000);  // out of 1–65535
        config.setExpireTime(-1);           // non-positive timing
        config.reload();

        Config::ValidationResult r = config.validate();
        QVERIFY(hasEntry(r.shapeWarnings, "slack/bot-token"));
        QVERIFY(hasEntry(r.shapeWarnings, "slack/app-token"));
        QVERIFY(hasEntry(r.shapeWarnings, "slack/listen-channel"));
        QVERIFY(hasEntry(r.shapeWarnings, "propresenter/port"));
        QVERIFY(hasEntry(r.shapeWarnings, "propresenter/expire-time"));
    }

    // Valid values raise no warning; absent optional-with-default keys
    // (host/port) must NOT warn — only present-but-invalid does. With the
    // three required keys valid, isClean() is true.
    void validate_silentForValidAndAbsentDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath("ProPager.ini");

        Config config(ini);
        config.load();

        // Valid required keys; host/port left UNSET (defaults apply).
        config.setSlackBotToken("xoxb-valid");
        config.setSlackAppToken("xapp-valid");
        config.setSlackListenChannel("C06Q284BDRT");
        config.reload();

        Config::ValidationResult r = config.validate();
        QVERIFY2(r.shapeWarnings.isEmpty(),
                 "absent host/port and valid tokens must not warn");
        QVERIFY(r.requiredMissing.isEmpty());
        QVERIFY(r.isClean());
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
