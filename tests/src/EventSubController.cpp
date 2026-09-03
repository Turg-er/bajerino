// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "mocks/BaseApplication.hpp"
#include "mocks/Helix.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/eventsub/Controller.hpp"
#include "Test.hpp"

#include <boost/asio/post.hpp>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace chatterino::eventsub {

class ControllerTestAccess
{
public:
    static void post(Controller &controller, std::function<void()> action)
    {
        boost::asio::post(controller.ioContext, std::move(action));
    }

    static std::optional<uint64_t> generation(
        Controller &controller, const SubscriptionRequest &request)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        auto it = controller.subscriptions.find(request);
        if (it == controller.subscriptions.end())
        {
            return std::nullopt;
        }
        return it->second.generation;
    }

    static bool isSubscribing(Controller &controller,
                              const SubscriptionRequest &request,
                              uint64_t generation, int32_t refCount)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        auto it = controller.subscriptions.find(request);
        return it != controller.subscriptions.end() &&
               it->second.state ==
                   Controller::Subscription::State::Subscribing &&
               it->second.generation == generation &&
               it->second.refCount == refCount;
    }

    static void erase(Controller &controller,
                      const SubscriptionRequest &request)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        controller.subscriptions.erase(request);
    }

    static void setSubscribed(Controller &controller,
                              const SubscriptionRequest &request,
                              const QString &subscriptionID)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        auto &subscription = controller.subscriptions.at(request);
        subscription.state = Controller::Subscription::State::Subscribed;
        subscription.subscriptionID = subscriptionID;
    }

    static bool isUnsubscribing(Controller &controller,
                                const SubscriptionRequest &request,
                                uint64_t generation, int32_t refCount,
                                const QString &subscriptionID)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        auto it = controller.subscriptions.find(request);
        return it != controller.subscriptions.end() &&
               it->second.state ==
                   Controller::Subscription::State::Unsubscribing &&
               it->second.generation == generation &&
               it->second.refCount == refCount &&
               it->second.subscriptionID == subscriptionID;
    }

    static bool contains(Controller &controller,
                         const SubscriptionRequest &request)
    {
        std::lock_guard lock(controller.subscriptionsMutex);
        return controller.subscriptions.contains(request);
    }

    static void markRequestSubscribed(Controller &controller,
                                      const SubscriptionRequest &request,
                                      uint64_t generation,
                                      const QString &subscriptionID)
    {
        controller.markRequestSubscribed(request, generation, {},
                                         subscriptionID);
    }
};

namespace {

using namespace std::chrono_literals;
using testing::_;

class MockApplication : public mock::BaseApplication
{
public:
    IController *getEventSub() override
    {
        return this->eventSub;
    }

    IController *eventSub = nullptr;
};

TEST(EventSubController, StaleSuccessDoesNotChangeNewSubscription)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);

    Controller controller;
    app.eventSub = &controller;

    std::promise<void> gateStartedPromise;
    auto gateStarted = gateStartedPromise.get_future();
    std::promise<void> releaseGatePromise;
    auto releaseGate = releaseGatePromise.get_future().share();
    ControllerTestAccess::post(controller, [&] {
        gateStartedPromise.set_value();
        releaseGate.wait();
    });

    if (gateStarted.wait_for(2s) != std::future_status::ready)
    {
        controller.setQuitting();
        releaseGatePromise.set_value();
        FAIL() << "EventSub worker did not start";
    }

    const SubscriptionRequest request{
        .subscriptionType = "channel.chat.user_message_hold",
        .subscriptionVersion = "1",
        .ownerTwitchUserID = "1234",
        .conditions = {{"broadcaster_user_id", "5678"}, {"user_id", "1234"}},
    };

    auto staleHandle = controller.subscribe(request);
    const auto staleGeneration =
        ControllerTestAccess::generation(controller, request).value_or(0);
    staleHandle.reset();
    EXPECT_TRUE(ControllerTestAccess::isSubscribing(controller, request,
                                                    staleGeneration, 0));
    ControllerTestAccess::erase(controller, request);

    auto currentHandle = controller.subscribe(request);
    const auto currentGeneration =
        ControllerTestAccess::generation(controller, request).value_or(0);
    EXPECT_NE(staleGeneration, 0);
    EXPECT_NE(currentGeneration, 0);
    EXPECT_NE(currentGeneration, staleGeneration);

    EXPECT_CALL(helix,
                deleteEventSubSubscription(QStringLiteral("stale-id"), _, _));
    ControllerTestAccess::markRequestSubscribed(controller, request,
                                                staleGeneration, "stale-id");

    EXPECT_TRUE(ControllerTestAccess::isSubscribing(controller, request,
                                                    currentGeneration, 1));

    controller.setQuitting();
    currentHandle.reset();
    releaseGatePromise.set_value();

    std::promise<void> drainedPromise;
    auto drained = drainedPromise.get_future();
    ControllerTestAccess::post(controller, [&] {
        drainedPromise.set_value();
    });
    EXPECT_EQ(drained.wait_for(2s), std::future_status::ready);
}

