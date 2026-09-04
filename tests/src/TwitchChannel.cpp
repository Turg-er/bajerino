// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchChannel.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "messages/Message.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "providers/twitch/ChannelPointReward.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "Test.hpp"
#include "TwitchChannelTestAccess.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtCore/qtestsupport_core.h>

#include <vector>

namespace chatterino::detail {

TEST(TwitchChannelDetail_isUnknownCommand, good)
{
    // clang-format off
    std::vector<QString> cases{
        "/me hello",
        ".me hello",
        "/ hello",
        ". hello",
        "/ /hello",
        ". .hello",
        "/ .hello",
        ". /hello",
        ".", // this results in an empty message but not in an error (twitchdev/issues#1019)
        "/me",
        ".me",
        "..",
        "...",
        "....",
        "",
        "foo",
        "a",
        "!",
        ". .",
        ". ..",
        ".. ..",
        ".. .",
        "/ /",
        "/ .",
        ". /",
        ". ./",
        ".. /",
        ".. me",
        ". me",
    };
    // clang-format on

    for (const auto &input : cases)
    {
        ASSERT_FALSE(isUnknownCommand(input))
            << input << " should not be considered an unknown command";
    }
}

TEST(TwitchChannelDetail_isUnknownCommand, bad)
{
    // clang-format off
    std::vector<QString> cases{
        "/badcommand",
        ".badcommand",
        "/badcommand hello",
        ".badcommand hello",
        "/@badcommand hello",
        ".@badcommand hello",
        "/bann username ban reason",
        "/bann username",
        "//",
        "./",
        "./me",
        "./w",
        "/.",
        "/.me",
        "/.w",
        "/,me",
    };
    // clang-format on

    for (const auto &input : cases)
    {
        ASSERT_TRUE(isUnknownCommand(input))
            << input << " should be considered an unknown command";
    }
}

}  // namespace chatterino::detail

namespace chatterino {

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : highlights(this->settings, &this->accounts)
        , pubSub("wss://" + PUBSUB_WSS_ADDR)
    {
    }

    bool isTest() const override
    {
        return this->testMode;
    }

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    HighlightController *getHighlights() override
    {
        return &this->highlights;
    }

    PubSub *getTwitchPubSub() override
    {
        return &this->pubSub;
    }

    mock::EmptyLogging logging;
    mock::MockTwitchIrcServer twitch;
    AccountController accounts;
    HighlightController highlights;
    PubSub pubSub;
    bool testMode = true;
};

QJsonObject makeChannelPointRedemption(const QString &redemptionId,
                                       QString cursor = "cursor-1")
{
    QJsonObject redemption{
        {"channel_id", "11148817"},
        {"cursor", cursor},
        {"redeemed_at", "2026-05-28T12:00:00Z"},
        {"user",
         QJsonObject{
             {"id", "129546453"},
             {"login", "nerixyz"},
             {"display_name", "nerixyz"},
         }},
        {"reward",
         QJsonObject{
             {"channel_id", "11148817"},
             {"cost", 1},
             {"id", "31a2344e-0fce-4229-9453-fb2e8b6dd02c"},
             {"is_user_input_required", false},
             {"title", "my reward"},
         }},
    };

    if (!redemptionId.isEmpty())
    {
        redemption.insert("id", redemptionId);
    }

    return redemption;
}

QJsonObject makePinnedChatUnpinPayload(const QString &pinId)
{
    return QJsonObject{
        {"type", "unpin-message"},
        {"data",
         QJsonObject{
             {"id", pinId},
             {"unpinned_by",
              QJsonObject{
                  {"display_name", "Moderator"},
                  {"login", "moderator"},
              }},
         }},
    };
}

TEST(TwitchChannel, ResolvesChannelModes)
{
    MockApplication app;

    TwitchChannel authenticated("authenticated",
                                TwitchChannelMode::Authenticated);
    EXPECT_EQ(authenticated.effectiveMode(), TwitchChannelMode::Authenticated);
    EXPECT_FALSE(authenticated.usesAnonymousReadConnection());
    EXPECT_TRUE(authenticated.usesAuthenticatedFeatures());
    EXPECT_FALSE(authenticated.isBajerinoAnonymous());

    TwitchChannel anonymousRead("anonymous-read",
                                TwitchChannelMode::AnonymousRead);
    EXPECT_EQ(anonymousRead.effectiveMode(), TwitchChannelMode::AnonymousRead);
    EXPECT_TRUE(anonymousRead.usesAnonymousReadConnection());
    EXPECT_TRUE(anonymousRead.usesAuthenticatedFeatures());
    EXPECT_FALSE(anonymousRead.isBajerinoAnonymous());

    TwitchChannel bajerinoAnonymous("bajerino-anonymous",
                                    TwitchChannelMode::BajerinoAnonymous);
    EXPECT_EQ(bajerinoAnonymous.effectiveMode(),
              TwitchChannelMode::BajerinoAnonymous);
    EXPECT_TRUE(bajerinoAnonymous.usesAnonymousReadConnection());
    EXPECT_FALSE(bajerinoAnonymous.usesAuthenticatedFeatures());
    EXPECT_TRUE(bajerinoAnonymous.isBajerinoAnonymous());
}

