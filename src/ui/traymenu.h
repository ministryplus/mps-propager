#ifndef PROPAGER_UI_TRAYMENU_H
#define PROPAGER_UI_TRAYMENU_H

#include <QIcon>
#include <QObject>
#include <QString>

#include "config.h"

class PagerController;
class QAction;
class QMenu;
class QSystemTrayIcon;

// TrayMenu is ProPager's menu-bar presence (Task 001-6), the native Qt6 port of
// widget.py. It owns a QSystemTrayIcon + QMenu mirroring the status window:
// disabled ProPresenter/Slack status lines, a disabled active-number line, an
// Open Window action, and Quit. It supersedes the placeholder tray created in
// Task 1 (that wiring moved out of main.cpp into here).
//
// Like MainWindow it is updated purely by signal/slot connections (Decision 7):
// no polling. Open Window is surfaced as a signal so main.cpp owns the window
// reference; Quit calls qApp->quit() directly.
class TrayMenu : public QObject
{
    Q_OBJECT

public:
    explicit TrayMenu(PagerController *pager, QObject *parent = nullptr);

    // Pure decision for the tray's warning indicator (Decision 8). Returns the
    // reason string to show as the tooltip when the tray should warn, or an
    // empty QString when it should show the normal state. Precedence: missing
    // required config first (most actionable — "fill in X to connect"), else
    // which client is down. Present-but-malformed shape warnings keep
    // `configValid` true, so they never trip the tray (surfaced on the
    // tab/banner instead). Static + widget-free so it is unit-tested guilessly.
    static QString warningTooltip(bool configValid, const QString &configSummary,
                                  bool slackConnected, bool proPresConnected);

public slots:
    void setSlackConnected(bool connected);
    void setProPresConnected(bool connected);
    // Receive the config-validation state (Task 002-1 type). Stores validity
    // plus a human-readable summary for the tooltip, then refreshes the warning.
    void setConfigValidation(const Config::ValidationResult &result);
    // Re-render the active-number line from the controller.
    void refreshActive();

signals:
    // Emitted by the Open Window action; main.cpp shows/raises the window.
    void openWindowRequested();
    // Emitted by the single coarse Reconnect action (Decision 12); main.cpp
    // (Task 002-7) connects this to both clients' reconnect entry points.
    void reconnectRequested();

private:
    // Reconcile config validity + both client-connected booleans into the
    // tray's icon + tooltip. The single place the warning state is applied.
    void refreshWarningState();

    // Menu-bar glyph (Task 8) and its derived amber warning variant, built once
    // in the constructor and swapped by refreshWarningState().
    QIcon m_normalIcon;
    QIcon m_warningIcon;

    PagerController *m_pager;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_proPresStatus;
    QAction *m_slackStatus;
    QAction *m_activeAction;
    QAction *m_reconnectAction;

    // Inputs to the warning state (step 2). Default to a clean, connected state
    // so the tray starts normal until told otherwise.
    bool m_configValid = true;
    QString m_configSummary;
    bool m_slackConnected = false;
    bool m_proPresConnected = false;
};

#endif // PROPAGER_UI_TRAYMENU_H
