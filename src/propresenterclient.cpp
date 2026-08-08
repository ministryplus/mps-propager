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
    // is_active is required by ProPresenter 21.x (POST/PUT reject the body with
    // "missing field `is_active`" otherwise). The message is created/updated
    // inactive; trigger() shows it and clear() hides it (Decision 5).
    return QJsonObject{{"id", id},
                       {"message", kMessageText},
                       {"tokens", QJsonArray{token}},
                       {"theme", theme},
                       {"visible_on_network", true},
                       {"is_active", false}};
}

QJsonArray ProPresenterClient::themesFromResponse(const QJsonObject &response)
{
    QJsonArray flat = response.value("themes").toArray();
    for (const QJsonValue &groupVal : response.value("groups").toArray()) {
        const QJsonArray groupThemes =
            groupVal.toObject().value("themes").toArray();
        for (const QJsonValue &theme : groupThemes) {
            flat.append(theme);
        }
    }
    return flat;
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
    // Safe-while-connected (Task 002-3): abort any in-flight ensure first so a
    // re-entry (reconnect) cannot leak the previous reply or double-resolve
    // m_messageId. Clear the tracking pointer BEFORE aborting so the aborted
    // reply's own finished handler no-ops instead of racing this one.
    if (m_ensureReply) {
        QNetworkReply *stale = m_ensureReply;
        m_ensureReply = nullptr;
        stale->abort();
    }

    qInfo().noquote() << "[ProPresenter] GET messages (ensure) ->" << apiBase();
    QNetworkReply *reply = m_nam.get(jsonRequest(apiBase() + "/v1/messages"));
    m_ensureReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_ensureReply == reply)
            m_ensureReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // A deliberate abort from a reconnect() re-entry is not a failure:
            // the superseding ensure reports its own outcome.
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                qDebug().noquote()
                    << "[ProPresenter] ensure aborted (superseded by reconnect)";
                return;
            }
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
            qInfo().noquote() << "[ProPresenter] connected; ProPager message not "
                                 "found — creating it";
            createMessage(); // will clear on completion
        } else {
            qInfo().noquote() << "[ProPresenter] connected; adopted existing "
                                 "ProPager message" << pathId(m_messageId);
            clear(); // startup recovery: known-empty state
        }
    });
}

void ProPresenterClient::reconnect()
{
    // Re-run the connect/ensure path from current Config. Discard any stale
    // resolved id — the host may have changed, so the old message record is not
    // reused; ensureMessage() re-resolves (find-or-create) it. ensureMessage()
    // aborts any in-flight ensure, so a rapid double reconnect() is safe.
    m_messageId = QJsonObject();
    ensureMessage();
}

void ProPresenterClient::test()
{
    // Reachability + adopt/create readiness only (Decision 10): GET the message
    // list; if the ProPager message exists it is adoptable, otherwise confirm a
    // theme exists so it could be created. Never set/trigger/clear. The result
    // is reported via tested() so the async REST call does not block.
    QNetworkReply *reply = m_nam.get(jsonRequest(apiBase() + "/v1/messages"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit tested({/*reachable=*/false, /*messageReady=*/false,
                         QStringLiteral("Cannot reach ProPresenter at %1: %2")
                             .arg(apiBase(), reply->errorString())});
            return;
        }

        const QJsonArray messages =
            QJsonDocument::fromJson(reply->readAll()).array();
        if (!findMessageId(messages, kMessageName).isEmpty()) {
            emit tested({true, true,
                         QStringLiteral("Reachable; ProPager message found")});
            return;
        }

        // Not present yet — confirm it could be created (a theme is available)
        // without actually creating it.
        QNetworkReply *themesReply =
            m_nam.get(jsonRequest(apiBase() + "/v1/themes"));
        connect(themesReply, &QNetworkReply::finished, this, [this, themesReply] {
            themesReply->deleteLater();
            if (themesReply->error() != QNetworkReply::NoError) {
                emit tested({true, false,
                             QStringLiteral("Reachable, but themes unavailable: %1")
                                 .arg(themesReply->errorString())});
                return;
            }
            const QJsonObject themesResp =
                QJsonDocument::fromJson(themesReply->readAll()).object();
            const bool creatable =
                !pickTheme(themesFromResponse(themesResp)).isEmpty();
            emit tested(
                {true, creatable,
                 creatable
                     ? QStringLiteral(
                           "Reachable; ProPager message will be created")
                     : QStringLiteral("Reachable, but no usable theme found")});
        });
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

        // /v1/themes returns an OBJECT {"groups":[...], "themes":[...]}, so
        // flatten it before theme selection (parsing it as an array yielded an
        // empty list and an empty theme in the create body).
        const QJsonObject themesResp =
            QJsonDocument::fromJson(themesReply->readAll()).object();
        const QJsonObject theme = pickTheme(themesFromResponse(themesResp));
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
            qInfo().noquote() << "[ProPresenter] created ProPager message"
                              << pathId(m_messageId);
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
    qInfo().noquote() << "[ProPresenter] PUT number" << number << "->"
                      << messagePath();
    QNetworkReply *reply = m_nam.put(jsonRequest(messagePath()),
                                     QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_setReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, number] {
        if (m_setReply == reply)
            m_setReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // The number never landed — do not show a stale value. Drop any
            // trigger that was waiting on this PUT.
            m_triggerPending = false;
            emit error(QStringLiteral("Failed to set number: %1")
                           .arg(reply->errorString()));
            return;
        }
        qDebug().noquote() << "[ProPresenter] number set OK:" << number;
        // The text has now landed; issue the trigger that was held back so
        // ProPresenter shows THIS number rather than the previous one.
        if (m_triggerPending) {
            m_triggerPending = false;
            sendTrigger();
        }
    });
}

void ProPresenterClient::trigger()
{
    if (m_messageId.isEmpty()) {
        emit error(QStringLiteral("Cannot trigger: ProPager message not ready"));
        return;
    }
    // If a setNumber() PUT is still in flight, hold the trigger until it lands.
    // Firing now would race the PUT on the wire and ProPresenter would show the
    // previously-set number (the live bug this guards against). The PUT's
    // finished handler issues the held trigger via sendTrigger().
    if (m_setReply) {
        qDebug().noquote()
            << "[ProPresenter] trigger deferred until pending PUT completes";
        m_triggerPending = true;
        return;
    }
    sendTrigger();
}

void ProPresenterClient::sendTrigger()
{
    // POST /v1/message/{id}/trigger (confirmed method — not GET). The optional
    // token-override body is empty; the message shows with its current tokens.
    // Success is 204 No Content: treat any non-error reply as success and do
    // not parse a body.
    qInfo().noquote() << "[ProPresenter] POST trigger ->" << messagePath();
    QNetworkReply *reply = m_nam.post(jsonRequest(messagePath() + "/trigger"),
                                      QByteArray("[]"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to trigger message: %1")
                           .arg(reply->errorString()));
            return;
        }
        qDebug().noquote() << "[ProPresenter] trigger OK";
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
    qInfo().noquote() << "[ProPresenter] GET clear ->" << messagePath();
    QNetworkReply *reply = m_nam.get(jsonRequest(messagePath() + "/clear"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("Failed to clear message: %1")
                           .arg(reply->errorString()));
            return;
        }
        qDebug().noquote() << "[ProPresenter] clear OK";
    });
}
