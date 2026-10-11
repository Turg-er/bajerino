// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/tiktok/TikTokTypes.hpp"
#include "providers/youtube/YouTubeInnertubeParser.hpp"
#include "providers/youtube/YouTubeTypes.hpp"
#include "Test.hpp"

#include <QJsonDocument>

using namespace chatterino;
using namespace Qt::StringLiterals;

TEST(TikTok, NormalizesHandlesAndProfileLinks)
{
    for (QStringView input : {u" @Creator.Name ", u"tiktok:Creator.Name",
                              u"https://www.tiktok.com/@Creator.Name/live"})
    {
        EXPECT_EQ(normalizeTikTokHandle(input), u"creator.name"_s);
    }
    for (QStringView input :
         {u"name/other", u"trailing.", u"https://example.com/@creator",
          u"https://tiktok.com/@creator?extra=1"})
    {
        EXPECT_FALSE(normalizeTikTokHandle(input));
    }
    EXPECT_EQ(tikTokProfileUrl(u"@creator").toString(),
              u"https://www.tiktok.com/@creator"_s);
}

TEST(YouTube, ParsesChatAndContinuation)
{
    const auto response = QJsonDocument::fromJson(R"({
        "continuationContents": {
            "liveChatContinuation": {
                "actions": [{"addChatItemAction": {"item": {
                    "liveChatTextMessageRenderer": {
                        "id": "message-1",
                        "authorExternalChannelId": "UCabcdefghijklmnopqrstuv",
                        "authorName": {"simpleText": "Viewer"},
                        "message": {"runs": [{"text": "Hello ø"}]},
                        "timestampUsec": "1700000000123000"
                    }
                }}}],
                "continuations": [{"timedContinuationData": {
                    "continuation": "next-page", "timeoutMs": 1000
                }}]
            }
        }
    })")
                              .object();
    auto result = parseYouTubeInnertubeChat(response, u"chat-1");
    ASSERT_TRUE(result.valid) << result.error;
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages[0].kind, YouTubeMessageKind::Text);
    EXPECT_EQ(result.messages[0].text, u"Hello ø"_s);
    EXPECT_EQ(result.messages[0].author.displayName, u"Viewer"_s);
    EXPECT_EQ(result.messages[0].liveChatId, u"chat-1"_s);
    EXPECT_EQ(result.messages[0].publishedAt.toMSecsSinceEpoch(),
              1700000000123);
    EXPECT_EQ(result.nextContinuation, u"next-page"_s);
    EXPECT_EQ(result.serverTimeout, std::chrono::milliseconds(1000));
    EXPECT_FALSE(result.ended);
}

TEST(YouTube, RejectsMalformedContinuation)
{
    const auto response = QJsonDocument::fromJson(R"({
        "continuationContents": {"liveChatContinuation": {
            "actions": [], "continuations": "invalid"
        }}
    })")
                              .object();
    const auto result = parseYouTubeInnertubeChat(response, u"chat-1");
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(YouTube, BoundsDeduplicationAndReplayHistory)
{
    YouTubeMessageDeduper deduper;
    YouTubeReplayBuffer history(2);
    YouTubeMessage message;
    message.liveChatId = u"chat-1"_s;
    message.kind = YouTubeMessageKind::Text;
    for (int i = 0; i < 3; ++i)
    {
        message.id = QString::number(i);
        message.text = u"message "_s + message.id;
        EXPECT_TRUE(deduper.accept(message));
        EXPECT_FALSE(deduper.accept(message));
        history.append(message);
    }
    const auto messages = history.snapshot();
    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.front().id, u"1"_s);
    EXPECT_EQ(messages.back().id, u"2"_s);
    EXPECT_EQ(deduper.size(), 3);
}
