#include "slackclient.h"

#include "backoff.h"
#include "config.h"
#include "pagercontroller.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {

constexpr auto kConnectionsOpen = "https://slack.com/api/apps.connections.open";
constexpr auto kReactionsAdd = "https://slack.com/api/reactions.add";
constexpr auto kUsersConversations =
    "https://slack.com/api/users.conversations?exclude_archived=true";

constexpr int kBaseBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;

// Qt's network requests have NO timeout by default. A handshake that hangs
// (typical right after the Mac wakes) would otherwise pin m_openReply forever:
// no reconnect, and every later `disconnect` frame ignored.
constexpr int kHandshakeTimeoutMs = 15000;

// How often a healthy connection writes a heartbeat line to the log.
constexpr qint64 kHeartbeatLogMs = 10 * 60 * 1000;

// Each socket carries its own watchdog as a child, so it dies with the socket
// and the two sockets of a graceful handoff are judged independently.
ConnectionWatchdog *watchdogOf(QWebSocket *sock)
{
    return sock ? sock->findChild<ConnectionWatchdog *>() : nullptr;
}

// A JSON Web-API request authenticated with a Bearer token.
QNetworkRequest bearerJsonRequest(const QString &url, const QString &token)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    return req;
}

} // namespace

SlackClient::SlackClient(const Config &config, PagerController *controller,
                         QObject *parent)
    : QObject(parent), m_config(config), m_controller(controller),
      m_connectionsOpenUrl(QString::fromLatin1(kConnectionsOpen))
{
    m_sinceHeartbeatLog.start();
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this,
            &SlackClient::openConnection);

    // Wire the message-grammar layer (Task 001-7). Inbound messages flow
    // through handleMessage; the controller's state signals drive the deferred
    // emoji feedback. Every page originates on the listen channel, so that is
    // the channel every controller-driven reaction targets. This wiring lives
    // here (not main.cpp) so Task 6 never needs to touch it.
    connect(this, &SlackClient::messageReceived, this,
            &SlackClient::handleMessage);
    if (m_controller) {
        const auto react = [this](const char *emoji, const QString &ts) {
            addReaction(QString::fromLatin1(emoji), m_config.slackListenChannel(),
                        ts);
        };
        connect(m_controller, &PagerController::queuedBusy, this,
                [react](const QString &ts) { react("hourglass", ts); });
        connect(m_controller, &PagerController::onScreen, this,
                [react](const QString &ts) { react("calling", ts); });
        connect(m_controller, &PagerController::cleared, this,
                [react](const QString &ts) { react("thumbsup", ts); });
    }

    // Per-socket lifecycle signals are wired in makeSocket(), because a graceful
    // refresh runs two sockets at once and each handler must know which fired.
}

void SlackClient::setConnectionsOpenUrl(const QUrl &url)
{
    m_connectionsOpenUrl = url;
}

void SlackClient::setWatchdogTiming(ConnectionWatchdog::Timing timing)
{
    m_watchdogTiming = timing;
}

QWebSocket *SlackClient::makeSocket()
{
    auto *sock = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    // Liveness: armed now, so the hello deadline also covers a TCP/TLS/WS
    // handshake that never completes. Pongs to our pings are the proof of life
    // on an idle connection (Qt answers Slack's pings internally, unseen).
    auto *dog = new ConnectionWatchdog(m_watchdogTiming, sock);
    connect(dog, &ConnectionWatchdog::pingDue, this, [sock] {
        if (sock->state() == QAbstractSocket::ConnectedState)
            sock->ping();
    });
    connect(dog, &ConnectionWatchdog::expired, this,
            [this, sock](const QString &reason) { onSocketExpired(sock, reason); });
    connect(sock, &QWebSocket::pong, this, [this, sock, dog](quint64 elapsedMs) {
        dog->noteActivity();
        if (sock == m_socket)
            logHeartbeat(elapsedMs);
    });
    dog->start();
    connect(sock, &QWebSocket::connected, this,
            [this, sock] { onSocketConnected(sock); });
    connect(sock, &QWebSocket::disconnected, this,
            [this, sock] { onSocketDisconnected(sock); });
    connect(sock, &QWebSocket::textMessageReceived, this,
            [this, sock](const QString &frame) { onSocketFrame(sock, frame); });
    connect(sock, &QWebSocket::errorOccurred, this,
            [this, sock](QAbstractSocket::SocketError) {
                emit error(QStringLiteral("Slack socket error: %1")
                               .arg(sock->errorString()));
            });
    return sock;
}

