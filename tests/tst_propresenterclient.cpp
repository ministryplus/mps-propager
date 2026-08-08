#include <QtTest>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include "config.h"
#include "propresenterclient.h"

// Unit tests for ProPresenterClient's pure request/response logic (Task 001-3).
// The network methods require a live ProPresenter and are verified manually
// (see the task's Verification section); here we lock down the JSON building
// and parsing that those network calls depend on.
class TestProPresenterClient : public QObject
{
    Q_OBJECT

private:
    // A themes-response entry: one theme carrying the given slide id-names.
    static QJsonObject themeWithSlides(const QStringList &slideNames)
    {
        QJsonArray slides;
        for (int i = 0; i < slideNames.size(); ++i) {
            QJsonObject id{{"name", slideNames.at(i)},
                           {"uuid", QStringLiteral("uuid-%1").arg(i)},
                           {"index", i}};
            slides.append(QJsonObject{{"id", id}});
        }
        return QJsonObject{{"slides", slides}};
    }

private slots:
    // apiBase builds the documented http URL from host/port.
    void apiBase_buildsHttpUrl()
    {
        QCOMPARE(ProPresenterClient::apiBase("127.0.0.1", 55184),
                 QString("http://127.0.0.1:55184"));
        QCOMPARE(ProPresenterClient::apiBase("10.0.0.5", 1234),
                 QString("http://10.0.0.5:1234"));
    }

    // buildMessageBody emits exactly the Decision-4 shape.
    void buildMessageBody_matchesDecision4Shape()
    {
        const QJsonObject theme{{"name", ""}, {"uuid", ""}, {"index", 0}};
        const QJsonObject body =
            ProPresenterClient::buildMessageBody("ProPager", "142", theme);

        const QJsonObject id = body.value("id").toObject();
        QCOMPARE(id.value("name").toString(), QString("ProPager"));
        QCOMPARE(id.value("uuid").toString(), QString(""));
        QCOMPARE(id.value("index").toInt(), 0);

        QCOMPARE(body.value("message").toString(), QString("VK: {Number}"));

        const QJsonArray tokens = body.value("tokens").toArray();
        QCOMPARE(tokens.size(), 1);
        const QJsonObject tok = tokens.at(0).toObject();
        QCOMPARE(tok.value("name").toString(), QString("Number"));
        QCOMPARE(tok.value("text").toObject().value("text").toString(),
                 QString("142"));

        QCOMPARE(body.value("theme").toObject(), theme);
        QCOMPARE(body.value("visible_on_network").toBool(), true);

        // ProPresenter 21.x requires a top-level is_active flag; omitting it
        // makes POST/PUT /v1/messages reject the body with HTTP 400
        // ("missing field `is_active`"). The message is not shown until
        // trigger(), so it is created inactive.
        QVERIFY2(body.contains("is_active"), "body must carry is_active");
        QCOMPARE(body.value("is_active").toBool(), false);

        // No legacy hardcoded UUID leaks in (Decision 5 / Task 001-3).
        QVERIFY(!QString::fromUtf8(QJsonDocument(body).toJson())
                     .contains("942C3FC3"));
    }

    // GET /v1/themes returns an OBJECT {"groups":[...], "themes":[...]}, not an
    // array; themesFromResponse flattens the top-level themes plus every
    // group's themes into one array of theme objects for pickTheme.
    void themesFromResponse_flattensTopLevelAndGroups()
    {
        const QJsonObject response{
            {"themes", QJsonArray{themeWithSlides({"Plain"})}},
            {"groups", QJsonArray{QJsonObject{
                          {"themes", QJsonArray{themeWithSlides({"VK Number"})}}}}}};

        const QJsonArray flat = ProPresenterClient::themesFromResponse(response);
        QCOMPARE(flat.size(), 2);
        // pickTheme over the flattened array still finds the vk slide that lives
        // inside a group (previously unreachable when parsed as a bare array).
        QCOMPARE(ProPresenterClient::pickTheme(flat).value("name").toString(),
                 QString("VK Number"));
    }

    // A response missing both keys flattens to an empty array (no crash).
    void themesFromResponse_emptyWhenNeitherPresent()
    {
        QVERIFY(ProPresenterClient::themesFromResponse(QJsonObject()).isEmpty());
    }

    // pickTheme returns the id-object of a slide whose name contains "vk".
    void pickTheme_findsVkSlide()
    {
        QJsonArray themes;
        themes.append(themeWithSlides({"Lower Third", "VK Number", "Bumper"}));

        const QJsonObject id = ProPresenterClient::pickTheme(themes);
        QCOMPARE(id.value("name").toString(), QString("VK Number"));
    }

