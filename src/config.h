#ifndef PROPAGER_CONFIG_H
#define PROPAGER_CONFIG_H

#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

// Config wraps a QSettings (IniFormat) resolved under the app-support/ProPager
// directory and exposes typed getters for every key in Spec 001 Decision 10.
//
// There is deliberately no propresenter/password key (Decision 4 removed the
// legacy WebSocket + password) and no [network]/OAuth keys (Decision 3).
class Config
{
public:
    // Resolve the .ini under the app-support/ProPager directory via
    // QStandardPaths (AppDataLocation). Requires QCoreApplication org/app
    // identity to be set first.
    Config();
    // Testable/injectable: use an explicit .ini file path (bypasses
    // QStandardPaths so tests never touch the user's real config).
    explicit Config(const QString &iniFilePath);
    ~Config();

    // Ensure the config directory exists, write a commented template .ini on
    // first run (never overwriting an existing file), then read settings.
    void load();

    // Resolved on-disk .ini path (QSettings::fileName()).
    QString configPath() const;
    // Directory that holds the .ini and the application log file.
    QString configDir() const;

    // Slack (Decision 10)
    QString slackBotToken() const;
    QString slackAppToken() const;
    QString slackListenChannel() const;
    QStringList slackIgnoreNumbers() const;

    // ProPresenter (Decision 10)
    QString propresenterHost() const;
    int propresenterPort() const;
    int batchWaitTime() const;
    int batchMaxCount() const;
    int expireTime() const;

private:
    static QString resolveDefaultIniPath();
    void writeTemplateIfMissing() const;
    // Read an int key, stripping any trailing inline comment; return `fallback`
    // when the key is unset or the value isn't a valid int.
    int intValueOr(const char *key, int fallback) const;

    QString m_iniPath;
    std::unique_ptr<QSettings> m_settings;
};

#endif // PROPAGER_CONFIG_H
