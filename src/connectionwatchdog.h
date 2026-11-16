#ifndef PROPAGER_CONNECTIONWATCHDOG_H
#define PROPAGER_CONNECTIONWATCHDOG_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

// ConnectionWatchdog decides when a Socket Mode connection is dead even though
// the OS never reported it closed. It owns no socket: the owner feeds it
// activity and turns its signals into socket actions, which keeps the timing
// logic unit-testable without a network.
//
// Why it exists: a half-open TCP connection (Mac asleep, Wi-Fi roam, a NAT or
// firewall silently dropping the flow) never fires QWebSocket::disconnected,
// so without this the app sat "connected" for hours receiving nothing. Qt
// answers Slack's server pings internally and exposes no signal for them, so
// the only observable proof of life is a pong to OUR ping, or any inbound
// frame. Slack's own SDKs use the same client-ping model.
//
// Two deadlines, both reported via expired() (once; the watchdog then stops):
//   - hello:  Slack must confirm the session (`hello` frame) within
//             helloDeadlineMs of start(). Covers a hung TLS/WS handshake and a
//             socket that opens but is never confirmed.
//   - stale:  after hello, some activity must arrive within staleAfterMs.
// pingDue() fires every pingIntervalMs while running, so the owner can ping and
// keep a healthy-but-idle connection producing pongs.
class ConnectionWatchdog : public QObject
{
    Q_OBJECT

public:
    struct Timing
    {
        int pingIntervalMs = 10000;
        int staleAfterMs = 30000;
        int helloDeadlineMs = 15000;
    };

    explicit ConnectionWatchdog(QObject *parent = nullptr);
    explicit ConnectionWatchdog(Timing timing, QObject *parent = nullptr);

    // Arm both deadlines from now. Restarting re-arms from scratch.
    void start();
    void stop();
    bool isRunning() const;

    // Any inbound frame or pong: the connection is demonstrably alive.
    void noteActivity();
    // Slack confirmed the session; the hello deadline no longer applies.
    void noteHello();

signals:
    void pingDue();
    void expired(const QString &reason);

private:
    void tick();
    void expire(const QString &reason);

    Timing m_timing;
    QTimer m_tick;
    QTimer m_helloDeadline;
    QElapsedTimer m_sinceActivity;
    bool m_helloSeen = false;
};

#endif // PROPAGER_CONNECTIONWATCHDOG_H