TEST(EventSubController, ReusesQueuedAttemptAfterResubscribe)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);

    Controller controller;
    app.eventSub = &controller;

    std::promise<void> gateStartedPromise;
    auto gateStarted = gateStartedPromise.get_future();
    std::promise<void> releaseGatePromise;
    auto releaseGate = releaseGatePromise.get_future().share();
    ControllerTestAccess::post(controller, [&] {
        gateStartedPromise.set_value();
        releaseGate.wait();
    });

    if (gateStarted.wait_for(2s) != std::future_status::ready)
    {
        controller.setQuitting();
        releaseGatePromise.set_value();
        FAIL() << "EventSub worker did not start";
    }

    const SubscriptionRequest request{
        .subscriptionType = "channel.chat.user_message_hold",
        .subscriptionVersion = "1",
        .ownerTwitchUserID = "1234",
        .conditions = {{"broadcaster_user_id", "5678"}, {"user_id", "1234"}},
    };

    auto handle = controller.subscribe(request);
    const auto generation =
        ControllerTestAccess::generation(controller, request).value_or(0);
    handle.reset();
    EXPECT_TRUE(ControllerTestAccess::isSubscribing(controller, request,
                                                    generation, 0));

    auto replacement = controller.subscribe(request);
    EXPECT_EQ(ControllerTestAccess::generation(controller, request),
              generation);
    EXPECT_TRUE(ControllerTestAccess::isSubscribing(controller, request,
                                                    generation, 1));

    controller.setQuitting();
    replacement.reset();
    releaseGatePromise.set_value();

    std::promise<void> drainedPromise;
    auto drained = drainedPromise.get_future();
    ControllerTestAccess::post(controller, [&] {
        drainedPromise.set_value();
    });
    EXPECT_EQ(drained.wait_for(2s), std::future_status::ready);
}

TEST(EventSubController, KeepsStateUntilUnsubscribeCompletes)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);

    Controller controller;
    app.eventSub = &controller;

    std::promise<void> gateStartedPromise;
    auto gateStarted = gateStartedPromise.get_future();
    std::promise<void> releaseGatePromise;
    auto releaseGate = releaseGatePromise.get_future().share();
    ControllerTestAccess::post(controller, [&] {
        gateStartedPromise.set_value();
        releaseGate.wait();
    });

    if (gateStarted.wait_for(2s) != std::future_status::ready)
    {
        controller.setQuitting();
        releaseGatePromise.set_value();
        FAIL() << "EventSub worker did not start";
    }

    const SubscriptionRequest request{
        .subscriptionType = "channel.chat.user_message_update",
        .subscriptionVersion = "1",
        .ownerTwitchUserID = "1234",
        .conditions = {{"broadcaster_user_id", "5678"}, {"user_id", "1234"}},
    };

    auto handle = controller.subscribe(request);
    const auto generation =
        ControllerTestAccess::generation(controller, request).value_or(0);
    ControllerTestAccess::setSubscribed(controller, request,
                                        QStringLiteral("subscription-id"));

    ResultCallback<> deleteSucceeded;
    EXPECT_CALL(helix, deleteEventSubSubscription(
                           QStringLiteral("subscription-id"), _, _))
        .WillOnce([&](const auto &, auto success, const auto &) {
            deleteSucceeded = std::move(success);
        });
    handle.reset();

    auto replacement = controller.subscribe(request);
    replacement.reset();
    EXPECT_TRUE(ControllerTestAccess::isUnsubscribing(
        controller, request, generation, 0, QStringLiteral("subscription-id")));

    EXPECT_TRUE(static_cast<bool>(deleteSucceeded));
    if (deleteSucceeded)
    {
        deleteSucceeded();
    }
    EXPECT_FALSE(ControllerTestAccess::contains(controller, request));

    controller.setQuitting();
    releaseGatePromise.set_value();

    std::promise<void> drainedPromise;
    auto drained = drainedPromise.get_future();
    ControllerTestAccess::post(controller, [&] {
        drainedPromise.set_value();
    });
    EXPECT_EQ(drained.wait_for(2s), std::future_status::ready);
}

}  // namespace
}  // namespace chatterino::eventsub
