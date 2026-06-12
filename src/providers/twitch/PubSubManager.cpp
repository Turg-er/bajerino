// SPDX-FileCopyrightText: 2022 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PubSubManager.hpp"

#include "Application.hpp"
#include "common/Env.hpp"
#include "common/QLogging.hpp"
#include "providers/liveupdates/BasicPubSubManager.hpp"
#include "providers/NetworkConfigurationProvider.hpp"
#include "providers/twitch/PubSubClient.hpp"

#include <QJsonArray>
#include <QStringList>

#include <memory>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

using namespace std::chrono_literals;

namespace chatterino {

namespace {

QString authenticatedTopicUserID(const QString &topic)
{
    const QStringList prefixes{
        u"predictions-user-v1."_s,
        u"community-points-user-v1."_s,
        u"chatrooms-user-v1."_s,
    };

    for (const auto &prefix : prefixes)
    {
        if (topic.startsWith(prefix))
        {
            return topic.mid(prefix.size());
        }
    }

    return {};
}

std::optional<WebSocketProxyOptions> twitchPubSubProxyOptions()
{
    // PubSub is an authenticated Twitch connection, so it is proxied in every
    // proxy mode (global, BAJERINO_PROXY_TWITCH, BAJERINO_PROXY_TWITCH_API_ONLY).
    return NetworkConfigurationProvider::webSocketProxyFromEnv(
        Env::get(), ProxyConnection::AuthedTwitch);
}

}  // namespace

class PubSubManagerPrivate
    : public BasicPubSubManager<PubSubManagerPrivate, PubSubClient>
{
public:
    PubSubManagerPrivate(PubSub &parent, QString host,
                         std::chrono::milliseconds heartbeatInterval);
    ~PubSubManagerPrivate() override;
    PubSubManagerPrivate(const PubSubManagerPrivate &) = delete;
    PubSubManagerPrivate(PubSubManagerPrivate &&) = delete;
    PubSubManagerPrivate &operator=(const PubSubManagerPrivate &) = delete;
    PubSubManagerPrivate &operator=(PubSubManagerPrivate &&) = delete;

    std::shared_ptr<PubSubClient> makeClient();
    void checkHeartbeats();

    void reconnect()
    {
        for (const auto &[id, c] : this->clients())
        {
            c->close();
        }
    }

    std::chrono::milliseconds heartbeatInterval;
    QTimer heartbeatTimer;

    PubSub &parent;

    friend BasicPubSubManager<PubSubManagerPrivate, PubSubClient>;
    friend PubSub;
};

PubSubManagerPrivate::PubSubManagerPrivate(
    PubSub &parent, QString host, std::chrono::milliseconds heartbeatInterval)
    : BasicPubSubManager(std::move(host), "PubSub", twitchPubSubProxyOptions())
    , heartbeatInterval(heartbeatInterval)
    , parent(parent)
{
    QObject::connect(&this->heartbeatTimer, &QTimer::timeout, this,
                     &PubSubManagerPrivate::checkHeartbeats);
    this->heartbeatTimer.setInterval(this->heartbeatInterval);
    this->heartbeatTimer.setSingleShot(false);
    this->heartbeatTimer.start();
}

PubSubManagerPrivate::~PubSubManagerPrivate()
{
    this->stop();
}

void PubSubManagerPrivate::checkHeartbeats()
{
    for (const auto &[id, client] : this->clients())
    {
        client->checkHeartbeat();
    }
}

std::shared_ptr<PubSubClient> PubSubManagerPrivate::makeClient()
{
    return std::make_shared<PubSubClient>(this->parent,
                                          this->heartbeatInterval);
}

PubSub::PubSub(const QString &host, std::chrono::seconds heartbeatInterval)
    : private_(new PubSubManagerPrivate(*this, host, heartbeatInterval))
{
}

PubSub::~PubSub() = default;

const liveupdates::Diag &PubSub::wsDiag() const
{
    return this->private_->diag;
}

void PubSub::stop()
{
    this->private_->stop();
}

void PubSub::listenToChannelPointRewards(const QString &channelID)
{
    static const QString topicFormat("community-points-channel-v1.%1");
    assert(!channelID.isEmpty());

    auto topic = topicFormat.arg(channelID);

    qCDebug(chatterinoPubSub) << "Listen to topic" << topic;
    this->private_->subscribe(TopicData{.topic = std::move(topic)});
}

void PubSub::listenToPinnedChatUpdates(const QString &channelID)
{
    static const QString topicFormat("pinned-chat-updates-v1.%1");
    assert(!channelID.isEmpty());
    auto topic = topicFormat.arg(channelID);
    qCDebug(chatterinoPubSub)
        << "Request pinned chat PubSub subscription" << topic;
    this->private_->subscribe(TopicData{.topic = std::move(topic)});
}

void PubSub::listenToPredictions(const QString &channelID)
{
    static const QString topicFormat("predictions-channel-v1.%1");
    assert(!channelID.isEmpty());
    auto topic = topicFormat.arg(channelID);
    qCDebug(chatterinoPubSub) << "Listen to topic" << topic;
    this->private_->subscribe(TopicData{.topic = std::move(topic)});
}

void PubSub::listenToPolls(const QString &channelID)
{
    static const QString topicFormat("polls.%1");
    assert(!channelID.isEmpty());
    auto topic = topicFormat.arg(channelID);
    qCDebug(chatterinoPubSub) << "Listen to topic" << topic;
    this->private_->subscribe(TopicData{.topic = std::move(topic)});
}

void PubSub::listenToRaids(const QString &channelID)
{
    static const QString topicFormat("raid.%1");
    assert(!channelID.isEmpty());
    auto topic = topicFormat.arg(channelID);
    qCDebug(chatterinoPubSub) << "Listen to topic" << topic;
    this->private_->subscribe(TopicData{.topic = std::move(topic)});
}

void PubSub::listenToChatWarnings(const QString &userID,
                                  const QString &authToken)
{
    this->listenToAuthenticatedTopic("chatrooms-user-v1." + userID, authToken);
}

void PubSub::listenToUserPredictions(const QString &userID,
                                     const QString &authToken)
{
    this->listenToAuthenticatedTopic("predictions-user-v1." + userID,
                                     authToken);
}

void PubSub::listenToUserChannelPoints(const QString &userID,
                                       const QString &authToken)
{
    this->listenToAuthenticatedTopic("community-points-user-v1." + userID,
                                     authToken);
}

void PubSub::listenToAuthenticatedTopic(QString topic, const QString &authToken)
{
    assert(!topic.isEmpty());
    if (authToken.isEmpty())
    {
        return;
    }

    auto it = this->authenticatedTopicTokens_.find(topic);
    if (it != this->authenticatedTopicTokens_.end())
    {
        if (it->second == authToken)
        {
            return;
        }
        this->private_->unsubscribe(TopicData{.topic = topic});
        it->second = authToken;
    }
    else
    {
        this->authenticatedTopicTokens_.emplace(topic, authToken);
    }

    qCDebug(chatterinoPubSub) << "Listen to authenticated topic" << topic;
    this->private_->subscribe(
        TopicData{.topic = std::move(topic), .authToken = authToken});
}

void PubSub::forgetUserAuthenticatedTopics(const QString &userID)
{
    QStringList topics;
    for (const auto &[topic, token] : this->authenticatedTopicTokens_)
    {
        (void)token;
        if (userID.isEmpty() || authenticatedTopicUserID(topic) == userID)
        {
            topics.push_back(topic);
        }
    }

    for (const auto &topic : topics)
    {
        qCDebug(chatterinoPubSub)
            << "Unlisten from authenticated topic" << topic;
        this->private_->unsubscribe(TopicData{.topic = topic});
        this->authenticatedTopicTokens_.erase(topic);
    }
}

void PubSub::forgetOtherUserAuthenticatedTopics(const QString &userID)
{
    QStringList topics;
    for (const auto &[topic, token] : this->authenticatedTopicTokens_)
    {
        (void)token;
        const auto topicUserID = authenticatedTopicUserID(topic);
        if (userID.isEmpty() || topicUserID != userID)
        {
            topics.push_back(topic);
        }
    }

    for (const auto &topic : topics)
    {
        qCDebug(chatterinoPubSub)
            << "Unlisten from authenticated topic" << topic;
        this->private_->unsubscribe(TopicData{.topic = topic});
        this->authenticatedTopicTokens_.erase(topic);
    }
}

void PubSub::reconnect()
{
    this->private_->reconnect();
}

}  // namespace chatterino
