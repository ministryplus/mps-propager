#ifndef PROPAGER_PROPRESENTERCLIENT_H
#define PROPAGER_PROPRESENTERCLIENT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

class Config;
class QNetworkReply;

// ProPresenterClient owns ProPager's ProPresenter message lifecycle over the
// documented REST /v1 API (Spec 001, Decisions 4 & 5). It uses
// QNetworkAccessManager exclusively — the legacy /remote protocol-701
// WebSocket path, password auth, and "vk"-title/${token} auto-detection are
// deleted, not ported.
//
// On startup the client ensures a Message named "ProPager" exists (find or
// create), stores its returned id object, and drives that single message
// through set -> trigger -> clear, always scoped to the stored id.
//
// Endpoints (confirmed against the ProPresenter OpenAPI spec at
// openapi.propresenter.com — closes Open TODO 1):
//   PUT  /v1/message/{id}          set the token text (Decision 4 body)
//   POST /v1/message/{id}/trigger  show the message (204 No Content on success)
//   GET  /v1/message/{id}/clear    hide ONLY this message (per-message, 204)
// The layer-wide GET /v1/clear/layer/messages is deliberately NOT used
// (Decision 5) so ProPager coexists with a tech's other messages.
class ProPresenterClient : public QObject
{
    Q_OBJECT

public:
    explicit ProPresenterClient(const Config &config, QObject *parent = nullptr);

    // --- Pure helpers (no network; unit-tested directly) -------------------

    // "http://{host}:{port}" base URL for the REST API.
    static QString apiBase(const QString &host, int port);

    // Decision-4 message body for the message named `name`, showing `number`,
    // with the given `theme` id-object embedded. Used for both PUT (theme left
    // empty) and POST /v1/messages create (theme from pickTheme).
    static QJsonObject buildMessageBody(const QString &name,
                                        const QString &number,
                                        const QJsonObject &theme);

    // Flatten a GET /v1/themes response into one array of theme objects (each
    // carrying a "slides" array) for pickTheme. The endpoint returns an OBJECT
    // {"groups":[{... "themes":[...]}], "themes":[...]}, not a bare array — so
    // parsing the reply as an array (as the first cut did) silently yielded an
    // empty list. This gathers the top-level `themes` plus every group's
    // `themes`.
    static QJsonArray themesFromResponse(const QJsonObject &response);

    // Port of bot.py propres_create_message theme-lookup: return the id-object
    // of a slide whose id.name contains "vk" (case-insensitive); if none
    // matches, fall back to the first theme's first slide (themes[0].slides[0]
    // .id). Returns an empty object if `themes` is empty/malformed.
    static QJsonObject pickTheme(const QJsonArray &themes);

    // Find the id-object of the message named `name` in a GET /v1/messages
    // response array; returns an empty object if not present.
    static QJsonObject findMessageId(const QJsonArray &messages,
                                     const QString &name);

    // Path identifier for {id}: prefer the uuid, fall back to the name.
    static QString pathId(const QJsonObject &idObject);

    // Outcome of test() (Task 002-3, Decision 10). Reachability + adopt/create
    // readiness only — NOT the deep #2 preflight. The struct is the seam #2's
    // preflight extends: add slide-label / message-shape fields here later
    // without reshaping the tested() signal or its call sites.
    struct TestResult
    {
        bool reachable = false;    // the REST API answered at apiBase()
        bool messageReady = false; // the ProPager message can be adopted/created
        QString detail;            // human-readable outcome for the tab/log
        // --- #2 extension seam (slide-label / message-shape checks) --------
    };

public slots:
    // The four action slots are virtual so PagerController (Task 001-4) can be
    // unit-driven against a recording fake client without a live ProPresenter.
    // Overriding them changes no production behavior or public shape.

    // Find or create the "ProPager" message, store its id, then clear it so the
    // app launches from a known-empty state (startup recovery, Decision 5).
    virtual void ensureMessage();
    // PUT the number onto the stored message (does not show it).
    virtual void setNumber(const QString &number);
    // POST trigger: show the stored message.
    virtual void trigger();
    // GET clear: hide only the stored message.
    virtual void clear();

    // Re-run the connect/ensure path from current Config (Task 002-3): discard
    // any stale resolved id (host may have changed) and re-resolve via
    // ensureMessage(). Safe to call while already connected — a prior in-flight
    // ensure is aborted, so no reply leaks and m_messageId is not
    // double-resolved. FOOTGUN (Decision 5): ensureMessage() issues the startup
    // clear on ProPager's own message, so 002-7 gates PP reconnect on dirty
    // connection fields. This method only provides the re-runnable entry point.
    void reconnect();

    // Reachability check (Task 002-3, Decision 10): confirm the REST API is
    // reachable and the ProPager message can be adopted or created, reporting
    // via tested(TestResult). Drives NO set/trigger/clear — reachability only.
    void test();

signals:
    void connected();
    void disconnected();
    void error(const QString &message);
    void tested(const TestResult &result);

private:
    QString apiBase() const;
    QString messagePath() const; // "http://.../v1/message/{id}"
    void createMessage();        // POST /v1/messages after ensure fails to find
    void sendTrigger();          // POST /v1/message/{id}/trigger (the wire call)

    const Config &m_config;
    QNetworkAccessManager m_nam;
    QJsonObject m_messageId; // id-object of the ProPager message, once resolved
    // In-flight GET /v1/messages from ensureMessage(), tracked so a reconnect()
    // re-entry can abort it (no leaked reply, no double-resolve). Cleared when
    // the reply finishes or is aborted.
    QPointer<QNetworkReply> m_ensureReply;
    // In-flight PUT from setNumber(), tracked so trigger() can defer until the
    // number text has actually landed. The PUT and the trigger POST otherwise
    // race on the wire and ProPresenter shows the PREVIOUS number. Cleared when
    // the PUT finishes.
    QPointer<QNetworkReply> m_setReply;
    // A trigger() arrived while a setNumber() PUT was still in flight; the POST
    // is held until that PUT completes, then issued from its finished handler.
    bool m_triggerPending = false;
};

#endif // PROPAGER_PROPRESENTERCLIENT_H
