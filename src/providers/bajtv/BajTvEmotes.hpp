#pragma once

#include "common/Aliases.hpp"
#include "common/Atomic.hpp"

#include <boost/unordered/unordered_flat_map.hpp>
#include <QJsonObject>

#include <memory>
#include <optional>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;
class EmoteMap;
class Channel;

/// Maps a Twitch User ID to a list of badge IDs
using BajTvChannelBadgeMap =
    boost::unordered::unordered_flat_map<QString, std::vector<int>>;

namespace bajtv::detail {

    EmoteMap parseChannelEmotes(const QJsonObject &jsonRoot);

    /**
     * Parse the `user_badge_ids` into a map of User IDs -> Badge IDs
     */
    // BajTvChannelBadgeMap parseChannelBadges(const QJsonObject &badgeRoot);

}  // namespace bajtv::detail

class BajTvEmotes final
{
public:
    BajTvEmotes();

    std::shared_ptr<const EmoteMap> emotes() const;
    std::optional<EmotePtr> emote(const EmoteName &name) const;
    void loadEmotes();
    void setEmotes(std::shared_ptr<const EmoteMap> emotes);
    static void loadChannel(const std::weak_ptr<Channel> &channel,
                            const QString &channelID,
                            std::function<void(EmoteMap &&)> emoteCallback,
                            bool manualRefresh);

private:
    Atomic<std::shared_ptr<const EmoteMap>> global_;
};

}  // namespace chatterino