    // The "vk" match is case-insensitive, matching bot.py's .lower() check.
    void pickTheme_matchIsCaseInsensitive()
    {
        QJsonArray themes;
        themes.append(themeWithSlides({"intro", "village kids VK", "outro"}));
        const QJsonObject id = ProPresenterClient::pickTheme(themes);
        QCOMPARE(id.value("name").toString(), QString("village kids VK"));
    }

    // With no vk slide, fall back to the first theme's first slide.
    void pickTheme_fallsBackToFirstSlide()
    {
        QJsonArray themes;
        themes.append(themeWithSlides({"Alpha", "Beta"}));
        themes.append(themeWithSlides({"Gamma"}));
        const QJsonObject id = ProPresenterClient::pickTheme(themes);
        QCOMPARE(id.value("name").toString(), QString("Alpha"));
    }

    // An empty themes list yields an empty object (no crash).
    void pickTheme_emptyThemesYieldsEmptyObject()
    {
        QVERIFY(ProPresenterClient::pickTheme(QJsonArray()).isEmpty());
    }

    // findMessageId locates the message named "ProPager" and returns its id.
    void findMessageId_findsByName()
    {
        QJsonArray messages;
        messages.append(QJsonObject{
            {"id", QJsonObject{{"name", "Announcements"},
                               {"uuid", "aaaa"},
                               {"index", 0}}}});
        messages.append(QJsonObject{
            {"id", QJsonObject{{"name", "ProPager"},
                               {"uuid", "bbbb"},
                               {"index", 1}}}});

        const QJsonObject id =
            ProPresenterClient::findMessageId(messages, "ProPager");
        QCOMPARE(id.value("name").toString(), QString("ProPager"));
        QCOMPARE(id.value("uuid").toString(), QString("bbbb"));
        QCOMPARE(id.value("index").toInt(), 1);
    }

    // A missing message yields an empty object (caller then creates it).
    void findMessageId_absentYieldsEmptyObject()
    {
        QJsonArray messages;
        messages.append(QJsonObject{
            {"id", QJsonObject{{"name", "Other"}, {"uuid", "x"}, {"index", 0}}}});
        QVERIFY(
            ProPresenterClient::findMessageId(messages, "ProPager").isEmpty());
    }

    // pathId prefers the uuid; when empty it falls back to the name.
    void pathId_prefersUuidThenName()
    {
        QCOMPARE(ProPresenterClient::pathId(
                     QJsonObject{{"uuid", "abc-123"}, {"name", "ProPager"}}),
                 QString("abc-123"));
        QCOMPARE(ProPresenterClient::pathId(
                     QJsonObject{{"uuid", ""}, {"name", "ProPager"}}),
                 QString("ProPager"));
    }

    // --- Task 002-3: reconnect() + test() (offline-safe paths) ------------
    //
    // The reachable/connected paths need a live ProPresenter and are verified
    // manually (see the task). These two exercise the deterministic
    // unreachable path against a refused local port: no live service, no flake.

    // Point a Config's ProPresenter at a dead local port so the REST GET is
    // refused immediately (connection refused), not hung. Config owns a
    // unique_ptr (non-copyable), so configure it in place by reference.
    static void configureDeadEndpoint(Config &config)
    {
        config.load();
        config.setPropresenterPort(65500); // nothing listens here (host: default)
        config.reload();
    }

    // test() against an unreachable endpoint reports reachable=false with a
    // useful detail string, and drives no set/trigger/clear.
    void test_reportsUnreachableForDeadEndpoint()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Config config(dir.filePath("ProPager.ini"));
        configureDeadEndpoint(config);
        ProPresenterClient client(config);

        ProPresenterClient::TestResult captured;
        bool got = false;
        connect(&client, &ProPresenterClient::tested, &client,
                [&](const ProPresenterClient::TestResult &r) {
                    captured = r;
                    got = true;
                });

        client.test();

