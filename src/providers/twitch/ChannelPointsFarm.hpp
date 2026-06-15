// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "common/SignalVector.hpp"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QTimer>

#include <functional>

namespace chatterino {

class ChannelPointsFarmModel;

/// Keeps the account counted as "watching" channels open in Bajerino by
/// periodically sending Twitch minute-watched events, so Twitch accrues watch
/// points and emits claim-available events (handled by TwitchChannel's
/// auto-claim). This impersonates a viewer, so it is gated behind the
/// (default off) farmChannelPoints setting.
///
/// Twitch only awards watch points on a limited number of streams at once
/// (MAX_FARMED). When more open channels are live than that, the priority list
/// decides who wins: channels earlier in the list first, then any remaining
/// open & live channels fill the leftover slots.
class ChannelPointsFarm final
{
public:
    // Twitch only credits watch points for this many concurrent streams.
    static constexpr int MAX_FARMED = 2;

    ChannelPointsFarm();

    /// Kick off the first watch tick without waiting for the timer.
    void initialize();

    bool isChannelFarmed(const QString &channelName) const;
    void addChannel(const QString &channelName);
    void removeChannel(const QString &channelName);

    ChannelPointsFarmModel *createModel(QObject *parent);

private:
    // A channel selected for watching during the current tick.
    struct WatchTarget {
        QString login;
        QString channelId;    // broadcaster user id
        QString broadcastId;  // current stream id
    };

    struct SpadeEntry {
        QString url;
        QDateTime fetchedAt;
        bool fetching = false;
    };

    void tick();
    void ensureSpadeUrl(const QString &login,
                        const std::function<void(const QString &)> &onResolved);
    /// Clears the in-flight flag for a channel's spade lookup, if cached.
    void markSpadeIdle(const QString &login);
    static void postMinuteWatched(const WatchTarget &target,
                                  const QString &spadeUrl,
                                  const QString &userId, const QString &token);

    QTimer timer_;
    SignalVector<QString> channels_;
    QHash<QString, SpadeEntry> spadeCache_;

    ChatterinoSetting<std::vector<QString>> setting_ = {
        "/moltorino/channelPoints/farmPriority"};
};

}  // namespace chatterino
