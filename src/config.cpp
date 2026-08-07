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

// Strip a trailing inline comment (" ; ..." / " # ...") and surrounding
// whitespace. QSettings keeps everything after '=', so an uncommented template
// line or a hand-annotated config would otherwise leak the comment into the
// value (e.g. port "55184 ; PP port" -> toInt() == 0). No Decision-10 value
// legitimately contains ';' or '#' (tokens, channel IDs, hosts, numbers), so
// cutting at the first such char is safe.
QString stripInlineComment(const QString &raw)
{
    const int semi = raw.indexOf(QLatin1Char(';'));
    const int hash = raw.indexOf(QLatin1Char('#'));
    int cut = semi;
    if (hash >= 0 && (cut < 0 || hash < cut))
        cut = hash;
    return (cut < 0 ? raw : raw.left(cut)).trimmed();
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
        "; Explanations go on their own comment line, never after a key — so\n"
        "; uncommenting a key can't drag a trailing comment into its value.\n"
        "\n"
        "[slack]\n"
        "; Bot token (OAuth & Permissions), starts xoxb-\n"
        "; bot-token=xoxb-...\n"
        "; App-level token (Basic Information -> App-Level Tokens, scope\n"
        "; connections:write, Socket Mode enabled), starts xapp-\n"
        "; app-token=xapp-...\n"
        "; Slack channel ID, e.g. C06Q284BDRT\n"
        "; listen-channel=\n"
        "; Numbers reacted to but not forwarded\n"
        "; ignore-numbers=5555, 7777\n"
        "\n"
        "[propresenter]\n"
        "; host=127.0.0.1\n"
        "; port=55184\n"
        "; Seconds to gather numbers into one batch\n"
        "; batch-wait-time=10\n"
        "; Max numbers combined onto one message\n"
        "; batch-max-count=3\n"
        "; Seconds a message stays on screen\n"
        "; expire-time=45\n";
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
    return stripInlineComment(m_settings->value(kSlackBotToken).toString());
}

QString Config::slackAppToken() const
{
    return stripInlineComment(m_settings->value(kSlackAppToken).toString());
}

QString Config::slackListenChannel() const
{
    return stripInlineComment(m_settings->value(kSlackListenChannel).toString());
}

QStringList Config::slackIgnoreNumbers() const
{
    const QVariant raw = m_settings->value(kSlackIgnoreNumbers);
    if (!raw.isValid())
        return defaultIgnoreNumbers();

    QStringList cleaned;
    for (const QString &n : raw.toStringList()) {
        const QString value = stripInlineComment(n);
        if (!value.isEmpty())
            cleaned << value;
    }
    return cleaned.isEmpty() ? defaultIgnoreNumbers() : cleaned;
}

QString Config::propresenterHost() const
{
    const QString host =
        stripInlineComment(m_settings->value(kPropresenterHost).toString());
    return host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
}

int Config::propresenterPort() const
{
    return intValueOr(kPropresenterPort, 55184);
}

int Config::batchWaitTime() const
{
    return intValueOr(kBatchWaitTime, 10);
}

int Config::batchMaxCount() const
{
    return intValueOr(kBatchMaxCount, 3);
}

int Config::expireTime() const
{
    return intValueOr(kExpireTime, 45);
}

int Config::intValueOr(const char *key, int fallback) const
{
    const QVariant raw = m_settings->value(key);
    if (!raw.isValid())
        return fallback;
    bool ok = false;
    const int n = stripInlineComment(raw.toString()).toInt(&ok);
    return ok ? n : fallback;
}
