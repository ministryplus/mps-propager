#include "slackclient.h"

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
    : QObject(parent), m_config(config), m_controller(controller)
{
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

    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_backoffMs = 0; // healthy connection resets backoff
        qInfo() << "[slack] Socket Mode connected";
        emit connected();
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        qWarning() << "[slack] Socket Mode disconnected";
        emit disconnected();
        // A close driven by reconnectNow() must not also arm the auto-backoff:
        // reconnectNow reopens immediately, and a stray timer would open a
        // second socket. Consume the flag and skip the reschedule once.
        if (m_manualClose) {
            m_manualClose = false;
            return;
        }
        scheduleReconnect();
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this,
            &SlackClient::onTextMessageReceived);
    connect(&m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit error(QStringLiteral("Slack socket error: %1")
                               .arg(m_socket.errorString()));
            });
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
    if (currentMs <= 0) {
        return kBaseBackoffMs;
    }
    if (currentMs >= kMaxBackoffMs) {
        return kMaxBackoffMs;
    }
    return qMin(currentMs * 2, kMaxBackoffMs);
}

SlackClient::ParsedCommand SlackClient::parseCommand(const QString &text,
                                                     const QStringList &ignoreNumbers,
                                                     bool hasLastNumber)
{
    // Ports bot.py on_message precedence exactly. '!'-prefix drops first (even
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

    // Safe-while-connected: tear down an existing/connecting socket so exactly
    // one QWebSocket remains after the reopen. Suppress the auto-backoff for
    // this intentional close (openConnection below drives the reopen).
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_manualClose = true;
        m_socket.close();
    }
    // Abort a pending handshake reply so it is not leaked and cannot open a
    // second socket when it returns.
    if (m_openReply) {
        QNetworkReply *stale = m_openReply;
        m_openReply = nullptr;
        stale->abort();
    }

    qInfo() << "[slack] manual reconnect: reopening now (backoff reset)";
    openConnection();
}

void SlackClient::openConnection()
{
    // POST apps.connections.open (app-level token) -> wss:// URL, then open the
    // socket. This replaces the slack-bolt Socket Mode handler (Decision 8).
    qDebug() << "[slack] POST apps.connections.open (app-level token)";
    QNetworkReply *reply = m_nam.post(
        bearerJsonRequest(kConnectionsOpen, m_config.slackAppToken()),
        QByteArray());
    m_openReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_openReply == reply)
            m_openReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // A deliberate abort from reconnectNow() is not a failure: the
            // superseding openConnection() reports its own outcome.
            if (reply->error() == QNetworkReply::OperationCanceledError)
                return;
            emit error(QStringLiteral("apps.connections.open failed: %1")
                           .arg(reply->errorString()));
            scheduleReconnect();
            return;
        }

        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString url = wssUrlFromConnectionsOpen(resp);
        if (url.isEmpty()) {
            emit error(QStringLiteral("apps.connections.open returned no URL: %1")
                           .arg(resp.value("error").toString()));
            scheduleReconnect();
            return;
        }

        qInfo() << "[slack] opening Socket Mode connection";
        m_socket.open(QUrl(url));
    });
}

void SlackClient::scheduleReconnect()
{
    m_backoffMs = nextBackoffMs(m_backoffMs);
    qWarning() << "[slack] reconnecting in" << m_backoffMs << "ms";
    m_reconnectTimer.start(m_backoffMs);
}

void SlackClient::onTextMessageReceived(const QString &frame)
{
    const QJsonObject envelope =
        QJsonDocument::fromJson(frame.toUtf8()).object();

    const QString frameType = envelope.value("type").toString();
    qDebug().noquote() << "[slack] frame received, type=" << frameType;

    // Ack first (Slack expects the ack before the event is acted on).
    const QJsonObject ack = buildAck(envelope);
    if (!ack.isEmpty()) {
        m_socket.sendTextMessage(
            QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));
        qDebug().noquote() << "[slack] acked envelope"
                           << envelope.value("envelope_id").toString();
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
    // Ports the body of bot.py on_message. Classification is pure
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
        // so its feedback lands on the "repeat" message (bot.py sender(ts)).
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
