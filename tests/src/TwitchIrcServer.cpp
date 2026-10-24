// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchIrcServer.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "messages/Emote.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Helix.hpp"
#include "mocks/Logging.hpp"
#include "providers/bttv/BttvLiveUpdates.hpp"
#include "providers/bttv/liveupdates/BttvLiveUpdateMessages.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "Test.hpp"
#include "TwitchChannelTestAccess.hpp"
#include "util/DebugCount.hpp"

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

    static void setReadPool(TwitchIrcServer &server,
                            TwitchReadConnectionPool *pool)
    {
        server.anonymousReadConnection_.reset(pool);
    }

    static void handleReadBlock(TwitchIrcServer &server, const QByteArray &raw)
    {
        std::unique_ptr<Communi::IrcMessage> message(
            Communi::IrcMessage::fromData(raw, nullptr));
        ASSERT_NE(message, nullptr);
        server.handleReadBlock(message.get());
    }

    static void restoreReadRoute(TwitchIrcServer &server,
                                 const std::shared_ptr<TwitchChannel> &channel)
    {
        // Suppress connection startup: this test checks routing and identity.
        server.readConnectionStarted_ = true;
        channel->clearExpectedReconnectParts();
        server.reconnectChannel(channel);
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

class LiveEmoteApplication : public MockApplication
{
public:
    BttvLiveUpdates *getBttvLiveUpdates() override
    {
        return &this->bttv;
    }

    SeventvEventAPI *getSeventvEventAPI() override
    {
        return &this->seventv;
    }

    SeventvPersonalEmotes *getSeventvPersonalEmotes() override
    {
        return &this->personal;
    }

    KickChatServer *getKickChatServer() override
    {
        return nullptr;
    }

    BttvLiveUpdates bttv{"wss://127.0.0.1:1"};
    SeventvEventAPI seventv{"wss://127.0.0.1:1"};
    SeventvPersonalEmotes personal;
};

TEST(TwitchIrcServer, BttvUpdatesFollowEmoteSourcesAcrossReadModes)
{
    LiveEmoteApplication app;
    app.settings.enableBTTVChannelEmotes = true;
    app.twitch.initEventAPIs(&app.bttv, nullptr);
    auto first = std::make_shared<TwitchChannel>("first_emote_source");
    auto second = std::make_shared<TwitchChannel>("second_emote_source");
    TwitchIrcServerTestAccess::addAuthenticatedReadChannel(app.twitch, first);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, second);
    ASSERT_TRUE(
        first->setEmoteSourceOverride(ChannelEmoteProvider::Bttv, "99"));
    ASSERT_TRUE(
        second->setEmoteSourceOverride(ChannelEmoteProvider::Bttv, "99"));

    const QJsonObject emote{
        {"id", "shared-emote"}, {"code", "SharedEmote"}, {"imageType", "png"}};
    const BttvLiveUpdateEmoteUpdateAddMessage donor(
        {{"channel", "twitch:99"}, {"emote", emote}});
    app.bttv.signals_.emoteAdded.invoke(donor);
    QCoreApplication::processEvents();
    EXPECT_TRUE(first->bttvEmote(EmoteName{"SharedEmote"}).has_value());
    EXPECT_TRUE(second->bttvEmote(EmoteName{"SharedEmote"}).has_value());

    ASSERT_TRUE(
        first->setEmoteSourceOverride(ChannelEmoteProvider::Bttv, "100"));
    EXPECT_TRUE(first->bttvEmotes()->empty());
    app.bttv.signals_.emoteAdded.invoke(donor);
    QCoreApplication::processEvents();
    EXPECT_TRUE(first->bttvEmotes()->empty());
    EXPECT_TRUE(second->bttvEmote(EmoteName{"SharedEmote"}).has_value());

    app.bttv.signals_.emoteAdded.invoke(BttvLiveUpdateEmoteUpdateAddMessage(
        {{"channel", "twitch:100"}, {"emote", emote}}));
    QCoreApplication::processEvents();
    EXPECT_TRUE(first->bttvEmote(EmoteName{"SharedEmote"}).has_value());
}

TEST(TwitchIrcServer, SevenTvSubscriptionsRemainUntilLastUserReleasesThem)
{
    LiveEmoteApplication app;
    auto first = std::make_shared<TwitchChannel>("first_seventv_source");
    auto second = std::make_shared<TwitchChannel>("second_seventv_source");
    TwitchIrcServerTestAccess::addAuthenticatedReadChannel(app.twitch, first);
    TwitchIrcServerTestAccess::addAnonymousChannel(app.twitch, second);
    const auto subscriptions = [] {
        return DebugCount::get(DebugObject::LiveUpdatesSubscriptionBacklog) +
               DebugCount::get(DebugObject::LiveUpdatesSubscription);
    };
    const auto baseline = subscriptions();

    first->updateSeventvData("user-a", "set-a");
    EXPECT_EQ(subscriptions(), baseline + 2);
    second->updateSeventvData("user-a", "set-a");
    EXPECT_EQ(subscriptions(), baseline + 2);
    first->updateSeventvData("user-b", "set-b");
    EXPECT_EQ(subscriptions(), baseline + 4);
    second.reset();
    EXPECT_EQ(subscriptions(), baseline + 2);
    first->updateSeventvData({}, {});
    EXPECT_EQ(subscriptions(), baseline);

    app.personal.createEmoteSet("set-a");
    first->updateSeventvData("user-a", "set-a");
    first->updateSeventvData({}, {});
    EXPECT_EQ(subscriptions(), baseline + 1);
    app.seventv.unsubscribeEmoteSet("set-a");
    EXPECT_EQ(subscriptions(), baseline);
}

