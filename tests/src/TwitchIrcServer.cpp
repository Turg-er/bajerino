// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchIrcServer.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "mocks/BaseApplication.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "Test.hpp"

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

    AccountController accounts;
    TwitchIrcServer twitch;
};

TEST(TwitchIrcServer, AppliesModeratedChannelInfoWithoutHoldingChannelMutex)
{
    MockApplication app;
    auto channel = std::make_shared<TwitchChannel>("pajlada", true);
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

}  // namespace
}  // namespace chatterino
