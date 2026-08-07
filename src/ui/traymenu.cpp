#include "traymenu.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

#include "pagercontroller.h"

TrayMenu::TrayMenu(PagerController *pager, QObject *parent)
    : QObject(parent), m_pager(pager)
{
    m_menu = new QMenu;
    m_tray = new QSystemTrayIcon(this);

    // Placeholder icon (real menu-bar assets are Task 8). A concrete icon is
    // needed or the tray item may not render on some platforms.
    m_tray->setIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    m_tray->setToolTip(QStringLiteral("ProPager"));

    // Order mirrors widget.py: ProPresenter status, Slack status, separator,
    // active number, separator, Open Window, Quit.
    m_proPresStatus = m_menu->addAction(QStringLiteral("ProPres: Disconnected"));
    m_proPresStatus->setEnabled(false);

    m_slackStatus = m_menu->addAction(QStringLiteral("Slack: Disconnected"));
    m_slackStatus->setEnabled(false);

    m_menu->addSeparator();

    m_activeAction = m_menu->addAction(QStringLiteral("Active Number: N/A"));
    m_activeAction->setEnabled(false);

    m_menu->addSeparator();

    // Single coarse Reconnect (Decision 12) — no per-connection items; that
    // lives on the Connections tab. Sits with the actionable items above Quit.
    m_reconnectAction = m_menu->addAction(QStringLiteral("Reconnect"));
    connect(m_reconnectAction, &QAction::triggered, this,
            &TrayMenu::reconnectRequested);

    QAction *openAction = m_menu->addAction(QStringLiteral("Open Window"));
    connect(openAction, &QAction::triggered, this,
            &TrayMenu::openWindowRequested);

    QAction *quitAction = m_menu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setVisible(true);

    // Both clients start disconnected, so render the initial warning state.
    refreshWarningState();
}

QString TrayMenu::warningTooltip(bool configValid, const QString &configSummary,
                                 bool slackConnected, bool proPresConnected)
{
    // Precedence (a): a required key is unset → cannot connect. Most actionable,
    // so it wins even when a client is also down.
    if (!configValid) {
        return configSummary.isEmpty()
                   ? QStringLiteral("Configuration incomplete")
                   : configSummary;
    }

    // Precedence (b): config is fine, so report which client is down.
    if (!slackConnected && !proPresConnected)
        return QStringLiteral("Slack and ProPresenter disconnected");
    if (!slackConnected)
        return QStringLiteral("Slack disconnected");
    if (!proPresConnected)
        return QStringLiteral("ProPresenter disconnected");

    // Config valid and both clients up → normal state.
    return QString();
}

void TrayMenu::setSlackConnected(bool connected)
{
    m_slackConnected = connected;
    m_slackStatus->setText(connected ? QStringLiteral("Slack: Connected")
                                     : QStringLiteral("Slack: Disconnected"));
    refreshWarningState();
}

void TrayMenu::setProPresConnected(bool connected)
{
    m_proPresConnected = connected;
    m_proPresStatus->setText(connected ? QStringLiteral("ProPres: Connected")
                                       : QStringLiteral("ProPres: Disconnected"));
    refreshWarningState();
}

void TrayMenu::setConfigValidation(const Config::ValidationResult &result)
{
    // Only required-but-unset keys block connecting (Decision 6); shape warnings
    // are surfaced on the tab/banner, not the tray.
    m_configValid = !result.hasBlockingErrors();

    QStringList reasons;
    reasons.reserve(result.requiredMissing.size());
    for (const Config::ValidationEntry &entry : result.requiredMissing)
        reasons << entry.message;
    m_configSummary = reasons.join(QStringLiteral("; "));

    refreshWarningState();
}

void TrayMenu::refreshWarningState()
{
    const QString reason = warningTooltip(m_configValid, m_configSummary,
                                          m_slackConnected, m_proPresConnected);
    if (reason.isEmpty()) {
        m_tray->setIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
        m_tray->setToolTip(QStringLiteral("ProPager"));
    } else {
        // Warning variant + reason on hover, so the cause is available even if
        // the icon delta is subtle (step 6).
        m_tray->setIcon(
            qApp->style()->standardIcon(QStyle::SP_MessageBoxWarning));
        m_tray->setToolTip(QStringLiteral("ProPager — %1").arg(reason));
    }
}

void TrayMenu::refreshActive()
{
    if (!m_pager)
        return;

    const QString active = m_pager->activeFormatted();
    if (active.isEmpty()) {
        m_activeAction->setText(QStringLiteral("Active Number: N/A"));
        return;
    }
    // Same singular/plural rule as the status window (see formatActiveLine).
    const QString plural =
        active.contains(QStringLiteral(" & ")) ? QStringLiteral("s") : QString();
    m_activeAction->setText(
        QStringLiteral("Active Number%1: %2").arg(plural, active));
}
