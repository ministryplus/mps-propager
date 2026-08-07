#ifndef PROPAGER_UI_CONNECTIONSTAB_H
#define PROPAGER_UI_CONNECTIONSTAB_H

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "config.h"

class QAbstractButton;
class QLabel;
class QLineEdit;

// ConnectionsTab is Spec 002's visible core (Task 002-4): a form over Config,
// split into a Slack section and a ProPresenter section. It is a *pure form* —
// it never writes Config, reloads the controller, or reconnects a client. It
// prefills from Config, tracks which connection fields changed since load/save
// (Decision 5 dirty-tracking), renders a Config::ValidationResult inline per
// field (Decision 6), masks tokens with a reveal toggle (Decision 11), and
// surfaces the resolved config path (Decision 13). All side effects live in
// main.cpp (Task 002-7), reached via the signals below.
class ConnectionsTab : public QWidget
{
    Q_OBJECT

public:
    enum class Section { Slack, ProPresenter };
    Q_ENUM(Section)

    explicit ConnectionsTab(const Config &config, QWidget *parent = nullptr);

    // Current (edited) field values for the integrator to push through Config.
    // Numeric getters parse the field text; validation/coercion policy lives in
    // Config::validate(), not here.
    QString slackBotToken() const;
    QString slackAppToken() const;
    QString slackListenChannel() const;
    QString slackIgnoreNumbers() const;
    QString proPresHost() const;
    int proPresPort() const;
    int batchWaitTime() const;
    int batchMaxCount() const;
    int expireTime() const;

    // True only when at least one *connection* field (Decision 4) in the section
    // differs from its baseline. Behavior fields never contribute — they must
    // not trip the reconnect gate (Decision 5).
    bool slackConnectionDirty() const;
    bool proPresConnectionDirty() const;

public slots:
    // Re-prefill every field from Config and reset the dirty baseline (Task
    // 002-7 calls this after a Save round-trip).
    void reloadFrom(const Config &config);
    // Reset the dirty baseline for one section to its current text (Task 002-7
    // calls this after a successful Save so the section is no longer dirty).
    void commitBaseline(Section section);
    // Render validation results inline per field: required-but-unset as an
    // error, present-but-malformed as a warning (Decision 6). A later call
    // clears stale markers first.
    void showValidation(const Config::ValidationResult &result);

signals:
    void saveRequested(ConnectionsTab::Section section);
    void reconnectRequested(ConnectionsTab::Section section);
    void testRequested(); // ProPresenter only

private:
    // Metadata for each editable field, so dirty-tracking and baseline resets
    // iterate one list instead of hand-maintaining per-field branches.
    struct Field
    {
        QLineEdit *edit;
        Section section;
        bool isConnection;
    };

    QWidget *buildSlackSection();
    QWidget *buildProPresSection();
    // Add a labelled row (caption, editor, inline message label) to `form`, wire
    // it into m_fields + m_msgLabels, and return the editor.
    QLineEdit *addField(class QFormLayout *form, const QString &caption,
                        const QString &key, Section section, bool isConnection,
                        bool secret);
    void captureBaseline();
    bool sectionConnectionDirty(Section section) const;

    const Config *m_config; // prefill + configPath(); never written here.

    QList<Field> m_fields;
    QHash<QLineEdit *, QString> m_baseline;
    QHash<QString, QLabel *> m_msgLabels; // keyed by Decision-4 config key

    // Named handles for the typed accessors.
    QLineEdit *m_slackBotToken = nullptr;
    QLineEdit *m_slackAppToken = nullptr;
    QLineEdit *m_slackListenChannel = nullptr;
    QLineEdit *m_slackIgnoreNumbers = nullptr;
    QLineEdit *m_proPresHost = nullptr;
    QLineEdit *m_proPresPort = nullptr;
    QLineEdit *m_batchWaitTime = nullptr;
    QLineEdit *m_batchMaxCount = nullptr;
    QLineEdit *m_expireTime = nullptr;

    QLabel *m_configPathLabel = nullptr;
};

#endif // PROPAGER_UI_CONNECTIONSTAB_H