TEST(TwitchChannel, FollowDefaultTracksGlobalMode)
{
    MockApplication app;
    app.settings.twitchDefaultChannelMode.setValue("authenticated");
    TwitchChannel channel("pajlada");

    EXPECT_EQ(channel.modeOverride(), std::nullopt);
    EXPECT_EQ(channel.effectiveMode(), TwitchChannelMode::Authenticated);

    app.settings.twitchDefaultChannelMode.setValue("anonymousread");
    EXPECT_EQ(channel.effectiveMode(), TwitchChannelMode::AnonymousRead);

    channel.setModeOverride(TwitchChannelMode::BajerinoAnonymous);
    app.settings.twitchDefaultChannelMode.setValue("authenticated");
    EXPECT_EQ(channel.effectiveMode(), TwitchChannelMode::BajerinoAnonymous);

    channel.setModeOverride(std::nullopt);
    EXPECT_EQ(channel.effectiveMode(), TwitchChannelMode::Authenticated);
}

TEST(TwitchChannel, ChannelPointsDefaultToUnknown)
{
    MockApplication app;
    TwitchChannel channel("pajlada");

    EXPECT_EQ(channel.channelPointBalance(), -1);
}

TEST(TwitchChannel, ChannelPointsSignalOnlyOnBalanceChange)
{
    MockApplication app;
    TwitchChannel channel("pajlada");
    int signalCount = 0;
    auto connection = channel.channelPointsChanged.connect([&signalCount] {
        signalCount++;
    });

    channel.setChannelPointBalance(100);
    EXPECT_EQ(channel.channelPointBalance(), 100);
    EXPECT_EQ(signalCount, 1);

    channel.setChannelPointBalance(100);
    EXPECT_EQ(channel.channelPointBalance(), 100);
    EXPECT_EQ(signalCount, 1);

    channel.setChannelPointBalance(125);
    EXPECT_EQ(channel.channelPointBalance(), 125);
    EXPECT_EQ(signalCount, 2);
}

TEST(TwitchChannel, DuplicateChannelPointRewardPubSubMessageIgnored)
{
    MockApplication app;
    TwitchChannel channel("pajlada");

    const auto redemption =
        makeChannelPointRedemption("fd8af65d-3532-4e91-b30e-3995cefe576b");

    channel.addChannelPointReward(ChannelPointReward(redemption));
    channel.addChannelPointReward(ChannelPointReward(redemption));

    EXPECT_EQ(channel.countMessages(), 1);
}

TEST(TwitchChannel, DuplicateChannelPointRewardPubSubMessageIgnoredWithoutId)
{
    MockApplication app;
    TwitchChannel channel("pajlada");

    const auto redemption = makeChannelPointRedemption({}, "cursor-1");

    channel.addChannelPointReward(ChannelPointReward(redemption));
    channel.addChannelPointReward(ChannelPointReward(redemption));

    EXPECT_EQ(channel.countMessages(), 1);
}

TEST(TwitchChannel, EnablingAnonymityClearsAuthenticatedPubSubTopics)
{
    MockApplication app;
    app.settings.enablePinnedMessages.setValue(false);

    TwitchChannel channel("pajlada", TwitchChannelMode::Authenticated);
    TwitchChannelTestAccess::setRoomId(channel, "11148817");

    app.pubSub.listenToUserChannelPoints("user-1", "test-token");
    QTest::qWait(200);
    ASSERT_EQ(app.pubSub.diag.listenResponses, 1);

    app.testMode = false;
    channel.setModeOverride(TwitchChannelMode::BajerinoAnonymous);
    QTest::qWait(500);

    const auto responsesAfterAnonymizing =
        app.pubSub.diag.listenResponses.load();
    app.pubSub.listenToUserChannelPoints("user-1", "test-token");
    QTest::qWait(200);

    EXPECT_EQ(app.pubSub.diag.listenResponses, responsesAfterAnonymizing + 1);
}

