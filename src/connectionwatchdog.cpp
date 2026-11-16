#include "connectionwatchdog.h"

ConnectionWatchdog::ConnectionWatchdog(QObject *parent)
    : ConnectionWatchdog(Timing{}, parent)
{
}

ConnectionWatchdog::ConnectionWatchdog(Timing timing, QObject *parent)
    : QObject(parent), m_timing(timing)
{
    m_tick.setInterval(m_timing.pingIntervalMs);
    connect(&m_tick, &QTimer::timeout, this, &ConnectionWatchdog::tick);

    m_helloDeadline.setSingleShot(true);
    m_helloDeadline.setInterval(m_timing.helloDeadlineMs);
    connect(&m_helloDeadline, &QTimer::timeout, this, [this] {
        expire(QStringLiteral("no hello from Slack within %1 ms")
                   .arg(m_timing.helloDeadlineMs));
    });
}

void ConnectionWatchdog::start()
{
    m_helloSeen = false;
    m_sinceActivity.start();
    m_tick.start();
    m_helloDeadline.start();
}

void ConnectionWatchdog::stop()
{
    m_tick.stop();
    m_helloDeadline.stop();
}

bool ConnectionWatchdog::isRunning() const
{
    return m_tick.isActive();
}

void ConnectionWatchdog::noteActivity()
{
    m_sinceActivity.restart();
}

void ConnectionWatchdog::noteHello()
{
    m_helloSeen = true;
    m_helloDeadline.stop();
    noteActivity();
}

void ConnectionWatchdog::tick()
{
    // Before hello, only the hello deadline applies (a connecting socket has
    // no traffic yet, and pinging it is pointless).
    if (!m_helloSeen)
        return;
    if (m_sinceActivity.elapsed() >= m_timing.staleAfterMs) {
        expire(QStringLiteral("no traffic for %1 ms").arg(m_sinceActivity.elapsed()));
        return;
    }
    emit pingDue();
}

void ConnectionWatchdog::expire(const QString &reason)
{
    stop();
    emit expired(reason);
}
