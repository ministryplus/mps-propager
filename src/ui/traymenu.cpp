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

    QAction *openAction = m_menu->addAction(QStringLiteral("Open Window"));
    connect(openAction, &QAction::triggered, this,
            &TrayMenu::openWindowRequested);

    QAction *quitAction = m_menu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setVisible(true);
}

void TrayMenu::setSlackConnected(bool connected)
{
    m_slackStatus->setText(connected ? QStringLiteral("Slack: Connected")
                                     : QStringLiteral("Slack: Disconnected"));
}

void TrayMenu::setProPresConnected(bool connected)
{
    m_proPresStatus->setText(connected ? QStringLiteral("ProPres: Connected")
                                       : QStringLiteral("ProPres: Disconnected"));
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