TEST(TwitchChannel, BajerinoAnonymousSubscribesToPublicPubSubTopics)
{
    MockApplication app;
    app.settings.enablePinnedMessages.setValue(false);
    app.settings.enablePredictions.setValue(true);
    app.settings.enablePolls.setValue(true);
    app.settings.showRaidStatusAboveInput.setValue(true);

    TwitchChannel channel("pajlada", TwitchChannelMode::BajerinoAnonymous);
    TwitchChannelTestAccess::setRoomId(channel, "11148817");

    // Open the fixture connection while self-signed test certificates are
    // accepted, then exercise the production-only refresh path on that socket.
    app.pubSub.listenToChannelPointRewards("fixture-bootstrap");
    for (int attempt = 0;
         attempt < 20 && app.pubSub.diag.listenResponses.load() < 1; ++attempt)
    {
        QTest::qWait(100);
    }
    ASSERT_EQ(app.pubSub.diag.listenResponses, 1);

    app.testMode = false;
    TwitchChannelTestAccess::refreshPubSub(channel);
    for (int attempt = 0;
         attempt < 20 && app.pubSub.diag.listenResponses.load() < 5; ++attempt)
    {
        QTest::qWait(100);
    }

    // Channel-point rewards, predictions, polls, and raids are all public.
    EXPECT_EQ(app.pubSub.wsDiag().connectionsOpened, 1);
    EXPECT_EQ(app.pubSub.wsDiag().connectionsFailed, 0);
    EXPECT_EQ(app.pubSub.diag.failedListenResponses, 0);
    EXPECT_EQ(app.pubSub.diag.listenResponses, 5);
}

TEST(TwitchChannel,
     AuthenticatedPubSubTopicsRemainUntilLastEligibleChannelChangesMode)
{
    MockApplication app;
    app.settings.enablePinnedMessages.setValue(false);

    auto first = std::make_shared<TwitchChannel>(
        "pajlada", TwitchChannelMode::Authenticated);
    auto second = std::make_shared<TwitchChannel>(
        "forsen", TwitchChannelMode::AnonymousRead);
    TwitchChannelTestAccess::setRoomId(*first, "11148817");
    TwitchChannelTestAccess::setRoomId(*second, "22484632");
    app.twitch.mockChannels.emplace(first->getName(), first);
    app.twitch.mockChannels.emplace(second->getName(), second);

    app.pubSub.listenToUserChannelPoints("user-1", "test-token");
    QTest::qWait(200);
    ASSERT_EQ(app.pubSub.diag.listenResponses, 1);

    app.testMode = false;
    first->setModeOverride(TwitchChannelMode::BajerinoAnonymous);
    QTest::qWait(200);

    const auto responsesWithEligibleChannel =
        app.pubSub.diag.listenResponses.load();
    app.pubSub.listenToUserChannelPoints("user-1", "test-token");
    QTest::qWait(200);
    EXPECT_EQ(app.pubSub.diag.listenResponses, responsesWithEligibleChannel);

    second->setModeOverride(TwitchChannelMode::BajerinoAnonymous);
    QTest::qWait(200);

    const auto responsesAfterLastEligibleChannel =
        app.pubSub.diag.listenResponses.load();
    app.pubSub.listenToUserChannelPoints("user-1", "test-token");
    QTest::qWait(200);
    EXPECT_EQ(app.pubSub.diag.listenResponses,
              responsesAfterLastEligibleChannel + 1);
}

TEST(TwitchChannel, ChannelPointsPubSubUpdateRequiresMatchingChannel)
{
    MockApplication app;
    TwitchChannel channel("pajlada");
    TwitchChannelTestAccess::setRoomId(channel, "111");
    channel.setChannelPointBalance(100);

    channel.handleUserPointsUpdate(QJsonObject{
        {"type", "points-spent"},
        {"data", QJsonObject{{"balance", QJsonObject{{"channel_id", "222"},
                                                     {"balance", 90}}}}},
    });

    EXPECT_EQ(channel.channelPointBalance(), 100);
}

TEST(TwitchChannel, ChannelPointsPubSubUpdateIgnoresMalformedPayload)
{
    MockApplication app;
    TwitchChannel channel("pajlada");
    TwitchChannelTestAccess::setRoomId(channel, "111");
    channel.setChannelPointBalance(100);

    channel.handleUserPointsUpdate(QJsonObject{
        {"type", "points-spent"},
        {"data", QJsonObject{{"channel_id", "111"}}},
    });

    EXPECT_EQ(channel.channelPointBalance(), 100);
}

TEST(TwitchChannel, ChannelPointsPubSubUpdateAppliesMatchingBalance)
{
    MockApplication app;
    TwitchChannel channel("pajlada");
    TwitchChannelTestAccess::setRoomId(channel, "111");
    channel.setChannelPointBalance(100);

    channel.handleUserPointsUpdate(QJsonObject{
        {"type", "points-spent"},
        {"data", QJsonObject{{"balance", QJsonObject{{"channel_id", "111"},
                                                     {"balance", 90}}}}},
    });

    EXPECT_EQ(channel.channelPointBalance(), 90);
}