void SlackClient::retireSocket(QWebSocket *sock)
{
    if (!sock)
        return;
    // Detach first so the ensuing close does NOT reach onSocketDisconnected:
    // a retired socket must not trigger a reconnect or flap the status light.
    if (ConnectionWatchdog *dog = watchdogOf(sock)) {
        dog->stop();
        dog->disconnect(this);
    }
    sock->disconnect(this);
    sock->close();
    sock->deleteLater();
}

void SlackClient::promotePending()
{
    if (!m_pending)
        return;
    qInfo() << "[slack] replacement connection live; retiring previous connection";
    QWebSocket *old = m_socket; // may be nullptr if the old one died mid-handoff
    m_socket = m_pending;
    m_pending = nullptr;
    retireSocket(old);
    // The app was already "connected" throughout the handoff, so there is no
    // status transition to emit — that is the whole point of make-before-break.
}

// --- Pure helpers -------------------------------------------------------

QJsonObject SlackClient::buildAck(const QJsonObject &envelope)
{
    const QString id = envelope.value("envelope_id").toString();
    if (id.isEmpty()) {
        return QJsonObject();
    }
    return QJsonObject{{"envelope_id", id}};
}

QJsonObject SlackClient::extractMessageEvent(const QJsonObject &envelope,
                                             const QString &listenChannel)
{
    const QJsonObject event =
        envelope.value("payload").toObject().value("event").toObject();

    if (event.value("type").toString() != QLatin1String("message")) {
        return QJsonObject();
    }
    if (event.value("channel").toString() != listenChannel) {
        return QJsonObject();
    }

    return QJsonObject{{"text", event.value("text").toString()},
                       {"ts", event.value("ts").toString()},
                       {"channel", event.value("channel").toString()}};
}

QString SlackClient::wssUrlFromConnectionsOpen(const QJsonObject &response)
{
    if (!response.value("ok").toBool()) {
        return QString();
    }
    return response.value("url").toString();
}

QVector<SlackChannel>
SlackClient::channelsFromConversations(const QJsonObject &response)
{
    QVector<SlackChannel> channels;
    const QJsonArray raw = response.value("channels").toArray();
    for (const QJsonValue &val : raw) {
        const QJsonObject obj = val.toObject();
        if (obj.value("is_channel").toBool()) {
            channels.append({obj.value("name").toString(),
                             obj.value("id").toString()});
        }
    }
    return channels;
}

int SlackClient::nextBackoffMs(int currentMs)
{
    return ::nextBackoffMs(currentMs, kBaseBackoffMs, kMaxBackoffMs);
}

SlackClient::ParsedCommand SlackClient::parseCommand(const QString &text,
                                                     const QStringList &ignoreNumbers,
                                                     bool hasLastNumber)
{
    // Ports the original bot.py's on_message precedence exactly (git history).
    // '!'-prefix drops first (even
    // when the text contains a number), then a 4-digit run wins over the
    // repeat/cancel substrings, then repeat before cancel.
    if (text.startsWith(QLatin1Char('!'))) {
        return {MessageAction::Ignore, QString()};
    }

    // Ports re.search(r"(?:\d){4}"): the first run of four digits in the text.
    static const QRegularExpression fourDigits(QStringLiteral("(?:\\d){4}"));
    const QRegularExpressionMatch match = fourDigits.match(text);
    if (match.hasMatch()) {
        const QString number = match.captured(0);
        if (ignoreNumbers.contains(number)) {
            return {MessageAction::IgnoredNumber, number};
        }
        return {MessageAction::Page, number};
    }

    const QString lower = text.toLower();
    if (lower.contains(QLatin1String("repeat"))) {
        return {hasLastNumber ? MessageAction::Repeat
                              : MessageAction::RepeatNoLast,
                QString()};
    }
    if (lower.contains(QLatin1String("cancel"))) {
        return {MessageAction::Cancel, QString()};
    }
    return {MessageAction::Ignore, QString()};
}

// --- Network methods ----------------------------------------------------

void SlackClient::start()
{
    openConnection();
}

