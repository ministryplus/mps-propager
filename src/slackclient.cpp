#include "slackclient.h"

#include "config.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
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

SlackClient::SlackClient(const Config &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this,
            &SlackClient::openConnection);

    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_backoffMs = 0; // healthy connection resets backoff
        qInfo() << "[slack] Socket Mode connected";
        emit connected();
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        qWarning() << "[slack] Socket Mode disconnected";
        emit disconnected();
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

// --- Network methods ----------------------------------------------------

void SlackClient::start()
{
    openConnection();
}

void SlackClient::openConnection()
{
    // POST apps.connections.open (app-level token) -> wss:// URL, then open the
    // socket. This replaces the slack-bolt Socket Mode handler (Decision 8).
    QNetworkReply *reply = m_nam.post(
        bearerJsonRequest(kConnectionsOpen, m_config.slackAppToken()),
        QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
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

    // Ack first (Slack expects the ack before the event is acted on).
    const QJsonObject ack = buildAck(envelope);
    if (!ack.isEmpty()) {
        m_socket.sendTextMessage(
            QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));
    }

    const QJsonObject msg =
        extractMessageEvent(envelope, m_config.slackListenChannel());
    if (!msg.isEmpty()) {
        emit messageReceived(msg.value("text").toString(),
                             msg.value("ts").toString(),
                             msg.value("channel").toString());
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
