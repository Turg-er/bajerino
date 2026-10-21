// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchIrc.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/QLogging.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "providers/twitch/TwitchEmotes.hpp"
#include "util/IrcHelpers.hpp"

#include "util/Helpers.hpp"

#include <QCache>
#include <span>
#include <utility>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace {

using namespace chatterino;

QString sharedBadgeString(QString value, QCache<QString, QString> &cache)
{
    if (value.isEmpty() || value.size() > 128)
    {
        return value;
    }
    if (const auto *existing = cache.object(value))
    {
        return *existing;
    }
    cache.insert(value, new QString(value));
    return value;
}

std::pair<QString, QString> sharedBadgePair(const QString &badge)
{
    if (badge.size() > 128)
    {
        return slashKeyValue(badge);
    }
    using Pair = std::pair<QString, QString>;
    static QCache<QString, Pair> pairs(512);
    static QCache<QString, QString> strings(512);
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (const auto *existing = pairs.object(badge))
    {
        return *existing;
    }
    auto pair = slashKeyValue(badge);
    pair.first = sharedBadgeString(std::move(pair.first), strings);
    pair.second = sharedBadgeString(std::move(pair.second), strings);
    pairs.insert(badge, new Pair(pair));
    return pair;
}

void appendTwitchEmoteOccurrences(QStringView emote,
                                  std::vector<TwitchEmoteOccurrence> &out,
                                  std::span<const int> codepointToUtf16Idx,
                                  QStringView originalMessage,
                                  int messageOffset)
{
    auto *app = getApp();

    auto [idRef, ranges] = splitOnce(emote, u':');
    if (ranges.empty())
    {
        return;
    }
    // FIXME: Add an EmoteIdView.
    auto id = EmoteId{idRef.toString()};

    for (const auto occurrence : ranges.tokenize(u','))
    {
        auto [fromStr, toStr] = splitOnce(occurrence, u'-');
        bool fromOk = false;
        bool toOk = false;
        int from = fromStr.toInt(&fromOk);
        int to = toStr.toInt(&toOk);
        if (!fromOk || !toOk || messageOffset < 0)
        {
            qCDebug(chatterinoTwitch) << "Invalid emote range:" << occurrence;
            continue;
        }
        if (from > to || from < messageOffset)
        {
            qCDebug(chatterinoTwitch)
                << "Out of bounds emote range:" << occurrence
                << "offset:" << messageOffset;
            continue;
        }
        to -= messageOffset;
        from -= messageOffset;
        if (std::cmp_greater_equal(to, codepointToUtf16Idx.size() - 1))
        {
            qCDebug(chatterinoTwitch)
                << "Out of bounds emote range:" << occurrence
                << "max-codepoints:" << codepointToUtf16Idx.size();
            return;
        }

        auto start = codepointToUtf16Idx[from];
        auto end = codepointToUtf16Idx[to + 1] - 1;
        assert(start <= end && end < originalMessage.length() &&
               "Bad codepointToUtf16Idx list");

        auto name = EmoteName{
            originalMessage.sliced(start, end - start + 1).toString()};
        auto ptr =
            app->getEmotes()->getTwitchEmotes()->getOrCreateEmote(id, name);
        if (!ptr)
        {
            qCDebug(chatterinoTwitch) << "Invalid emote:" << id.string;
            continue;
        }

        out.emplace_back(TwitchEmoteOccurrence{
            .start = start,
            .end = end,
            .ptr = ptr,
            .name = name,
        });
    }
}