void SlackClient::reconnectNow()
{
    // Cancel any pending backoff and reset it, so the reconnect happens now
    // rather than after the accumulated exponential wait.
    m_reconnectTimer.stop();
    m_backoffMs = 0;

    // Abort a pending handshake reply so it is not leaked and cannot open a
    // second socket when it returns.
    if (m_openReply) {
        QNetworkReply *stale = m_openReply;
        m_openReply = nullptr;
        stale->abort();
    }
    // Retire both the live socket and any in-flight graceful replacement so
    // exactly one fresh socket remains after the reopen. retireSocket detaches
    // signals first, so neither close reaches onSocketDisconnected (no stray
    // backoff reschedule, no competing socket) — openConnection drives the
    // reopen itself.
    retireSocket(m_pending);
    m_pending = nullptr;
    retireSocket(m_socket);
    m_socket = nullptr;

    qInfo() << "[slack] manual reconnect: reopening now (backoff reset)";
    openConnection();
}

void SlackClient::openConnection()
{
    requestConnection(/*graceful=*/false);
}

void SlackClient::beginGracefulReconnect()
{
    qInfo() << "[slack] disconnect requested by Slack; bringing up replacement "
               "connection (make-before-break)";
    requestConnection(/*graceful=*/true);
}

void SlackClient::requestConnection(bool graceful)
{
    // POST apps.connections.open (app-level token) -> wss:// URL, then open the
    // socket. This replaces the slack-bolt Socket Mode handler (Decision 8).
    // graceful=true opens the replacement (m_pending) while the current socket
    // keeps running; graceful=false opens the active socket (m_socket) directly.
    qDebug() << "[slack] POST apps.connections.open (app-level token)";
    QNetworkRequest req = bearerJsonRequest(m_connectionsOpenUrl.toString(),
                                            m_config.slackAppToken());
    req.setTransferTimeout(kHandshakeTimeoutMs);
    QNetworkReply *reply = m_nam.post(req, QByteArray());
    m_openReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, graceful] {
        // Deliberate aborts (reconnectNow, an active-socket death during a
        // graceful handshake) clear m_openReply BEFORE aborting. A transfer
        // timeout also surfaces as OperationCanceledError, but leaves
        // m_openReply pointing here — so only a superseded reply is silent.
        const bool superseded = (m_openReply != reply);
        if (!superseded)
            m_openReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (superseded &&
                reply->error() == QNetworkReply::OperationCanceledError)
                return;
            const QString why =
                reply->error() == QNetworkReply::OperationCanceledError
                    ? QStringLiteral("timed out after %1 ms").arg(kHandshakeTimeoutMs)
                    : reply->errorString();
            emit error(QStringLiteral("apps.connections.open failed: %1").arg(why));
            // A graceful handshake that fails leaves the live socket untouched;
            // don't reconnect-storm. Slack will force-close it later, and that
            // death drives the ordinary backoff reconnect.
            if (graceful) {
                qWarning() << "[slack] replacement handshake failed; keeping "
                              "current connection";
                return;
            }
            scheduleReconnect();
            return;
        }

        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString url = wssUrlFromConnectionsOpen(resp);
        if (url.isEmpty()) {
            emit error(QStringLiteral("apps.connections.open returned no URL: %1")
                           .arg(resp.value("error").toString()));
            if (graceful) {
                qWarning() << "[slack] replacement handshake returned no URL; "
                              "keeping current connection";
                return;
            }
            scheduleReconnect();
            return;
        }

        QWebSocket *sock = makeSocket();
        if (graceful) {
            m_pending = sock;
            qInfo() << "[slack] opening replacement Socket Mode connection";
        } else {
            m_socket = sock;
            qInfo() << "[slack] opening Socket Mode connection";
        }
        sock->open(QUrl(url));
    });
}

void SlackClient::scheduleReconnect()
{
    m_backoffMs = nextBackoffMs(m_backoffMs);
    qWarning() << "[slack] reconnecting in" << m_backoffMs << "ms";
    m_reconnectTimer.start(m_backoffMs);
}

void SlackClient::onSocketConnected(QWebSocket *sock)
{
    // The WebSocket is up, but Slack hasn't confirmed the session. Backoff
    // reset, connected(), and promotion all wait for its `hello`
    // (onSocketFrame): a socket that opens but is never confirmed is not
    // healthy, and its watchdog will tear it down.
    if (sock == m_pending) {
        qInfo() << "[slack] replacement connection established; awaiting hello";
        return;
    }
    qInfo() << "[slack] Socket Mode socket open; awaiting hello";
}

