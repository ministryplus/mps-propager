#include "propresenterclient.h"

#include "config.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

// The message name and token grammar are fixed by Decision 4.
constexpr auto kMessageName = "ProPager";
constexpr auto kTokenName = "Number";
constexpr auto kMessageText = "VK: {Number}";
constexpr auto kPlaceholderNumber = "142"; // shown only on first creation

QNetworkRequest jsonRequest(const QString &url)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return req;
}

} // namespace

ProPresenterClient::ProPresenterClient(const Config &config, QObject *parent)
    : QObject(parent), m_config(config)
{
}

// --- Pure helpers -------------------------------------------------------

QString ProPresenterClient::apiBase(const QString &host, int port)
{
    return QStringLiteral("http://%1:%2").arg(host).arg(port);
}

QJsonObject ProPresenterClient::buildMessageBody(const QString &name,
                                                 const QString &number,
                                                 const QJsonObject &theme)
{
    // Decision-4 body shape. id.uuid is left empty (no legacy hardcoded UUID);
    // the message record is identified in the URL path, not this body.
    QJsonObject id{{"name", name}, {"uuid", ""}, {"index", 0}};
    QJsonObject token{{"name", kTokenName},
                      {"text", QJsonObject{{"text", number}}}};
    return QJsonObject{{"id", id},
                       {"message", kMessageText},
                       {"tokens", QJsonArray{token}},
                       {"theme", theme},
                       {"visible_on_network", true}};
}

QJsonObject ProPresenterClient::pickTheme(const QJsonArray &themes)
{
    // Port of bot.py propres_create_message: first slide whose id.name contains
    // "vk" (case-insensitive) wins; otherwise fall back to themes[0].slides[0].
    for (const QJsonValue &themeVal : themes) {
        const QJsonArray slides = themeVal.toObject().value("slides").toArray();
        for (const QJsonValue &slideVal : slides) {
            const QJsonObject id = slideVal.toObject().value("id").toObject();
            if (id.value("name").toString().contains("vk", Qt::CaseInsensitive)) {
                return id;
            }
        }
    }

    if (!themes.isEmpty()) {
        const QJsonArray slides =
            themes.first().toObject().value("slides").toArray();
        if (!slides.isEmpty()) {
            return slides.first().toObject().value("id").toObject();
        }
    }

    return QJsonObject();
}

QJsonObject ProPresenterClient::findMessageId(const QJsonArray &messages,
                                              const QString &name)
{
    for (const QJsonValue &msgVal : messages) {
        const QJsonObject id = msgVal.toObject().value("id").toObject();
        if (id.value("name").toString() == name) {
            return id;
        }
    }
    return QJsonObject();
}

QString ProPresenterClient::pathId(const QJsonObject &idObject)
{
    const QString uuid = idObject.value("uuid").toString();
    return uuid.isEmpty() ? idObject.value("name").toString() : uuid;
}

// --- Network methods ----------------------------------------------------

QString ProPresenterClient::apiBase() const
{
    return apiBase(m_config.propresenterHost(), m_config.propresenterPort());
}

QString ProPresenterClient::messagePath() const
{
    return apiBase() + QStringLiteral("/v1/message/") + pathId(m_messageId);
}

void ProPresenterClient::ensureMessage()
{
    QNetworkReply *reply = m_nam.get(jsonRequest(apiBase() + "/v1/messages"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to reach ProPresenter: %1")
                           .arg(reply->errorString()));
            emit disconnected();
            return;
        }

        emit connected();
        const QJsonArray messages =
            QJsonDocument::fromJson(reply->readAll()).array();
        m_messageId = findMessageId(messages, kMessageName);

        if (m_messageId.isEmpty()) {
            createMessage(); // will clear on completion
        } else {
            clear(); // startup recovery: known-empty state
        }
    });
}

void ProPresenterClient::createMessage()
{
    // Look up a theme, then POST /v1/messages to create the "ProPager" message.
    QNetworkReply *themesReply = m_nam.get(jsonRequest(apiBase() + "/v1/themes"));
    connect(themesReply, &QNetworkReply::finished, this, [this, themesReply] {
        themesReply->deleteLater();
        if (themesReply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to load ProPresenter themes: %1")
                           .arg(themesReply->errorString()));
            return;
        }

        const QJsonArray themes =
            QJsonDocument::fromJson(themesReply->readAll()).array();
        const QJsonObject theme = pickTheme(themes);
        const QJsonObject body =
            buildMessageBody(kMessageName, kPlaceholderNumber, theme);

        QNetworkReply *createReply =
            m_nam.post(jsonRequest(apiBase() + "/v1/messages"),
                       QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(createReply, &QNetworkReply::finished, this, [this, createReply] {
            createReply->deleteLater();
            if (createReply->error() != QNetworkReply::NoError) {
                emit error(QStringLiteral("Failed to create ProPager message: %1")
                               .arg(createReply->errorString()));
                return;
            }
            const QJsonObject created =
                QJsonDocument::fromJson(createReply->readAll()).object();
            m_messageId = created.value("id").toObject();
            clear(); // startup recovery on the freshly created message
        });
    });
}

void ProPresenterClient::setNumber(const QString &number)
{
    if (m_messageId.isEmpty()) {
        emit error(QStringLiteral("Cannot set number: ProPager message not ready"));
        return;
    }
    // PUT the Decision-4 body with an empty theme (theme is fixed at creation).
    const QJsonObject body = buildMessageBody(
        kMessageName, number, QJsonObject{{"name", ""}, {"uuid", ""}, {"index", 0}});
    QNetworkReply *reply = m_nam.put(jsonRequest(messagePath()),
                                     QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to set number: %1")
                           .arg(reply->errorString()));
        }
    });
}

void ProPresenterClient::trigger()
{
    if (m_messageId.isEmpty()) {
        emit error(QStringLiteral("Cannot trigger: ProPager message not ready"));
        return;
    }
    // POST /v1/message/{id}/trigger (confirmed method — not GET). The optional
    // token-override body is empty; the message shows with its current tokens.
    // Success is 204 No Content: treat any non-error reply as success and do
    // not parse a body.
    QNetworkReply *reply = m_nam.post(jsonRequest(messagePath() + "/trigger"),
                                      QByteArray("[]"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to trigger message: %1")
                           .arg(reply->errorString()));
        }
    });
}

void ProPresenterClient::clear()
{
    if (m_messageId.isEmpty()) {
        emit error(QStringLiteral("Cannot clear: ProPager message not ready"));
        return;
    }
    // GET /v1/message/{id}/clear — per-message clear (Decision 5), 204 on
    // success. Never the layer-wide /v1/clear/layer/messages.
    QNetworkReply *reply = m_nam.get(jsonRequest(messagePath() + "/clear"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to clear message: %1")
                           .arg(reply->errorString()));
        }
    });
}
