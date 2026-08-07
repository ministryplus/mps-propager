#ifndef PROPAGER_UI_MAINWINDOW_H
#define PROPAGER_UI_MAINWINDOW_H

#include <QList>
#include <QMainWindow>
#include <QString>

#include "pagercontroller.h" // for PagerController::Batch in the static API

class QLabel;
class QCloseEvent;

// MainWindow is ProPager's status window (Task 001-6), the native Qt6 port of
// overview.py + mainwindow.py. It shows the active number, the queued-numbers
// list, and a status bar reporting Slack and ProPresenter connection state.
//
// Decision 7: every update arrives as a direct signal/slot connection — the
// poll_in_thread status thread from overview.py is deliberately NOT ported.
// The window pulls its data from a PagerController via the refresh() slot, which
// clients trigger by connecting the controller's state signals to it.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(PagerController *pager, QWidget *parent = nullptr);

    // --- Pure formatting (ports overview.py; unit-tested directly) ---------

    // The active/last block. Ports overview.py: a "Last Number: N" line is
    // prepended when `lastNumber` is set; the active line pluralizes to
    // "Active Numbers:" for a multi-number batch (detected by the " & " that
    // PagerController::formatBatch places before the final number) and shows
    // "Active Number: N/A" when nothing is on screen.
    static QString formatActiveLine(const QString &lastNumber,
                                    const QString &activeFormatted);
    // The "Numbers Queued:" block: each batch formatted via
    // PagerController::formatBatch, newline-joined under the header.
    static QString formatQueue(const QList<PagerController::Batch> &batches);

public slots:
    void setSlackConnected(bool connected);
    void setProPresConnected(bool connected);
    // Re-render the active line and queue list from the controller.
    void refresh();
    // Ports mainwindow.py setup_err_alert: a critical modal for fatal
    // config/connection errors (title "Error!").
    void showSetupError(const QString &message);

protected:
    // Hide to the tray instead of quitting (menu-bar app); only Quit exits.
    void closeEvent(QCloseEvent *event) override;

private:
    PagerController *m_pager;
    QLabel *m_activeLabel;
    QLabel *m_queueLabel;
    QLabel *m_slackStatus;
    QLabel *m_proPresStatus;
};

#endif // PROPAGER_UI_MAINWINDOW_H
