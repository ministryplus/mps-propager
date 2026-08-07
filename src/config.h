#ifndef PROPAGER_CONFIG_H
#define PROPAGER_CONFIG_H

#include <QList>
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
    // One validation finding: the offending key plus a human-readable display
    // message. Carrying both means the tab, banner, and tray (Spec 002
    // Decision 8) render without re-deriving anything.
    struct ValidationEntry
    {
        QString key;
        QString message;
    };

    // Two-tier validation result (Spec 002 Decision 6). `requiredMissing`
    // blocks connecting ("you must fix this"); `shapeWarnings` are warn-only
    // ("this looks off" but does not block). validate() runs both on load and
    // on every Save and produces the single structure every surface consumes.
    struct ValidationResult
    {
        QList<ValidationEntry> requiredMissing;
        QList<ValidationEntry> shapeWarnings;

        bool hasBlockingErrors() const { return !requiredMissing.isEmpty(); }
        bool isClean() const
        {
            return requiredMissing.isEmpty() && shapeWarnings.isEmpty();
        }
    };

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

    // --- Write API (Spec 002 Decision 2, Task 002-1) -----------------------
    // Per-key setters mirroring the getters. Each writes through
    // QSettings::setValue + sync() so the value hits disk immediately; the
    // .ini becomes app-owned (the first Save collapses the commented template
    // to bare key=value — QSettings IniFormat does not preserve comments).
    void setSlackBotToken(const QString &value);
    void setSlackAppToken(const QString &value);
    void setSlackListenChannel(const QString &value);
    void setSlackIgnoreNumbers(const QStringList &value);
    void setPropresenterHost(const QString &value);
    void setPropresenterPort(int value);
    void setBatchWaitTime(int value);
    void setBatchMaxCount(int value);
    void setExpireTime(int value);

    // Re-sync from disk so a written value is observed by the getters without
    // constructing a new Config (QSettings::sync() both flushes pending writes
    // and re-reads the backing file).
    void reload();

    // Classify the current settings into the two-tier ValidationResult
    // (Decision 6). Reuses the same stripInlineComment semantics as the
    // getters, so a hand-annotated legacy .ini validates like a bare one.
    ValidationResult validate() const;

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
