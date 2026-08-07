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

    // --- parseCommand (pure, ports bot.py on_message grammar, Task 001-7) ---
    //
    // The Verification table in the task maps directly onto these cases. The
    // channel filter and ack happen upstream (extractMessageEvent); parseCommand
    // only sees text already known to be on the listen channel.

    // A 4-digit run is extracted and forwarded as a page.
    void parseCommand_fourDigitIsPage()
    {
        const auto cmd = SlackClient::parseCommand("kid 1234 needs pickup", {}, false);
        QCOMPARE(cmd.action, SlackClient::MessageAction::Page);
        QCOMPARE(cmd.number, QString("1234"));
    }

    // Only the FIRST 4-digit run is taken (ports re.search first match).
    void parseCommand_takesFirstFourDigitRun()
    {
        const auto cmd = SlackClient::parseCommand("a 1234 then 5678", {}, false);
        QCOMPARE(cmd.action, SlackClient::MessageAction::Page);
        QCOMPARE(cmd.number, QString("1234"));
    }

    // A longer digit run still yields exactly the first four digits.
    void parseCommand_fourDigitsFromLongerRun()
    {
        const auto cmd = SlackClient::parseCommand("12345", {}, false);
        QCOMPARE(cmd.action, SlackClient::MessageAction::Page);
        QCOMPARE(cmd.number, QString("1234"));
    }

    // A number present in ignore-numbers is flagged (❌), not forwarded.
    void parseCommand_ignoredNumber()
    {
        const auto cmd =
            SlackClient::parseCommand("5555", QStringList{"5555"}, false);
        QCOMPARE(cmd.action, SlackClient::MessageAction::IgnoredNumber);
        QCOMPARE(cmd.number, QString("5555"));
    }

    // A leading '!' drops the whole message even when it contains a number.
    void parseCommand_bangPrefixDroppedDespiteNumber()
    {
        const auto cmd = SlackClient::parseCommand("!note 1234", {}, false);
        QCOMPARE(cmd.action, SlackClient::MessageAction::Ignore);
    }

    // 'repeat' with a prior number re-sends it; without one, thumbsdown.
    void parseCommand_repeatWithAndWithoutLast()
    {
        QCOMPARE(SlackClient::parseCommand("repeat", {}, true).action,
                 SlackClient::MessageAction::Repeat);
        QCOMPARE(SlackClient::parseCommand("please REPEAT", {}, false).action,
                 SlackClient::MessageAction::RepeatNoLast);
    }

    // 'cancel' is a case-insensitive substring command.
    void parseCommand_cancelCaseInsensitive()
    {
        QCOMPARE(SlackClient::parseCommand("Cancel that", {}, false).action,
                 SlackClient::MessageAction::Cancel);
    }

    // A 4-digit match wins over repeat/cancel substrings (precedence).
    void parseCommand_numberBeatsRepeatAndCancel()
    {
        const auto cmd = SlackClient::parseCommand("repeat 4321", {}, true);
        QCOMPARE(cmd.action, SlackClient::MessageAction::Page);
        QCOMPARE(cmd.number, QString("4321"));
    }

    // Plain chatter with no number and no command is ignored entirely.
    void parseCommand_plainChatterIgnored()
    {
        QCOMPARE(SlackClient::parseCommand("hello team", {}, true).action,
                 SlackClient::MessageAction::Ignore);
    }
};

QTEST_GUILESS_MAIN(TestSlackClient)
#include "tst_slackclient.moc"
