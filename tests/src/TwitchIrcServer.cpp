// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchIrcServer.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Logging.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "Test.hpp"
#include "TwitchChannelTestAccess.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <memory>
#include <mutex>

namespace chatterino {

class TwitchIrcServerTestAccess
{
public:
    static void addAnonymousChannel(
        TwitchIrcServer &server, const std::shared_ptr<TwitchChannel> &channel)
    {
        server.anonymousChannels.insert(channel->getName(), channel);
    }

    static void addAuthenticatedReadChannel(
        TwitchIrcServer &server, const std::shared_ptr<TwitchChannel> &channel)
    {
        server.channels.insert(channel->getName(), channel);
    }

    static void clearChannels(TwitchIrcServer &server)
    {
        server.channels.clear();
        server.anonymousChannels.clear();
    }

    static void addModeratedChannel(TwitchIrcServer &server,
                                    const QString &channelName)
    {
        server.moderatedChannels.insert(channelName);
    }

    static void applyModeratedChannelInfo(TwitchIrcServer &server)
    {
        server.applyModeratedChannelInfo();
    }

    static bool channelMutexIsAvailable(TwitchIrcServer &server)
    {
        std::unique_lock lock(server.channelMutex, std::try_to_lock);
        return lock.owns_lock();
    }
};

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    PubSub *getTwitchPubSub() override
    {
        return &this->pubSub;
    }

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    AccountController accounts;
    mock::EmptyLogging logging;
    PubSub pubSub{"wss://127.0.0.1:1"};
    TwitchIrcServer twitch;
};

TEST(TwitchIrcServer, AppliesModeratedChannelInfoWithoutHoldingChannelMutex)
{
    MockApplication app;
    auto channel = std::make_shared<TwitchChannel>(
        "pajlada", TwitchChannelMode::BajerinoAnonymous);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, channel);

    bool signalInvoked = false;
    bool mutexWasAvailable = false;
    auto connection = channel->userStateChanged.connect([&] {
        signalInvoked = true;
        mutexWasAvailable =
            TwitchIrcServerTestAccess::channelMutexIsAvailable(app.twitch);
    });

    TwitchIrcServerTestAccess::addModeratedChannel(app.twitch,
                                                   channel->getName());
    TwitchIrcServerTestAccess::applyModeratedChannelInfo(app.twitch);

    EXPECT_TRUE(channel->isMod());
    EXPECT_TRUE(signalInvoked);
    EXPECT_TRUE(mutexWasAvailable);
}

TEST(TwitchIrcServer, DetectsAuthenticatedFeaturesAcrossReadRoutes)
{
    MockApplication app;

    auto bajerinoAnonymous = std::make_shared<TwitchChannel>(
        "bajerino-anonymous", TwitchChannelMode::BajerinoAnonymous);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch,
                                                   bajerinoAnonymous);
    EXPECT_FALSE(app.twitch.hasAuthenticatedChannels());

    TwitchIrcServerTestAccess::clearChannels(app.twitch);
    auto anonymousRead = std::make_shared<TwitchChannel>(
        "anonymous-read", TwitchChannelMode::AnonymousRead);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, anonymousRead);
    EXPECT_TRUE(app.twitch.hasAuthenticatedChannels());

    TwitchIrcServerTestAccess::clearChannels(app.twitch);
    auto authenticated = std::make_shared<TwitchChannel>(
        "authenticated", TwitchChannelMode::Authenticated);
    TwitchIrcServerTestAccess::addAuthenticatedReadChannel(app.twitch,
                                                           authenticated);
    EXPECT_TRUE(app.twitch.hasAuthenticatedChannels());
}

TEST(TwitchIrcServer, OrdinaryLookupPreservesExistingModeOverride)
{
    MockApplication app;
    auto channel = std::make_shared<TwitchChannel>(
        "pajlada", TwitchChannelMode::BajerinoAnonymous);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, channel);

    const auto found = app.twitch.getOrAddChannel("#Pajlada");

    EXPECT_EQ(found, channel);
    EXPECT_EQ(channel->modeOverride(), TwitchChannelMode::BajerinoAnonymous);
}

TEST(TwitchIrcServer, DeliversPublicPubSubToBajerinoAnonymousChannel)
{
    MockApplication app;
    app.twitch.initialize();

    auto channel = std::make_shared<TwitchChannel>(
        "pajlada", TwitchChannelMode::BajerinoAnonymous);
    TwitchChannelTestAccess::setRoomId(*channel, "11148817");
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, channel);

    app.pubSub.prediction.updated.invoke(QJsonObject{
        {"topic", "predictions-channel-v1.11148817"},
        {"type", "event-created"},
        {"data",
         QJsonObject{
             {"event",
              QJsonObject{
                  {"id", "prediction-1"},
                  {"title", "Will it work?"},
                  {"status", "ACTIVE"},
                  {"outcomes",
                   QJsonArray{
                       QJsonObject{{"id", "outcome-1"}, {"title", "Yes"}},
                       QJsonObject{{"id", "outcome-2"}, {"title", "No"}},
                   }},
              }},
         }},
    });
    app.pubSub.poll.updated.invoke(QJsonObject{
        {"topic", "polls.11148817"},
        {"type", "POLL_CREATE"},
        {"data",
         QJsonObject{
             {"poll_id", "poll-1"},
             {"title", "Choose one"},
             {"status", "ACTIVE"},
             {"choices",
              QJsonArray{
                  QJsonObject{
                      {"choice_id", "choice-1"},
                      {"title", "First"},
                      {"votes", QJsonObject{{"total", 1}}},
                  },
                  QJsonObject{
                      {"choice_id", "choice-2"},
                      {"title", "Second"},
                      {"votes", QJsonObject{{"total", 0}}},
                  },
              }},
         }},
    });
    app.pubSub.raid.updated.invoke(QJsonObject{
        {"topic", "raid.11148817"},
        {"raid",
         QJsonObject{
             {"id", "raid-1"},
             {"source_id", "11148817"},
             {"target_id", "22484632"},
             {"target_login", "forsen"},
         }},
    });
    QCoreApplication::processEvents();

    const auto prediction = *channel->accessPrediction();
    ASSERT_TRUE(prediction.has_value());
    EXPECT_EQ(prediction->id, "prediction-1");

    const auto poll = *channel->accessPoll();
    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(poll->id, "poll-1");

    const auto raid = *channel->accessRaid();
    ASSERT_TRUE(raid.has_value());
    EXPECT_EQ(raid->id, "raid-1");
}

}  // namespace
}  // namespace chatterino
