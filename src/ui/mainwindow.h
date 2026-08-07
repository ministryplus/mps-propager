#ifndef PROPAGER_UI_MAINWINDOW_H
#define PROPAGER_UI_MAINWINDOW_H

#include <QList>
#include <QMainWindow>
#include <QString>

#include "pagercontroller.h" // for PagerController::Batch in the static API

class QLabel;
class QPlainTextEdit;
class QCloseEvent;

// MainWindow is ProPager's status window (Task 001-6), the native Qt6 port of
// overview.py + mainwindow.py. A QTabWidget holds two tabs:
//   - Overview: the active number, the queued-numbers list, and a status bar
//     reporting Slack and ProPresenter connection state.
//   - Log: a scrolling, read-only view of the app's error/access log, plus
//     buttons to reveal the on-disk log file and clear the view.
//
// Decision 7: every update arrives as a direct signal/slot connection — the
// poll_in_thread status thread from overview.py is deliberately NOT ported.
// The Log tab replaces the modal error dialog (a QMessageBox spins a nested
// event loop, so bursts of errors stacked modals and could wedge the UI); it
// makes an otherwise-unactionable "Disconnected" status actionable by showing
// the actual failure text.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `logDir` is the directory holding ProPager.log (Config::configDir()); the
    // Log tab's "Reveal Log File" button opens it.
    explicit MainWindow(PagerController *pager, const QString &logDir,
                        QWidget *parent = nullptr);

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
    // Append one log line to the Log tab (connect LogBroker::appended here).
    void appendLog(const QString &line);

protected:
    // Hide to the tray instead of quitting (menu-bar app); only Quit exits.
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildOverviewTab();
    QWidget *buildLogTab();

    PagerController *m_pager;
    QString m_logDir;
    QLabel *m_activeLabel;
    QLabel *m_queueLabel;
    QLabel *m_slackStatus;
    QLabel *m_proPresStatus;
    QPlainTextEdit *m_logView;
};

#endif // PROPAGER_UI_MAINWINDOW_H