TEST(TwitchIrcServer, SevenTvUpdatesReachChannelsAndPersonalSets)
{
    LiveEmoteApplication app;
    app.settings.enableSevenTVPersonalEmotes = true;
    app.twitch.initEventAPIs(nullptr, &app.seventv);
    auto channel = std::make_shared<TwitchChannel>("shared_seventv_set");
    TwitchIrcServerTestAccess::addAuthenticatedReadChannel(app.twitch, channel);
    channel->updateSeventvData("user-a", "set-a");
    auto emote = std::make_shared<Emote>();
    emote->id = EmoteId{"emote-a"};
    emote->name = EmoteName{"Before"};
    EmoteMap original{{emote->name, emote}};
    channel->setSeventvEmotes(std::make_shared<const EmoteMap>(original));
    app.personal.addEmoteSetForTwitchUser("set-a", std::move(original), "99");

    const seventv::eventapi::Dispatch dispatch(
        {{"type", "emote_set.update"}, {"body", QJsonObject{{"id", "set-a"}}}});
    const seventv::eventapi::EmoteUpdateDispatch renamed(
        dispatch, {{"name", "Before"}}, {{"id", "emote-a"}, {"name", "After"}});
    app.seventv.signals_.emoteUpdated.invoke(renamed);
    QCoreApplication::processEvents();
    EXPECT_TRUE(channel->seventvEmotes()->contains(EmoteName{"After"}));
    EXPECT_NE(app.personal.getEmoteForTwitchUser("99", EmoteName{"After"}),
              nullptr);

    app.seventv.signals_.emoteRemoved.invoke(
        seventv::eventapi::EmoteRemoveDispatch(
            dispatch, {{"id", "emote-a"}, {"name", "After"}}));
    QCoreApplication::processEvents();
    EXPECT_TRUE(channel->seventvEmotes()->empty());
    EXPECT_EQ(app.personal.getEmoteForTwitchUser("99", EmoteName{"After"}),
              nullptr);
}

class ReadPoolProbe : public TwitchReadConnectionPool
{
public:
    using TwitchReadConnectionPool::TwitchReadConnectionPool;
    void onChannelCreated(TwitchChannel &channel) override
    {
        ++this->added;
        this->name = channel.getName();
    }
    void removeChannel(const QString &name) override
    {
        ++this->removed;
        EXPECT_EQ(name, this->name);
    }
    void reconnectChannel(TwitchChannel &) override
    {
        ++this->rejoined;
    }
    void reconnect() override
    {
        ++this->reconnected;
    }
    void close() override
    {
    }
    QString name;
    int added = 0;
    int removed = 0;
    int rejoined = 0;
    int reconnected = 0;
};

TEST(TwitchIrcServer, BanFallbackPreservesChannelIdentityAndConfiguredMode)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    auto account =
        std::make_shared<TwitchAccount>("viewer", "token", "client", "123");
    app.accounts.twitch.accounts.append(account);
    app.accounts.twitch.currentUsername = "viewer";
    app.accounts.twitch.load();

    auto channel = std::make_shared<TwitchChannel>(
        "pajlada", TwitchChannelMode::Authenticated);
    TwitchIrcServerTestAccess::addAuthenticatedReadChannel(app.twitch, channel);
    auto *pool = new ReadPoolProbe(&app.twitch);
    TwitchIrcServerTestAccess::setReadPool(app.twitch, pool);

    for (const auto &ignored :
         {QByteArray("@target-user-id=123;ban-duration=60 :tmi.twitch.tv "
                     "CLEARCHAT #pajlada :viewer"),
          QByteArray(
              "@target-user-id=456 :tmi.twitch.tv CLEARCHAT #pajlada :other"),
          QByteArray("@historical=1;msg-id=msg_banned :tmi.twitch.tv NOTICE "
                     "#pajlada :banned")})
    {
        TwitchIrcServerTestAccess::handleReadBlock(app.twitch, ignored);
        EXPECT_FALSE(channel->usesAnonymousReadConnection());
        EXPECT_EQ(pool->added, 0);
    }

    const QByteArray ban(
        "@target-user-id=123 :tmi.twitch.tv CLEARCHAT #pajlada :viewer");
    TwitchIrcServerTestAccess::handleReadBlock(app.twitch, ban);
    EXPECT_TRUE(channel->usesAnonymousReadConnection());
    EXPECT_TRUE(channel->usesAuthenticatedFeatures());
    EXPECT_TRUE(channel->canSendMessage());
    EXPECT_EQ(channel->modeOverride(), TwitchChannelMode::Authenticated);
    EXPECT_EQ(app.twitch.getOrAddChannel("pajlada"), channel);
    EXPECT_EQ(app.twitch.getAnonymousChannelOrEmpty("pajlada"), channel);
    EXPECT_EQ(pool->added, 1);
    EXPECT_EQ(pool->reconnected, 1);

    TwitchIrcServerTestAccess::handleReadBlock(app.twitch, ban);
    EXPECT_EQ(pool->added, 1);

    TwitchIrcServerTestAccess::restoreReadRoute(app.twitch, channel);
    EXPECT_FALSE(channel->usesAnonymousReadConnection());
    EXPECT_TRUE(channel->usesAuthenticatedFeatures());
    EXPECT_EQ(channel->modeOverride(), TwitchChannelMode::Authenticated);
    EXPECT_EQ(app.twitch.getOrAddChannel("pajlada"), channel);
    EXPECT_TRUE(app.twitch.getAnonymousChannelOrEmpty("pajlada")->isEmpty());
    EXPECT_EQ(pool->removed, 1);
}

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
