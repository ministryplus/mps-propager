#ifndef PROPAGER_SLACKCLIENT_H
#define PROPAGER_SLACKCLIENT_H

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWebSocket>

class Config;
class PagerController;
class QNetworkReply;

// A channel the bot can see (ported from fetch_channel_list in the original
// bot.py, now in git history).
struct SlackChannel
{
    QString name;
    QString id;

    bool operator==(const SlackChannel &other) const
    {
        return name == other.name && id == other.id;
    }
};

// SlackClient is a hand-rolled Slack Socket Mode + Web API client — no SDK, no
// QJSEngine, no bolt-js, no Node sidecar (Decisions 8 & 9). Socket Mode runs
// over a QWebSocket; the Web API is called through QNetworkAccessManager.
// Everything lives on the single Qt event loop — no threads (Decision 7).
//
// It establishes the Socket Mode connection, acks and dispatches envelopes,
// surfaces raw `message` events from the configured listen channel as a
// signal, and exposes Web API helpers for emoji feedback (reactions.add) and
// channel discovery (users.conversations). It does NOT parse commands or
// extract numbers — that is Task 7. Auth uses the bot token and app-level
// token from Config only; there is no OAuth flow and no fetch_tokens/[network]
// behavior (Decision 3).
class SlackClient : public QObject
{
    Q_OBJECT

public:
    // The optional controller lets the message-grammar layer (Task 001-7)
    // enqueue pages, cancel the on-screen message, and route emoji feedback
    // from the controller's state signals back to Slack. Passing nullptr yields
    // a pure Socket-Mode/Web-API client with no paging behavior (used by the
    // envelope/response unit tests, which only call the static helpers).
    explicit SlackClient(const Config &config,
                         PagerController *controller = nullptr,
                         QObject *parent = nullptr);

    // The inbound-message grammar (Decision 6), ported from on_message in the
    // original bot.py (git history).
    enum class MessageAction {
        Ignore,        // dropped: '!'-prefix, or noise with no number/command
        Page,          // a fresh 4-digit number to forward (see `number`)
        IgnoredNumber, // a 4-digit number listed in slack/ignore-numbers (❌)
        Repeat,        // "repeat" with a prior number to re-send
        RepeatNoLast,  // "repeat" with no prior number (👎)
        Cancel         // "cancel" the on-screen message (👍)
    };
    struct ParsedCommand
    {
        MessageAction action = MessageAction::Ignore;
        QString number; // set for Page / IgnoredNumber only
    };

    // --- Pure helpers (no network; unit-tested directly) -------------------

    // Classify one listen-channel message (channel filtering already applied by
    // extractMessageEvent). Ports the original bot.py's on_message precedence exactly (git history):
    // '!'-prefix drops first; then the first 4-digit run wins (ignore-list ->
    // IgnoredNumber, else Page); then "repeat" (case-insensitive substring;
    // Repeat when hasLastNumber, else RepeatNoLast); then "cancel"; else Ignore.
    static ParsedCommand parseCommand(const QString &text,
                                      const QStringList &ignoreNumbers,
                                      bool hasLastNumber);

    // Ack payload for a Socket Mode envelope. Returns {"envelope_id": "<id>"}
    // when the envelope carries an envelope_id, else an empty object (nothing
    // to ack, e.g. a "hello" frame).
    static QJsonObject buildAck(const QJsonObject &envelope);

    // If `envelope` wraps a `message`-type event on `listenChannel`, return
    // {"text","ts","channel"}; otherwise an empty object. No command parsing.
    static QJsonObject extractMessageEvent(const QJsonObject &envelope,
                                           const QString &listenChannel);

    // Extract the wss:// URL from an apps.connections.open response
    // ({"ok":true,"url":"wss://..."}); empty string when ok is false/missing.
    static QString wssUrlFromConnectionsOpen(const QJsonObject &response);

    // Bot's non-archived channels from a users.conversations response,
    // filtered to entries where is_channel is true (ported fetch_channel_list).
    static QVector<SlackChannel>
    channelsFromConversations(const QJsonObject &response);

