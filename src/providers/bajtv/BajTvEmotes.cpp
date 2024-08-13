
#include "providers/bajtv/BajTvEmotes.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/bajtv/BajTvUtil.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"

#include <qlogging.h>

namespace {

using namespace chatterino;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
const auto &LOG = chatterinoFfzemotes;

const QString CHANNEL_HAS_NO_EMOTES(
    "This channel has no Baj TV channel emotes.");

// FFZ doesn't provide any data on the size for room badges,
// so we assume 18x18 (same as a Twitch badge)
constexpr QSize BASE_BADGE_SIZE(18, 18);

Url getEmoteLink(const QJsonArray &urls, const QString &emoteScale)
{
    QJsonValue emoteUrl;
    for (const auto url : urls)
    {
        if (url.toObject()["type"] == emoteScale)
        {
            emoteUrl = url.toObject()["url"];
            break;
        }
    }
    if (emoteUrl.isUndefined() || emoteUrl.isNull())
    {
        return {""};
    }
    std::cout << emoteUrl.toString().toStdString() << std::endl;
    assert(emoteUrl.isString());

    return parseBajTvUrl(emoteUrl.toString());
}

void fillInEmoteData(const QJsonObject &emote, const QJsonArray &urls,
                     const EmoteName &name, const QString &tooltip,
                     Emote &emoteData)
{
    auto url1x = getEmoteLink(urls, "1");
    auto url2x = getEmoteLink(urls, "2");
    auto url3x = getEmoteLink(urls, "4");
    QSize baseSize(emote["width"].toInt(28), emote["height"].toInt(28));

    //, code, tooltip
    emoteData.name = name;
    emoteData.images = ImageSet{
        Image::fromUrl(url1x, 1, baseSize),
        url2x.string.isEmpty() ? Image::getEmpty()
                               : Image::fromUrl(url2x, 0.5, baseSize * 2),
        url3x.string.isEmpty() ? Image::getEmpty()
                               : Image::fromUrl(url3x, 0.25, baseSize * 4)};
    emoteData.tooltip = {tooltip};
}

EmotePtr cachedOrMake(Emote &&emote, const EmoteId &id)
{
    static std::unordered_map<EmoteId, std::weak_ptr<const Emote>> cache;
    static std::mutex mutex;

    return cachedOrMakeEmotePtr(std::move(emote), cache, mutex, id);
}

void parseEmoteSetInto(const QJsonObject &emoteSet, const QString &kind,
                       EmoteMap &map)
{
    for (const auto emoteRef : emoteSet["emoticons"].toArray())
    {
        const auto emoteJson = emoteRef.toObject();

        // margins
        auto id = EmoteId{QString::number(emoteJson["id"].toInt())};
        auto name = EmoteName{emoteJson["name"].toString()};
        auto author = EmoteAuthor{emoteJson["owner"].toString()};
        auto urls = emoteJson["urls"].toArray();
        if (emoteJson["animated"].isObject())
        {
            // prefer animated images if available
            urls = emoteJson["animated"].toArray();
        }

        Emote emote;
        fillInEmoteData(emoteJson, urls, name,
                        QString("%1<br>%2 Baj TV Emote<br>By: %3")
                            .arg(name.string, kind, author.string),
                        emote);
        emote.homePage = Url{QString("http://localhost:3000/emoticon/%1-%2")
                                 .arg(id.string)
                                 .arg(name.string)};

        map[name] = cachedOrMake(std::move(emote), id);
    }
}

EmoteMap parseGlobalEmotes(const QJsonObject &jsonRoot)
{
    // Load default sets from the `default_sets` object
    std::unordered_set<int> defaultSets{};
    auto jsonDefaultSets = jsonRoot["default_sets"].toArray();
    for (auto jsonDefaultSet : jsonDefaultSets)
    {
        defaultSets.insert(jsonDefaultSet.toInt());
    }

    auto emotes = EmoteMap();

    for (const auto emoteSetRef : jsonRoot["sets"].toObject())
    {
        const auto emoteSet = emoteSetRef.toObject();
        auto emoteSetID = emoteSet["id"].toInt();
        if (!defaultSets.contains(emoteSetID))
        {
            qCDebug(LOG) << "Skipping global emote set" << emoteSetID
                         << "as it's not part of the default sets";
            continue;
        }

        parseEmoteSetInto(emoteSet, "Global", emotes);
    }

    return emotes;
}

}  // namespace

