#include "connectionstab.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Inline validation styling — the two tiers must be visually distinguishable
// ("must fix to connect" vs. "this looks off"). A dynamic `severity` property
// carries the tier so it survives re-styling and is assertable in tests.
constexpr auto kSeverityError = "error";
constexpr auto kSeverityWarning = "warning";

void setMessage(QLabel *label, const QString &text, const char *severity)
{
    label->setText(text);
    label->setProperty("severity", QString::fromLatin1(severity));
    label->setStyleSheet(
        QLatin1String(severity) == QLatin1String(kSeverityError)
            ? QStringLiteral("color: #c0392b; font-weight: bold;") // red, bold
            : QStringLiteral("color: #b8860b;"));                  // amber
}

void clearMessage(QLabel *label)
{
    label->clear();
    label->setProperty("severity", QString());
    label->setStyleSheet(QString());
}

} // namespace

ConnectionsTab::ConnectionsTab(const Config &config, QWidget *parent)
    : QWidget(parent), m_config(&config)
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buildSlackSection());
    layout->addWidget(buildProPresSection());

    // Config-path label + Reveal Config File (Decision 13), mirroring the Reveal
    // Log File button in mainwindow.cpp.
    auto *pathRow = new QHBoxLayout;
    m_configPathLabel = new QLabel(config.configPath(), this);
    m_configPathLabel->setObjectName(QStringLiteral("config-path"));
    m_configPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_configPathLabel->setWordWrap(true);
    auto *revealConfig =
        new QPushButton(QStringLiteral("Reveal Config File"), this);
    revealConfig->setObjectName(QStringLiteral("reveal-config"));
    const QString configPath = config.configPath();
    connect(revealConfig, &QPushButton::clicked, this, [configPath] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(configPath));
    });
    pathRow->addWidget(m_configPathLabel, /*stretch=*/1);
    pathRow->addWidget(revealConfig);
    layout->addLayout(pathRow);

    layout->addStretch();

    captureBaseline();
}

QLineEdit *ConnectionsTab::addField(QFormLayout *form, const QString &caption,
                                    const QString &key, Section section,
                                    bool isConnection, bool secret)
{
    auto *edit = new QLineEdit(this);
    edit->setObjectName(key);

    auto *msg = new QLabel(this);
    msg->setObjectName(key + QStringLiteral("/msg"));
    msg->setWordWrap(true);
    m_msgLabels.insert(key, msg);

    if (secret) {
        // Masked token with an eye toggle that flips echo mode (Decision 11).
        edit->setEchoMode(QLineEdit::Password);
        auto *reveal = new QToolButton(this);
        reveal->setObjectName(key + QStringLiteral("/reveal"));
        reveal->setCheckable(true);
        reveal->setText(QStringLiteral("Reveal"));
        connect(reveal, &QToolButton::toggled, edit, [edit](bool on) {
            edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        });
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(edit, /*stretch=*/1);
        rowLayout->addWidget(reveal);
        form->addRow(caption, row);
    } else {
        form->addRow(caption, edit);
    }
    form->addRow(QString(), msg);

    m_fields.append({edit, section, isConnection});
    return edit;
}

QWidget *ConnectionsTab::buildSlackSection()
{
    auto *box = new QGroupBox(QStringLiteral("Slack"), this);
    auto *outer = new QVBoxLayout(box);
    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_slackBotToken =
        addField(form, QStringLiteral("Bot token"),
                 QStringLiteral("slack/bot-token"), Section::Slack,
                 /*isConnection=*/true, /*secret=*/true);
    m_slackBotToken->setText(m_config->slackBotToken());

    m_slackAppToken =
        addField(form, QStringLiteral("App-level token"),
                 QStringLiteral("slack/app-token"), Section::Slack,
                 /*isConnection=*/true, /*secret=*/true);
    m_slackAppToken->setText(m_config->slackAppToken());

    m_slackListenChannel =
        addField(form, QStringLiteral("Listen channel"),
                 QStringLiteral("slack/listen-channel"), Section::Slack,
                 /*isConnection=*/true, /*secret=*/false);
    m_slackListenChannel->setText(m_config->slackListenChannel());

    m_slackIgnoreNumbers =
        addField(form, QStringLiteral("Ignore numbers"),
                 QStringLiteral("slack/ignore-numbers"), Section::Slack,
                 /*isConnection=*/false, /*secret=*/false);
    m_slackIgnoreNumbers->setText(
        m_config->slackIgnoreNumbers().join(QStringLiteral(", ")));

    auto *buttons = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("Save"), box);
    save->setObjectName(QStringLiteral("slack/save"));
    connect(save, &QPushButton::clicked, this,
            [this] { emit saveRequested(Section::Slack); });
    auto *reconnect = new QPushButton(QStringLiteral("Reconnect"), box);
    reconnect->setObjectName(QStringLiteral("slack/reconnect"));
    connect(reconnect, &QPushButton::clicked, this,
            [this] { emit reconnectRequested(Section::Slack); });
    buttons->addStretch();
    buttons->addWidget(save);
    buttons->addWidget(reconnect);
    outer->addLayout(buttons);

    return box;
}

