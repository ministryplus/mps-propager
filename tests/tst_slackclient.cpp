#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "slackclient.h"

// Unit tests for SlackClient's pure envelope/response logic (Task 001-5). The
// live Socket Mode + Web API round-trips require real Slack tokens and are
// verified manually (see the task's Verification section); here we lock down
// the JSON handling those network calls depend on.
class TestSlackClient : public QObject
{
    Q_OBJECT

private:
    // A Socket Mode events_api envelope wrapping a single event object.
    static QJsonObject envelope(const QString &envelopeId, const QJsonObject &event)
    {
        QJsonObject env{{"type", "events_api"},
                        {"payload", QJsonObject{{"event", event}}}};
        if (!envelopeId.isNull()) {
            env.insert("envelope_id", envelopeId);
        }
        return env;
    }

private slots:
    // An envelope carrying an envelope_id acks with exactly that id.
    void buildAck_echoesEnvelopeId()
    {
        const QJsonObject env = envelope("e-123", QJsonObject{{"type", "message"}});
        const QJsonObject ack = SlackClient::buildAck(env);
        QCOMPARE(ack.value("envelope_id").toString(), QString("e-123"));
        QCOMPARE(ack.keys().size(), 1);
    }

    // A frame with no envelope_id (e.g. "hello") produces nothing to ack.
    void buildAck_withoutEnvelopeIdIsEmpty()
    {
        QVERIFY(SlackClient::buildAck(QJsonObject{{"type", "hello"}}).isEmpty());
    }

    // A message event on the listen channel surfaces raw text/ts/channel.
    void extractMessageEvent_matchOnListenChannel()
    {
        const QJsonObject event{{"type", "message"},
                                {"text", "kid 1234 needs pickup"},
                                {"ts", "1699999999.000100"},
                                {"channel", "C06LISTEN"}};
        const QJsonObject out =
            SlackClient::extractMessageEvent(envelope("e1", event), "C06LISTEN");

        QCOMPARE(out.value("text").toString(), QString("kid 1234 needs pickup"));
        QCOMPARE(out.value("ts").toString(), QString("1699999999.000100"));
        QCOMPARE(out.value("channel").toString(), QString("C06LISTEN"));
    }

    // A message on a different channel is ignored.
    void extractMessageEvent_ignoresOtherChannel()
    {
        const QJsonObject event{{"type", "message"},
                                {"text", "hi"},
                                {"ts", "1.2"},
                                {"channel", "C_OTHER"}};
        QVERIFY(SlackClient::extractMessageEvent(envelope("e1", event), "C06LISTEN")
                    .isEmpty());
    }

    // A non-message event on the listen channel is ignored.
    void extractMessageEvent_ignoresNonMessageType()
    {
        const QJsonObject event{{"type", "reaction_added"},
                                {"channel", "C06LISTEN"}};
        QVERIFY(SlackClient::extractMessageEvent(envelope("e1", event), "C06LISTEN")
                    .isEmpty());
    }

    // apps.connections.open ok:true yields the wss URL.
    void wssUrl_okTrueReturnsUrl()
    {
        const QJsonObject resp{{"ok", true},
                               {"url", "wss://wss-primary.slack.com/link/?ticket=x"}};
        QCOMPARE(SlackClient::wssUrlFromConnectionsOpen(resp),
                 QString("wss://wss-primary.slack.com/link/?ticket=x"));
    }

    // ok:false yields no URL (caller treats as an error / retry).
    void wssUrl_okFalseReturnsEmpty()
    {
        const QJsonObject resp{{"ok", false}, {"error", "invalid_auth"}};
        QVERIFY(SlackClient::wssUrlFromConnectionsOpen(resp).isEmpty());
    }

    // users.conversations is filtered to true channels (ported fetch_channel_list).
    void channels_filtersToChannels()
    {
        QJsonArray chans;
        chans.append(QJsonObject{
            {"name", "general"}, {"id", "C1"}, {"is_channel", true}});
        chans.append(QJsonObject{
            {"name", "dm"}, {"id", "D1"}, {"is_channel", false}});
        chans.append(QJsonObject{
            {"name", "pagers"}, {"id", "C2"}, {"is_channel", true}});

        const QVector<SlackChannel> out = SlackClient::channelsFromConversations(
            QJsonObject{{"ok", true}, {"channels", chans}});

        const QVector<SlackChannel> expected{{"general", "C1"}, {"pagers", "C2"}};
        QCOMPARE(out, expected);
    }

    // Backoff grows exponentially from a base and is capped.
    void backoff_exponentialWithCap()
    {
        const int base = SlackClient::nextBackoffMs(0);
        QVERIFY2(base > 0, "first backoff must be positive");
        QCOMPARE(SlackClient::nextBackoffMs(base), base * 2);
        QCOMPARE(SlackClient::nextBackoffMs(base * 2), base * 4);

        // A very large current value saturates at the cap, never overflowing.
        const int capped = SlackClient::nextBackoffMs(1000 * 1000);
        QVERIFY2(capped <= 60000 && capped >= base,
                 "backoff must saturate at a sane cap");
        QCOMPARE(SlackClient::nextBackoffMs(capped), capped);
    }
};

QTEST_GUILESS_MAIN(TestSlackClient)
#include "tst_slackclient.moc"
