#ifndef PROPAGER_SLACKCLIENT_H
#define PROPAGER_SLACKCLIENT_H

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWebSocket>

class Config;
class PagerController;

// A channel the bot can see (ported from bot.py fetch_channel_list).
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

    // The inbound-message grammar (Decision 6), ported from bot.py on_message.
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
    // extractMessageEvent). Ports bot.py's on_message precedence exactly:
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
    void openConnection();          // POST apps.connections.open
    void scheduleReconnect();       // backoff + re-run openConnection
    void onTextMessageReceived(const QString &frame);

    const Config &m_config;
    PagerController *m_controller = nullptr;
    QString m_lastNumber; // most recent forwarded number, for `repeat`
    QNetworkAccessManager m_nam;
    QWebSocket m_socket;
    QTimer m_reconnectTimer;
    int m_backoffMs = 0;
};

#endif // PROPAGER_SLACKCLIENT_H
