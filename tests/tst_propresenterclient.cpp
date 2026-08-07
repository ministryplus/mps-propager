#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

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
};

QTEST_GUILESS_MAIN(TestProPresenterClient)
#include "tst_propresenterclient.moc"
