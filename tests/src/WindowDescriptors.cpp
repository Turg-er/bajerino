// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/WindowDescriptors.hpp"

#include "Test.hpp"
#include "util/QMagicEnum.hpp"

#include <QJsonObject>

namespace chatterino {
namespace {

TEST(WindowDescriptors, TwitchChannelModeRoundTrip)
{
    for (const auto mode :
         {TwitchChannelMode::Authenticated, TwitchChannelMode::AnonymousRead,
          TwitchChannelMode::BajerinoAnonymous})
    {
        SplitDescriptor descriptor;
        descriptor.type_ = "twitch";
        descriptor.channelName_ = "pajlada";
        descriptor.twitchChannelMode_ = mode;

        const auto json = descriptor.toJson();
        const auto data = json["data"].toObject();
        EXPECT_EQ(data["twitchChannelMode"].toString(),
                  qmagicenum::enumNameString(mode));
        EXPECT_FALSE(data.contains("anonymousOverride"));

        const auto decoded = SplitDescriptor::loadFromJSON(json);
        EXPECT_EQ(decoded.twitchChannelMode_, mode);
    }
}

TEST(WindowDescriptors, FollowDefaultOmitsTwitchChannelMode)
{
    SplitDescriptor descriptor;
    descriptor.type_ = "twitch";
    descriptor.channelName_ = "pajlada";

    const auto json = descriptor.toJson();
    EXPECT_FALSE(json["data"].toObject().contains("twitchChannelMode"));
    EXPECT_EQ(SplitDescriptor::loadFromJSON(json).twitchChannelMode_,
              std::nullopt);
}

TEST(WindowDescriptors, LoadsLegacyAnonymousOverride)
{
    const auto makeJson = [](bool anonymous) {
        return QJsonObject{
            {"data",
             QJsonObject{
                 {"type", "twitch"},
                 {"name", "pajlada"},
                 {"anonymousOverride", anonymous},
             }},
        };
    };

    EXPECT_EQ(SplitDescriptor::loadFromJSON(makeJson(true)).twitchChannelMode_,
              TwitchChannelMode::BajerinoAnonymous);
    EXPECT_EQ(SplitDescriptor::loadFromJSON(makeJson(false)).twitchChannelMode_,
              TwitchChannelMode::Authenticated);
}

TEST(WindowDescriptors, ChildTwitchChannelModeRoundTripAndMigration)
{
    ChildChannelDescriptor descriptor{
        .platform = "twitch",
        .channelName = "pajlada",
        .twitchChannelMode = TwitchChannelMode::AnonymousRead,
    };

    const auto json = descriptor.toJson();
    EXPECT_EQ(ChildChannelDescriptor::fromJson(json).twitchChannelMode,
              TwitchChannelMode::AnonymousRead);

    const QJsonObject legacy{
        {"platform", "twitch"},
        {"channel", "pajlada"},
        {"anonymousOverride", true},
    };
    EXPECT_EQ(ChildChannelDescriptor::fromJson(legacy).twitchChannelMode,
              TwitchChannelMode::BajerinoAnonymous);

    auto legacyAuthenticated = legacy;
    legacyAuthenticated["anonymousOverride"] = false;
    EXPECT_EQ(
        ChildChannelDescriptor::fromJson(legacyAuthenticated).twitchChannelMode,
        TwitchChannelMode::Authenticated);

    QJsonObject followDefault{
        {"platform", "twitch"},
        {"channel", "pajlada"},
    };
    EXPECT_EQ(ChildChannelDescriptor::fromJson(followDefault).twitchChannelMode,
              std::nullopt);
    const ChildChannelDescriptor defaultDescriptor{
        .platform = "twitch",
        .channelName = "pajlada",
    };
    EXPECT_FALSE(defaultDescriptor.toJson().contains("twitchChannelMode"));
}

}  // namespace
}  // namespace chatterino
