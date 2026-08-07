#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Decision-10 keys and their documented defaults, kept in one place so the
// getters and the first-run template can't drift apart.
constexpr auto kSlackBotToken = "slack/bot-token";
constexpr auto kSlackAppToken = "slack/app-token";
constexpr auto kSlackListenChannel = "slack/listen-channel";
constexpr auto kSlackIgnoreNumbers = "slack/ignore-numbers";
constexpr auto kPropresenterHost = "propresenter/host";
constexpr auto kPropresenterPort = "propresenter/port";
constexpr auto kBatchWaitTime = "propresenter/batch-wait-time";
constexpr auto kBatchMaxCount = "propresenter/batch-max-count";
constexpr auto kExpireTime = "propresenter/expire-time";

QStringList defaultIgnoreNumbers()
{
    return QStringList{QStringLiteral("5555"), QStringLiteral("7777")};
}

// Commented template written on first run. Every key appears as a comment with
// its default so the user can uncomment and fill in. No password (Decision 4)
// and no [network]/OAuth section (Decision 3).
const char *templateContents()
{
    return
        "; ProPager configuration\n"
        ";\n"
        "; Edit the values below and restart ProPager. Lines starting with ';'\n"
        "; are comments — uncomment a line and set its value to override the\n"
        "; built-in default shown.\n"
        "\n"
        "[slack]\n"
        "; bot-token=xoxb-...\n"
        "; app-token=xapp-...\n"
        "; listen-channel=          ; Slack channel ID, e.g. C06Q284BDRT\n"
        "; ignore-numbers=5555, 7777 ; numbers reacted to but not forwarded\n"
        "\n"
        "[propresenter]\n"
        "; host=127.0.0.1\n"
        "; port=55184\n"
        "; batch-wait-time=10        ; seconds to gather numbers into one batch\n"
        "; batch-max-count=3         ; max numbers combined onto one message\n"
        "; expire-time=45            ; seconds a message stays on screen\n";
}

} // namespace

Config::Config() : Config(resolveDefaultIniPath()) {}

Config::Config(const QString &iniFilePath)
    : m_iniPath(iniFilePath),
      m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

Config::~Config() = default;

QString Config::resolveDefaultIniPath()
{
    // AppDataLocation resolves to the app-support/ProPager directory on macOS
    // (Decision 11); the .ini and the log file both live here — never under
    // ~/Documents (Decision 10).
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/ProPager.ini");
}

void Config::load()
{
    QDir().mkpath(configDir());
    writeTemplateIfMissing();
    m_settings->sync();
}

void Config::writeTemplateIfMissing() const
{
    if (QFile::exists(m_iniPath)) {
        return; // Never clobber an existing user config.
    }

    QFile file(m_iniPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&file) << templateContents();
    }
}

QString Config::configPath() const
{
    return m_settings->fileName();
}

QString Config::configDir() const
{
    return QFileInfo(m_iniPath).absolutePath();
}

QString Config::slackBotToken() const
{
    return m_settings->value(kSlackBotToken, QString()).toString();
}

QString Config::slackAppToken() const
{
    return m_settings->value(kSlackAppToken, QString()).toString();
}

QString Config::slackListenChannel() const
{
    return m_settings->value(kSlackListenChannel, QString()).toString();
}

QStringList Config::slackIgnoreNumbers() const
{
    return m_settings->value(kSlackIgnoreNumbers, defaultIgnoreNumbers())
        .toStringList();
}

QString Config::propresenterHost() const
{
    return m_settings->value(kPropresenterHost, QStringLiteral("127.0.0.1"))
        .toString();
}

int Config::propresenterPort() const
{
    return m_settings->value(kPropresenterPort, 55184).toInt();
}

int Config::batchWaitTime() const
{
    return m_settings->value(kBatchWaitTime, 10).toInt();
}

int Config::batchMaxCount() const
{
    return m_settings->value(kBatchMaxCount, 3).toInt();
}

int Config::expireTime() const
{
    return m_settings->value(kExpireTime, 45).toInt();
}