namespace chatterino {
using namespace bajtv::detail;

EmoteMap bajtv::detail::parseChannelEmotes(const QJsonObject &jsonRoot)
{
    auto emotes = EmoteMap();

    for (const auto emoteSetRef : jsonRoot["sets"].toObject())
    {
        parseEmoteSetInto(emoteSetRef.toObject(), "Channel", emotes);
    }

    return emotes;
}

BajTvEmotes::BajTvEmotes()
    : global_(std::make_shared<EmoteMap>())
{
}

std::shared_ptr<const EmoteMap> BajTvEmotes::emotes() const
{
    return this->global_.get();
}

std::optional<EmotePtr> BajTvEmotes::emote(const EmoteName &name) const
{
    auto emotes = this->global_.get();
    auto it = emotes->find(name);
    if (it != emotes->end())
    {
        return it->second;
    }
    return std::nullopt;
}

void BajTvEmotes::loadEmotes()
{
    if (!Settings::instance().enableBajTVGlobalEmotes)
    {
        this->setEmotes(EMPTY_EMOTE_MAP);
        return;
    }

    QString url("http://localhost:3000/defaultsets");

    NetworkRequest(url)

        .timeout(30000)
        .onSuccess([this](auto result) {
            auto parsedSet = parseGlobalEmotes(result.parseJson());
            this->setEmotes(std::make_shared<EmoteMap>(std::move(parsedSet)));
        })
        .execute();
}

void BajTvEmotes::setEmotes(std::shared_ptr<const EmoteMap> emotes)
{
    this->global_.set(std::move(emotes));
}
void BajTvEmotes::loadChannel(const std::weak_ptr<Channel> &channel,
                              const QString &channelID,
                              std::function<void(EmoteMap &&)> emoteCallback,
                              bool manualRefresh)
{
    qCDebug(LOG) << "Reload Baj TV Channel Emotes for channel" << channelID;

    NetworkRequest("http://localhost:3000/channel-emotes/" + channelID)

        .timeout(20000)
        .onSuccess([emoteCallback = std::move(emoteCallback), channel,
                    manualRefresh, channelID](const auto &result) {
            const auto json = result.parseJson();

            auto emoteMap = parseChannelEmotes(json);

            bool hasEmotes = !emoteMap.empty();

            emoteCallback(std::move(emoteMap));
            // channelBadgesCallback(std::move(channelBadges));
            if (auto shared = channel.lock(); manualRefresh)
            {
                if (hasEmotes)
                {
                    shared->addSystemMessage(
                        QStringLiteral("Baj TV channel emotes reloaded. Id: %1")
                            .arg(channelID));
                }
                else
                {
                    shared->addSystemMessage(CHANNEL_HAS_NO_EMOTES);
                }
            }
        })
        .onError([channelID, channel, manualRefresh](const auto &result) {
            auto shared = channel.lock();
            if (!shared)
            {
                return;
            }

            if (result.status() == 404)
            {
                // User does not have any FFZ emotes
                if (manualRefresh)
                {
                    shared->addSystemMessage(CHANNEL_HAS_NO_EMOTES);
                }
            }
            else
            {
                // TODO: Auto retry in case of a timeout, with a delay
                auto errorString = result.formatError();
                qCWarning(LOG) << "Error fetching Baj TV emotes for channel"
                               << channelID << ", error" << errorString;
                shared->addSystemMessage(
                    QStringLiteral("Failed to fetch Baj TV channel "
                                   "emotes. (Error: %1) (Id: %2)")
                        .arg(errorString, channelID));
            }
        })
        .execute();
}

}  // namespace chatterino
