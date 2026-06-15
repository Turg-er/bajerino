// SPDX-License-Identifier: MIT

#include "providers/twitch/ChannelPointsFarm.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/ChannelPointsFarmModel.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <tuple>

namespace chatterino {

using namespace Qt::StringLiterals;

namespace {

// Spade requests every minute; sending slightly under a minute avoids missing
// a watch-credit boundary if the request is a little slow.
constexpr int TICK_INTERVAL_MS = 58 * 1000;
// Re-scrape a channel's spade URL at most this often; it rarely changes.
constexpr qint64 SPADE_TTL_SECONDS = 6LL * 60 * 60;

}  // namespace

ChannelPointsFarm::ChannelPointsFarm()
{
    for (const auto &name : this->setting_.getValue())
    {
        this->channels_.append(name);
    }

    // channels_ outlives this connection, so it's safe to ignore.
    std::ignore = this->channels_.delayedItemsChanged.connect([this] {
        this->setting_.setValue(this->channels_.raw());
    });

    QObject::connect(&this->timer_, &QTimer::timeout, [this] {
        this->tick();
    });
    this->timer_.start(TICK_INTERVAL_MS);
}

void ChannelPointsFarm::initialize()
{
    this->tick();
}

bool ChannelPointsFarm::isChannelFarmed(const QString &channelName) const
{
    const auto &raw = this->channels_.raw();
    return std::ranges::any_of(raw, [&](const QString &name) {
        return name.compare(channelName, Qt::CaseInsensitive) == 0;
    });
}

void ChannelPointsFarm::addChannel(const QString &channelName)
{
    if (!this->isChannelFarmed(channelName))
    {
        this->channels_.append(channelName);
    }
}

void ChannelPointsFarm::removeChannel(const QString &channelName)
{
    const auto &raw = this->channels_.raw();
    for (size_t i = 0; i < raw.size(); i++)
    {
        if (raw.at(i).compare(channelName, Qt::CaseInsensitive) == 0)
        {
            this->channels_.removeAt(static_cast<int>(i));
            return;
        }
    }
}

ChannelPointsFarmModel *ChannelPointsFarm::createModel(QObject *parent)
{
    auto *model = new ChannelPointsFarmModel(parent);
    model->initialize(&this->channels_);
    return model;
}

void ChannelPointsFarm::tick()
{
    if (!getSettings()->farmChannelPoints)
    {
        return;
    }

    const auto max =
        std::clamp(getSettings()->maxFarmedChannels.getValue(), 0, MAX_FARMED);
    if (max <= 0)
    {
        return;
    }

    auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (!account || account->isAnon())
    {
        return;
    }
    const auto token = account->getOAuthToken();
    const auto userId = account->getUserId();
    if (token.isEmpty() || userId.isEmpty())
    {
        return;
    }

    // Gather the channels currently open in Bajerino that are live, keyed by
    // lower-cased login so the priority list matches case-insensitively.
    QHash<QString, WatchTarget> liveOpen;
    QStringList liveOrder;  // iteration order, for the fallback fill
    getApp()->getTwitch()->forEachChannel([&](const ChannelPtr &chan) {
        auto *tc = dynamic_cast<TwitchChannel *>(chan.get());
        if (tc == nullptr || !tc->isLive())
        {
            return;
        }

        QString broadcastId;
        {
            auto status = tc->accessStreamStatus();
            if (!status->live)
            {
                return;
            }
            broadcastId = status->streamId;
        }
        const auto channelId = tc->roomId();
        const auto login = tc->getName();
        const auto key = login.toLower();
        if (broadcastId.isEmpty() || channelId.isEmpty() || login.isEmpty() ||
            liveOpen.contains(key))
        {
            return;
        }

        liveOpen.insert(key, WatchTarget{
                                 .login = login,
                                 .channelId = channelId,
                                 .broadcastId = broadcastId,
                             });
        liveOrder.push_back(key);
    });

    // Priority list first (in order), then fill any remaining slots with the
    // other open & live channels. Twitch only credits a couple of streams at
    // once, hence the cap.
    QList<WatchTarget> targets;
    QSet<QString> pickedKeys;
    const auto pick = [&](const QString &key) {
        if (targets.size() >= max || pickedKeys.contains(key))
        {
            return;
        }
        auto it = liveOpen.constFind(key);
        if (it != liveOpen.constEnd())
        {
            targets.push_back(*it);
            pickedKeys.insert(key);
        }
    };
    for (const auto &login : this->channels_.raw())
    {
        pick(login.toLower());
    }
    for (const auto &key : liveOrder)
    {
        pick(key);
    }

    for (const auto &target : targets)
    {
        this->ensureSpadeUrl(target.login,
                             [target, userId, token](const QString &spadeUrl) {
                                 ChannelPointsFarm::postMinuteWatched(
                                     target, spadeUrl, userId, token);
                             });
    }
}

void ChannelPointsFarm::ensureSpadeUrl(
    const QString &login,
    const std::function<void(const QString &)> &onResolved)
{
    auto entryIt = this->spadeCache_.find(login);
    if (entryIt == this->spadeCache_.end())
    {
        entryIt = this->spadeCache_.insert(login, SpadeEntry{});
    }
    auto &entry = *entryIt;
    const auto now = QDateTime::currentDateTimeUtc();
    if (!entry.url.isEmpty() && entry.fetchedAt.isValid() &&
        entry.fetchedAt.secsTo(now) < SPADE_TTL_SECONDS)
    {
        onResolved(entry.url);
        return;
    }
    if (entry.fetching)
    {
        return;  // a resolution is already in flight
    }
    entry.fetching = true;

    NetworkRequest("https://www.twitch.tv/" + login)
        .onSuccess([this, login, onResolved](const NetworkResult &result) {
            const auto body = QString::fromUtf8(result.getData());
            static const QRegularExpression settingsRe(
                uR"((https://(?:static\.twitchcdn\.net|assets\.twitch\.tv)/config/settings\.[0-9a-f]+\.js))"_s);
            const auto settingsMatch = settingsRe.match(body);
            if (!settingsMatch.hasMatch())
            {
                this->markSpadeIdle(login);
                qCDebug(chatterinoTwitch)
                    << "[PointsFarm] Could not find settings URL for" << login;
                return;
            }

            NetworkRequest(settingsMatch.captured(1))
                .onSuccess(
                    [this, login, onResolved](const NetworkResult &result2) {
                        const auto js = QString::fromUtf8(result2.getData());
                        static const QRegularExpression spadeRe(
                            uR"RE("spade_url"\s*:\s*"([^"]+)")RE"_s);
                        const auto spadeMatch = spadeRe.match(js);

                        auto cachedIt = this->spadeCache_.find(login);
                        if (cachedIt == this->spadeCache_.end())
                        {
                            return;
                        }
                        auto &cached = *cachedIt;
                        cached.fetching = false;
                        if (!spadeMatch.hasMatch())
                        {
                            qCDebug(chatterinoTwitch)
                                << "[PointsFarm] Could not find spade URL for"
                                << login;
                            return;
                        }
                        cached.url = spadeMatch.captured(1);
                        cached.fetchedAt = QDateTime::currentDateTimeUtc();
                        onResolved(cached.url);
                    })
                .onError([this, login](const NetworkResult & /*result*/) {
                    this->markSpadeIdle(login);
                })
                .execute();
        })
        .onError([this, login](const NetworkResult & /*result*/) {
            this->markSpadeIdle(login);
        })
        .execute();
}

void ChannelPointsFarm::markSpadeIdle(const QString &login)
{
    auto it = this->spadeCache_.find(login);
    if (it != this->spadeCache_.end())
    {
        it->fetching = false;
    }
}

void ChannelPointsFarm::postMinuteWatched(const WatchTarget &target,
                                          const QString &spadeUrl,
                                          const QString &userId,
                                          const QString &token)
{
    QJsonObject properties;
    properties.insert("channel_id", target.channelId);
    properties.insert("broadcast_id", target.broadcastId);
    properties.insert("player", "site");
    properties.insert("user_id", userId.toLongLong());
    properties.insert("live", true);
    properties.insert("channel", target.login);

    QJsonObject event;
    event.insert("event", "minute-watched");
    event.insert("properties", properties);

    QJsonArray events;
    events.append(event);

    const auto encoded =
        QJsonDocument(events).toJson(QJsonDocument::Compact).toBase64();
    const QByteArray body = "data=" + QUrl::toPercentEncoding(encoded);

    NetworkRequest(spadeUrl.toStdString(), NetworkRequestType::Post)
        .useProxy()
        .timeout(10 * 1000)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .header("Authorization", "OAuth " + token)
        .payload(body)
        .onSuccess([target](const NetworkResult & /*result*/) {
            qCDebug(chatterinoTwitch)
                << "[PointsFarm] Sent watch event for" << target.login;
        })
        .onError([target](const NetworkResult &result) {
            qCDebug(chatterinoTwitch)
                << "[PointsFarm] Watch event failed for" << target.login << ":"
                << result.formatError();
        })
        .execute();
}

}  // namespace chatterino