TEST(TwitchChannel, PredictionPubSubUpdateSetsActivePrediction)
{
    MockApplication app;
    TwitchChannel channel("pajlada");

    int signalCount = 0;
    auto connection = channel.predictionChanged.connect([&signalCount] {
        signalCount++;
    });

    channel.handlePredictionUpdate(QJsonObject{
        {"type", "event-created"},
        {"data",
         QJsonObject{
             {"event",
              QJsonObject{
                  {"id", "prediction-1"},
                  {"title", "Will it work?"},
                  {"status", "ACTIVE"},
                  {"prediction_window_seconds", 120},
                  {"created_at", "2026-03-08T12:00:00Z"},
                  {"created_by", QJsonObject{{"display_name", "Moderator"}}},
                  {"locked_by", QJsonObject{}},
                  {"ended_by", QJsonObject{}},
                  {"outcomes",
                   QJsonArray{
                       QJsonObject{{"id", "outcome-1"},
                                   {"title", "Yes"},
                                   {"total_points", 400},
                                   {"total_users", 4}},
                       QJsonObject{{"id", "outcome-2"},
                                   {"title", "No"},
                                   {"total_points", 100},
                                   {"total_users", 1}},
                   }},
              }},
         }},
    });

    auto prediction = *channel.accessPrediction();
    ASSERT_TRUE(prediction.has_value());
    EXPECT_EQ(prediction->id, "prediction-1");
    EXPECT_EQ(prediction->title, "Will it work?");
    EXPECT_EQ(prediction->status, "ACTIVE");
    ASSERT_EQ(prediction->outcomes.size(), 2);
    EXPECT_EQ(prediction->outcomes[0].title, "Yes");
    EXPECT_EQ(prediction->outcomes[1].title, "No");
    EXPECT_EQ(signalCount, 1);
}

TEST(TwitchChannel, PredictionCanceledClearsActivePrediction)
{
    MockApplication app;
    TwitchChannel channel("pajlada");

    TwitchChannel::PredictionEvent prediction;
    prediction.id = "prediction-1";
    prediction.title = "Will it work?";
    prediction.status = "ACTIVE";
    channel.setActivePrediction(prediction);

    channel.handlePredictionUpdate(QJsonObject{
        {"type", "event-canceled"},
        {"data", QJsonObject{}},
    });

    EXPECT_FALSE((*channel.accessPrediction()).has_value());
}

TEST(TwitchChannel, DuplicatePinnedChatUnpinOnlyAddsOneSystemMessage)
{
    MockApplication app;
    app.settings.enablePinnedMessages.setValue(true);
    app.settings.showUnpinNotifications.setValue(true);
    TwitchChannel channel("pajlada");

    TwitchChannel::PinnedMessage pin;
    pin.pinId = "pin-1";
    channel.setPinnedMessage(pin);

    int signalCount = 0;
    auto connection = channel.pinnedMessageChanged.connect([&signalCount] {
        signalCount++;
    });

    const auto messageCountBefore = channel.getMessageSnapshot().size();
    const auto payload = makePinnedChatUnpinPayload("pin-1");

    channel.handlePinnedChatUpdate(payload);
    channel.handlePinnedChatUpdate(payload);

    const auto messages = channel.getMessageSnapshot();
    ASSERT_EQ(messages.size(), messageCountBefore + 1);
    EXPECT_EQ(messages.back()->messageText, "Moderator unpinned the message.");
    EXPECT_FALSE((*channel.accessPinnedMessage()).has_value());
    EXPECT_EQ(signalCount, 1);
}

TEST(TwitchChannel, StalePinnedChatUnpinDoesNotClearNewerPin)
{
    MockApplication app;
    app.settings.enablePinnedMessages.setValue(true);
    TwitchChannel channel("pajlada");

    TwitchChannel::PinnedMessage pin;
    pin.pinId = "pin-new";
    channel.setPinnedMessage(pin);

    const auto messageCountBefore = channel.getMessageSnapshot().size();

    channel.handlePinnedChatUpdate(makePinnedChatUnpinPayload("pin-old"));

    const auto currentPin = *channel.accessPinnedMessage();
    ASSERT_TRUE(currentPin.has_value());
    EXPECT_EQ(currentPin->pinId, "pin-new");
    EXPECT_EQ(channel.getMessageSnapshot().size(), messageCountBefore);
}

}  // namespace

}  // namespace chatterino