void appendTwitchGifOccurrences(const QString &gifToken,
                                std::vector<TwitchGifOccurrence> &vec,
                                const std::vector<int> &correctPositions,
                                const QString &originalMessage,
                                int messageOffset)
{
    const auto parts = gifToken.split('|', Qt::KeepEmptyParts);
    if (parts.size() != 3)
    {
        return;
    }

    const auto coords = parts.at(0).split('-', Qt::KeepEmptyParts);
    if (coords.size() != 2)
    {
        return;
    }

    const auto gifId = parts.at(1);
    const auto gifUrl = parts.at(2);
    if (gifId.isEmpty() || gifUrl.isEmpty())
    {
        return;
    }

    if (messageOffset < 0)
    {
        return;
    }

    bool okFrom = false;
    bool okTo = false;
    const auto rawFrom = coords.at(0).toInt(&okFrom);
    const auto rawTo = coords.at(1).toInt(&okTo);
    if (!okFrom || !okTo || rawFrom < 0 || rawTo < 0 ||
        rawFrom < messageOffset || rawTo < messageOffset)
    {
        return;
    }

    const auto from = rawFrom - messageOffset;
    const auto to = rawTo - messageOffset;
    const auto maxPositions = correctPositions.size();
    if (from > to || to >= maxPositions)
    {
        qCDebug(chatterinoTwitch)
            << "GIF coords" << from << "-" << to << "are out of range ("
            << maxPositions << ")";
        return;
    }

    const auto start = correctPositions[from];
    auto end = correctPositions[to];

    if (end + 1 < originalMessage.length() &&
        originalMessage.at(end).isHighSurrogate() &&
        originalMessage.at(end + 1).isLowSurrogate())
    {
        ++end;
    }
    if (start > end || start < 0 || end >= originalMessage.length())
    {
        qCDebug(chatterinoTwitch) << "GIF coords" << from << "-" << to
                                  << "are out of range after offsets ("
                                  << originalMessage.length() << ")";
        return;
    }

    const auto name = EmoteName{originalMessage.mid(start, end - start + 1)};
    QUrl parsedUrl{gifUrl};
    if (!parsedUrl.isValid() || parsedUrl.host().isEmpty() ||
        parsedUrl.scheme().compare(u"https", Qt::CaseInsensitive) != 0 ||
        !parsedUrl.userInfo().isEmpty() || parsedUrl.port(443) != 443)
    {
        return;
    }

    QString downloadUrl = gifUrl;
    QString highQualityUrl = gifUrl;
    const auto host = parsedUrl.host().toLower();
    if (host == u"giphy.com" || host.endsWith(u".giphy.com"))
    {
        auto path = parsedUrl.path();
        if (path.endsWith(u"/giphy.gif", Qt::CaseInsensitive))
        {
            path.chop(10);
            path.append(u"/200w.gif");
            parsedUrl.setPath(path);
            downloadUrl = parsedUrl.toString();
        }
        if (parsedUrl.path().endsWith(u"/200w.gif") &&
            parsedUrl.path().section('/', -2, -2) == gifId &&
            std::ranges::all_of(gifId, [](QChar c) {
                return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                       (c >= u'0' && c <= u'9');
            }))
        {
            downloadUrl =
                QStringLiteral("https://media.giphy.com/media/%1/200w.webp")
                    .arg(gifId);
            highQualityUrl =
                QStringLiteral("https://media.giphy.com/media/%1/giphy.webp")
                    .arg(gifId);
        }
    }

    constexpr int MAX_TWITCH_GIF_FRAME_DIMENSION = 200;
    auto img = Image::fromStreamingGifUrl(
        Url{downloadUrl}, MAX_TWITCH_GIF_FRAME_DIMENSION, 1.0,
        {MAX_TWITCH_GIF_FRAME_DIMENSION, MAX_TWITCH_GIF_FRAME_DIMENSION});
    img->setFrameCacheLifetime(std::chrono::seconds(5));

    constexpr int HIGH_QUALITY_FRAME_DIMENSION = 400;
    auto highQualityImage = Image::fromStreamingGifUrl(
        Url{highQualityUrl}, HIGH_QUALITY_FRAME_DIMENSION, 0.5,
        {HIGH_QUALITY_FRAME_DIMENSION, HIGH_QUALITY_FRAME_DIMENSION});
    highQualityImage->setFrameCacheLifetime(std::chrono::seconds(5));

    auto emote = std::make_shared<Emote>(Emote{
        .name = name,
        .images = ImageSet{std::move(img), std::move(highQualityImage)},
        .tooltip = Tooltip{name.string.toHtmlEscaped() + "<br>Twitch GIF"},
        .homePage = Url{gifUrl},
        .id = EmoteId{gifId},
    });

    vec.push_back(TwitchGifOccurrence{
        start,
        end,
        std::move(emote),
        name,
        gifId,
        gifUrl,
    });
}

}

namespace chatterino {

std::unordered_map<QString, QString> parseBadgeInfoTag(Communi::TagsRef tags)
{
    std::unordered_map<QString, QString> infoMap;

    auto infoIt = tags.get("badge-info");
    if (!infoIt)
    {
        return infoMap;
    }

    auto info = infoIt->split(',', Qt::SkipEmptyParts);

    for (const QString &badge : info)
    {
        infoMap.emplace(sharedBadgePair(badge));
    }

    return infoMap;
}

std::vector<TwitchBadge> parseBadgeTag(Communi::TagsRef tags,
                                       const QString &tagName)
{
    std::vector<TwitchBadge> b;

    auto badgesIt = tags.get(tagName);
    if (!badgesIt)
    {
        return b;
    }

    auto badges = badgesIt->split(',', Qt::SkipEmptyParts);

    for (const QString &badge : badges)
    {
        if (!badge.contains('/'))
        {
            continue;
        }

        auto pair = sharedBadgePair(badge);
        b.emplace_back(TwitchBadge{pair.first, pair.second});
    }

    return b;
}

std::vector<TwitchEmoteOccurrence> parseTwitchEmotes(Communi::TagsRef tags,
                                                     QStringView content,
                                                     int messageOffset)
{

    std::vector<TwitchEmoteOccurrence> twitchEmotes;

    auto emotesTag = tags.get("emotes");

    if (!emotesTag)
    {
        return twitchEmotes;
    }

    std::vector<int> correctPositions;
    correctPositions.reserve(content.size() + 1);
    for (int i = 0; i < content.size(); ++i)
    {
        if (!content.at(i).isLowSurrogate())
        {
            correctPositions.push_back(i);
        }
    }
    correctPositions.push_back(static_cast<int>(content.size()));
    for (const auto emote : emotesTag->tokenize(u'/'))
    {
        appendTwitchEmoteOccurrences(emote, twitchEmotes, correctPositions,
                                     content, messageOffset);
    }

    return twitchEmotes;
}

std::vector<TwitchGifOccurrence> parseTwitchGifs(Communi::TagsRef tags,
                                                 const QString &content,
                                                 int messageOffset)
{
    std::vector<TwitchGifOccurrence> twitchGifs;

    auto gifsTag = tags.get("gifs");
    if (!gifsTag)
    {
        return twitchGifs;
    }

    const auto gifStrings = (*gifsTag).split(',', Qt::SkipEmptyParts);
    if (gifStrings.isEmpty())
    {
        return twitchGifs;
    }

    std::vector<int> correctPositions;
    for (int i = 0; i < content.size(); ++i)
    {
        if (!content.at(i).isLowSurrogate())
        {
            correctPositions.push_back(i);
        }
    }

    for (const QString &gifToken : gifStrings)
    {
        appendTwitchGifOccurrences(gifToken, twitchGifs, correctPositions,
                                   content, messageOffset);
    }

    std::ranges::sort(twitchGifs, [](const auto &a, const auto &b) {
        return a.start < b.start;
    });

    return twitchGifs;
}

}