void SlackClient::onSocketExpired(QWebSocket *sock, const QString &reason)
{
    const char *which = sock == m_pending  ? "replacement"
                        : sock == m_socket ? "active"
                                           : "retired";
    qWarning().noquote() << "[slack]" << which
                         << "connection presumed dead:" << reason
                         << "- dropping it and reconnecting";
    // abort(), not close(): a close handshake on a dead link would just hang.
    // Detach first so the abort's own disconnected() can't double-handle it,
    // then run the ordinary disconnect bookkeeping ourselves.
    sock->disconnect(this);
    sock->abort();
    onSocketDisconnected(sock);
}

void SlackClient::logHeartbeat(quint64 rttMs)
{
    if (m_sinceHeartbeatLog.elapsed() < kHeartbeatLogMs)
        return;
    m_sinceHeartbeatLog.restart();
    qInfo().noquote() << QStringLiteral("[slack] heartbeat ok (pong in %1 ms)")
                             .arg(rttMs);
}

void SlackClient::onSocketDisconnected(QWebSocket *sock)
{
    // Whatever closed it, this socket is finished: its watchdog must not fire
    // between now and the deferred delete.
    if (ConnectionWatchdog *dog = watchdogOf(sock))
        dog->stop();
    if (sock == m_pending) {
        // The replacement died before it could take over. The live socket is
        // still serving, so this is invisible to the app unless it too is gone.
        qWarning() << "[slack] replacement connection dropped before promotion";
        m_pending = nullptr;
        sock->deleteLater();
        if (!m_socket) {
            emit disconnected();
            scheduleReconnect();
        }
        return;
    }
    if (sock == m_socket) {
        qWarning() << "[slack] Socket Mode disconnected";
        m_socket = nullptr;
        sock->deleteLater();
        if (m_pending) {
            // A graceful replacement is already mid-handshake; let it promote
            // rather than opening a competing socket, and don't flap status.
            return;
        }
        // Cancel any in-flight graceful handshake so it can't open a stray
        // socket that races the backoff reconnect below.
        if (m_openReply) {
            QNetworkReply *stale = m_openReply;
            m_openReply = nullptr;
            stale->abort();
        }
        emit disconnected();
        scheduleReconnect();
        return;
    }
    // A retired socket normally has its signals detached before close; this is
    // only a defensive cleanup path.
    sock->deleteLater();
}

void SlackClient::onSocketFrame(QWebSocket *sock, const QString &frame)
{
    const QJsonObject envelope =
        QJsonDocument::fromJson(frame.toUtf8()).object();

    const QString frameType = envelope.value("type").toString();
    qDebug().noquote() << "[slack] frame received, type=" << frameType;
    if (ConnectionWatchdog *dog = watchdogOf(sock))
        dog->noteActivity();

    // Ack first (Slack expects the ack before the event is acted on). The ack
    // MUST go back on the socket the frame arrived on — during a graceful
    // handoff both sockets are briefly live.
    const QJsonObject ack = buildAck(envelope);
    if (!ack.isEmpty()) {
        sock->sendTextMessage(
            QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));
        qDebug().noquote() << "[slack] acked envelope"
                           << envelope.value("envelope_id").toString();
    }

    // Slack asks us to reconnect ahead of closing this connection. Bring up a
    // replacement now, while this socket still delivers events. Only the live
    // socket triggers it, and only when no handoff is already under way — Slack
    // sends two disconnect frames (a warning, then refresh_requested).
    if (frameType == QLatin1String("disconnect")) {
        if (sock == m_socket && !m_pending && !m_openReply)
            beginGracefulReconnect();
        return;
    }
    // Slack confirmed the session. A replacement takes over now; the first
    // active socket is what the app reports as connected.
    if (frameType == QLatin1String("hello")) {
        if (ConnectionWatchdog *dog = watchdogOf(sock))
            dog->noteHello();
        m_backoffMs = 0; // a confirmed session resets backoff
        const int lifetimeS = envelope.value("debug_info")
                                  .toObject()
                                  .value("approximate_connection_time")
                                  .toInt();
        if (lifetimeS > 0)
            qInfo().noquote() << QStringLiteral("[slack] Slack expects to refresh "
                                                "this connection in ~%1 min")
                                     .arg(lifetimeS / 60);
        if (sock == m_pending) {
            promotePending();
        } else if (sock == m_socket) {
            qInfo() << "[slack] Socket Mode connected";
            emit connected();
        }
        return;
    }

    const QString listen = m_config.slackListenChannel();
    const QJsonObject msg = extractMessageEvent(envelope, listen);
    if (!msg.isEmpty()) {
        qDebug().noquote() << "[slack] message on listen channel" << listen
                           << "text:" << msg.value("text").toString();
        emit messageReceived(msg.value("text").toString(),
                             msg.value("ts").toString(),
                             msg.value("channel").toString());
    } else {
        // The most common setup mistake is a wrong listen-channel or the bot not
        // being in the channel: surface a real message event that we dropped
        // because its channel did not match, so it is diagnosable from the log.
        const QJsonObject event =
            envelope.value("payload").toObject().value("event").toObject();
        if (event.value("type").toString() == QLatin1String("message")) {
            qDebug().noquote()
                << "[slack] ignoring message on channel"
                << event.value("channel").toString() << "(listening on" << listen
                << ")";
        }
    }
}