    // Exponential reconnect backoff in ms: 0 -> base, else doubled up to a cap.
    static int nextBackoffMs(int currentMs);

public slots:
    // Begin the handshake: POST apps.connections.open, then open the socket.
    void start();
    // Manual "kick it now" (Task 002-3): cancel any pending backoff, reset it
    // to zero, and re-run openConnection() immediately — skipping the
    // exponential wait scheduleReconnect() accumulates. Safe to call while
    // already connected: the existing socket (and any pending handshake reply)
    // is torn down first, so exactly one QWebSocket remains and no reply leaks.
    // Reads m_config (tokens, channel) live. The automatic-backoff path is
    // unchanged; this is purely an additional entry point.
    void reconnectNow();
    // POST reactions.add for the feedback layer (bot token).
    void addReaction(const QString &emoji, const QString &channel,
                     const QString &ts);
    // GET users.conversations and emit channelsListed().
    void listChannels();

signals:
    void connected();
    void disconnected();
    void error(const QString &message);
    void messageReceived(const QString &text, const QString &ts,
                         const QString &channel);
    void channelsListed(const QVector<SlackChannel> &channels);

private slots:
    // Applies parseCommand to an inbound listen-channel message and performs the
    // side effects: enqueue/cancel via the controller, and immediate reactions
    // (❌/👎/👍). Deferred feedback (⌛/📞/👍-on-clear) is driven by controller
    // signals wired in the constructor. Connected to messageReceived.
    void handleMessage(const QString &text, const QString &ts,
                       const QString &channel);

private:
    // Hard (re)connect: POST apps.connections.open, then open a socket as the
    // new active connection. Used by start(), the backoff timer, reconnectNow().
    void openConnection();
    // Graceful "make-before-break" refresh (Slack `disconnect` frame): open a
    // *second* socket alongside the live one; the old one is retired only once
    // the replacement is confirmed live (its `hello`), so no inbound message is
    // dropped in a reconnect gap. This is why we roll two sockets, not one.
    void beginGracefulReconnect();
    // Shared handshake: POST apps.connections.open and, on success, open a
    // socket into m_socket (graceful=false) or m_pending (graceful=true).
    void requestConnection(bool graceful);
    void scheduleReconnect();       // backoff + re-run openConnection

    // Allocate a socket parented to this and wire its lifecycle signals through
    // the per-socket handlers (the handlers branch on which socket fired).
    QWebSocket *makeSocket();
    // Close and delete a socket after detaching its signals from us, so its
    // shutdown does NOT reach onSocketDisconnected (no reconnect, no status
    // flap). Used when we intentionally drop a socket (promotion, reconnectNow).
    void retireSocket(QWebSocket *sock);
    // Adopt m_pending as the active connection and retire the previous one.
    void promotePending();

    // Per-socket lifecycle (a socket pointer identifies active vs pending).
    void onSocketConnected(QWebSocket *sock);
    void onSocketDisconnected(QWebSocket *sock);
    void onSocketFrame(QWebSocket *sock, const QString &frame);

    const Config &m_config;
    PagerController *m_controller = nullptr;
    QString m_lastNumber; // most recent forwarded number, for `repeat`
    QNetworkAccessManager m_nam;
    // The live connection, and (only during a graceful refresh) the incoming
    // replacement awaiting its `hello`. Both nullptr means no socket. Parented
    // to this, so they are cleaned up with the client.
    QWebSocket *m_socket = nullptr;
    QWebSocket *m_pending = nullptr;
    QTimer m_reconnectTimer;
    int m_backoffMs = 0;
    // In-flight apps.connections.open reply, tracked so reconnectNow() (and a
    // mid-handshake socket death) can abort a pending handshake instead of
    // leaking it or letting it open a competing socket.
    QPointer<QNetworkReply> m_openReply;
};

#endif // PROPAGER_SLACKCLIENT_H