QWidget *ConnectionsTab::buildProPresSection()
{
    auto *box = new QGroupBox(QStringLiteral("ProPresenter"), this);
    auto *outer = new QVBoxLayout(box);
    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_proPresHost =
        addField(form, QStringLiteral("Host"),
                 QStringLiteral("propresenter/host"), Section::ProPresenter,
                 /*isConnection=*/true, /*secret=*/false);
    m_proPresHost->setText(m_config->propresenterHost());

    m_proPresPort =
        addField(form, QStringLiteral("Port"),
                 QStringLiteral("propresenter/port"), Section::ProPresenter,
                 /*isConnection=*/true, /*secret=*/false);
    m_proPresPort->setText(QString::number(m_config->propresenterPort()));

    m_batchWaitTime =
        addField(form, QStringLiteral("Batch wait time (s)"),
                 QStringLiteral("propresenter/batch-wait-time"),
                 Section::ProPresenter, /*isConnection=*/false,
                 /*secret=*/false);
    m_batchWaitTime->setText(QString::number(m_config->batchWaitTime()));

    m_batchMaxCount =
        addField(form, QStringLiteral("Batch max count"),
                 QStringLiteral("propresenter/batch-max-count"),
                 Section::ProPresenter, /*isConnection=*/false,
                 /*secret=*/false);
    m_batchMaxCount->setText(QString::number(m_config->batchMaxCount()));

    m_expireTime =
        addField(form, QStringLiteral("Expire time (s)"),
                 QStringLiteral("propresenter/expire-time"),
                 Section::ProPresenter, /*isConnection=*/false,
                 /*secret=*/false);
    m_expireTime->setText(QString::number(m_config->expireTime()));

    auto *buttons = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("Save"), box);
    save->setObjectName(QStringLiteral("propresenter/save"));
    connect(save, &QPushButton::clicked, this,
            [this] { emit saveRequested(Section::ProPresenter); });
    auto *test = new QPushButton(QStringLiteral("Test"), box);
    test->setObjectName(QStringLiteral("propresenter/test"));
    connect(test, &QPushButton::clicked, this,
            [this] { emit testRequested(); });
    buttons->addStretch();
    buttons->addWidget(save);
    buttons->addWidget(test);
    outer->addLayout(buttons);

    return box;
}

void ConnectionsTab::captureBaseline()
{
    m_baseline.clear();
    for (const Field &field : m_fields)
        m_baseline.insert(field.edit, field.edit->text());
}

bool ConnectionsTab::sectionConnectionDirty(Section section) const
{
    for (const Field &field : m_fields) {
        if (field.section != section || !field.isConnection)
            continue;
        if (field.edit->text() != m_baseline.value(field.edit))
            return true;
    }
    return false;
}

bool ConnectionsTab::slackConnectionDirty() const
{
    return sectionConnectionDirty(Section::Slack);
}

bool ConnectionsTab::proPresConnectionDirty() const
{
    return sectionConnectionDirty(Section::ProPresenter);
}

void ConnectionsTab::reloadFrom(const Config &config)
{
    m_config = &config;

    m_slackBotToken->setText(config.slackBotToken());
    m_slackAppToken->setText(config.slackAppToken());
    m_slackListenChannel->setText(config.slackListenChannel());
    m_slackIgnoreNumbers->setText(
        config.slackIgnoreNumbers().join(QStringLiteral(", ")));
    m_proPresHost->setText(config.propresenterHost());
    m_proPresPort->setText(QString::number(config.propresenterPort()));
    m_batchWaitTime->setText(QString::number(config.batchWaitTime()));
    m_batchMaxCount->setText(QString::number(config.batchMaxCount()));
    m_expireTime->setText(QString::number(config.expireTime()));

    if (m_configPathLabel)
        m_configPathLabel->setText(config.configPath());

    captureBaseline();
}

void ConnectionsTab::commitBaseline(Section section)
{
    for (const Field &field : m_fields) {
        if (field.section == section)
            m_baseline.insert(field.edit, field.edit->text());
    }
}

void ConnectionsTab::showValidation(const Config::ValidationResult &result)
{
    // Clear stale markers first so a re-validation removes fixed findings.
    for (QLabel *label : m_msgLabels)
        clearMessage(label);

    for (const Config::ValidationEntry &entry : result.requiredMissing) {
        if (QLabel *label = m_msgLabels.value(entry.key))
            setMessage(label, entry.message, kSeverityError);
    }
    for (const Config::ValidationEntry &entry : result.shapeWarnings) {
        if (QLabel *label = m_msgLabels.value(entry.key))
            setMessage(label, entry.message, kSeverityWarning);
    }
}

QString ConnectionsTab::slackBotToken() const { return m_slackBotToken->text(); }
QString ConnectionsTab::slackAppToken() const { return m_slackAppToken->text(); }
QString ConnectionsTab::slackListenChannel() const
{
    return m_slackListenChannel->text();
}
QString ConnectionsTab::slackIgnoreNumbers() const
{
    return m_slackIgnoreNumbers->text();
}
QString ConnectionsTab::proPresHost() const { return m_proPresHost->text(); }
int ConnectionsTab::proPresPort() const { return m_proPresPort->text().toInt(); }
int ConnectionsTab::batchWaitTime() const
{
    return m_batchWaitTime->text().toInt();
}
int ConnectionsTab::batchMaxCount() const
{
    return m_batchMaxCount->text().toInt();
}
int ConnectionsTab::expireTime() const { return m_expireTime->text().toInt(); }