void SlackClient::handleMessage(const QString &text, const QString &ts,
                                const QString &channel)
{
    // Ports the body of on_message from the original bot.py (git history). Classification is pure
    // (parseCommand); this method performs the side effects. Deferred feedback
    // (⌛ on busy-enqueue, 📞 on screen, 👍 on clear) is emitted by the
    // controller's signals wired in the constructor — only the immediate
    // reactions live here.
    const ParsedCommand cmd =
        parseCommand(text, m_config.slackIgnoreNumbers(), !m_lastNumber.isEmpty());

    const auto actionName = [](MessageAction a) -> const char * {
        switch (a) {
        case MessageAction::Ignore: return "Ignore";
        case MessageAction::IgnoredNumber: return "IgnoredNumber";
        case MessageAction::Page: return "Page";
        case MessageAction::Repeat: return "Repeat";
        case MessageAction::RepeatNoLast: return "RepeatNoLast";
        case MessageAction::Cancel: return "Cancel";
        }
        return "?";
    };
    qDebug().noquote() << "[slack] parsed action=" << actionName(cmd.action)
                       << "number=" << cmd.number;

    switch (cmd.action) {
    case MessageAction::Ignore:
        return;
    case MessageAction::IgnoredNumber:
        addReaction(QStringLiteral("x"), channel, ts); // ❌
        return;
    case MessageAction::Page:
        m_lastNumber = cmd.number;
        if (m_controller)
            m_controller->enqueueNumber(ts, cmd.number);
        return;
    case MessageAction::Repeat:
        // Re-send the prior number as a fresh page, keyed to THIS message's ts
        // so its feedback lands on the "repeat" message (the original bot.py's sender(ts)).
        if (m_controller)
            m_controller->enqueueNumber(ts, m_lastNumber);
        return;
    case MessageAction::RepeatNoLast:
        addReaction(QStringLiteral("thumbsdown"), channel, ts); // 👎
        return;
    case MessageAction::Cancel:
        if (m_controller)
            m_controller->cancel();
        addReaction(QStringLiteral("thumbsup"), channel, ts); // 👍
        return;
    }
}

void SlackClient::addReaction(const QString &emoji, const QString &channel,
                              const QString &ts)
{
    const QJsonObject body{{"channel", channel}, {"name", emoji},
                           {"timestamp", ts}};
    QNetworkReply *reply =
        m_nam.post(bearerJsonRequest(kReactionsAdd, m_config.slackBotToken()),
                   QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, emoji] {
        reply->deleteLater();
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError ||
            !resp.value("ok").toBool()) {
            emit error(QStringLiteral("reactions.add(%1) failed: %2")
                           .arg(emoji, resp.value("error").toString()));
        }
    });
}

void SlackClient::listChannels()
{
    QNetworkReply *reply = m_nam.get(
        bearerJsonRequest(kUsersConversations, m_config.slackBotToken()));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("users.conversations failed: %1")
                           .arg(reply->errorString()));
            return;
        }
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        emit channelsListed(channelsFromConversations(resp));
    });
}