        QTRY_VERIFY_WITH_TIMEOUT(got, 3000);
        QVERIFY(!captured.reachable);
        QVERIFY(!captured.messageReady);
        QVERIFY2(!captured.detail.isEmpty(),
                 "unreachable result must carry a diagnostic detail");
    }

    // reconnect() is safe to call repeatedly while a prior ensure is still in
    // flight: the first reply is aborted (not leaked, no double-resolve) and
    // the app does not crash. Against a dead port the surviving attempt fails
    // and emits disconnected exactly once.
    void reconnect_safeWhenCalledRepeatedly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Config config(dir.filePath("ProPager.ini"));
        configureDeadEndpoint(config);
        ProPresenterClient client(config);

        int disconnects = 0;
        connect(&client, &ProPresenterClient::disconnected, &client,
                [&] { ++disconnects; });

        client.reconnect();
        client.reconnect(); // immediate re-entry: must abort the first cleanly

        QTRY_VERIFY_WITH_TIMEOUT(disconnects >= 1, 3000);
        // The aborted first attempt must not itself report a disconnect.
        QCOMPARE(disconnects, 1);
    }

    // Regression (live bug): a page briefly displayed stale content (the previous
    // number, or a token UUID) before the correct value appeared. Root cause: the
    // display path issued a per-page PUT *and* a trigger — the PUT re-renders the
    // message definition, flashing unresolved content, before the trigger shows
    // it. ProPresenter's documented model carries the value IN the trigger, so
    // the number must travel as a token override on POST /v1/message/{id}/trigger
    // (body shape [{name, text:{text}}], confirmed against the OpenAPI spec) and
    // NO per-page PUT should be issued.
    void trigger_carriesNumberToken_andIssuesNoPerPagePut()
    {
        QStringList methods;    // request lines the server saw (per page action)
        QByteArray triggerBody; // captured POST .../trigger request body

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const quint16 port = server.serverPort();

        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *sock = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(sock, &QObject::destroyed, [buf] { delete buf; });
                connect(sock, &QTcpSocket::disconnected, sock,
                        &QObject::deleteLater);
                connect(sock, &QTcpSocket::readyRead, sock,
                        [&methods, &triggerBody, sock, buf] {
                            buf->append(sock->readAll());
                            const int headerEnd = buf->indexOf("\r\n\r\n");
                            if (headerEnd < 0)
                                return; // await the full header block
                            // Body-aware: hold until the whole body has arrived.
                            int contentLength = 0;
                            for (const QByteArray &h :
                                 buf->left(headerEnd).split('\n')) {
                                const QByteArray line = h.trimmed().toLower();
                                if (line.startsWith("content-length:"))
                                    contentLength =
                                        line.mid(15).trimmed().toInt();
                            }
                            if (buf->size() < headerEnd + 4 + contentLength)
                                return;
                            const QByteArray reqLine =
                                buf->left(buf->indexOf("\r\n"));
                            const QByteArray body =
                                buf->mid(headerEnd + 4, contentLength);
                            buf->clear();
                            const auto send204 = [sock] {
                                sock->write("HTTP/1.1 204 No Content\r\n"
                                            "Content-Length: 0\r\n\r\n");
                                sock->flush();
                            };
                            if (reqLine.startsWith("GET")
                                && reqLine.contains("/v1/messages")) {
                                const QByteArray msg =
                                    "[{\"id\":{\"name\":\"ProPager\",\"uuid\":"
                                    "\"msg1\",\"index\":0}}]";
                                sock->write("HTTP/1.1 200 OK\r\n"
                                            "Content-Type: application/json\r\n"
                                            "Content-Length: "
                                            + QByteArray::number(msg.size())
                                            + "\r\n\r\n" + msg);
                                sock->flush();
                            } else if (reqLine.contains("/trigger")) {
                                methods << QStringLiteral("TRIGGER");
                                triggerBody = body;
                                send204();
                            } else if (reqLine.startsWith("PUT")) {
                                methods << QStringLiteral("PUT");
                                send204();
                            } else {
                                send204(); // startup clear
                            }
                        });
            }
        });

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Config config(dir.filePath("ProPager.ini"));
        config.load();
        config.setPropresenterPort(port); // host defaults to 127.0.0.1
        config.reload();
        ProPresenterClient client(config);

        bool connected = false;
        connect(&client, &ProPresenterClient::connected, &client,
                [&] { connected = true; });
        client.ensureMessage();
        QTRY_VERIFY_WITH_TIMEOUT(connected, 3000); // message id resolved

        client.setNumber(QStringLiteral("1234"));
        client.trigger();

        QTRY_VERIFY_WITH_TIMEOUT(!triggerBody.isEmpty(), 3000);

        // The trigger carries the number as a token override, correct shape.
        const QJsonArray tokens = QJsonDocument::fromJson(triggerBody).array();
        QVERIFY2(!tokens.isEmpty(),
                 "trigger body must carry the token override array");
        const QJsonObject token = tokens.first().toObject();
        QCOMPARE(token.value("name").toString(), QString("Number"));
        QCOMPARE(token.value("text").toObject().value("text").toString(),
                 QString("1234"));

        // No per-page PUT: the value travels in the trigger, so a page never
        // re-renders the definition (the flash's root cause).
        QVERIFY2(!methods.contains(QStringLiteral("PUT")),
                 "the per-page display path must not issue a PUT");
    }
};

QTEST_GUILESS_MAIN(TestProPresenterClient)
#include "tst_propresenterclient.moc"
