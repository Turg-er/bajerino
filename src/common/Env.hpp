// SPDX-FileCopyrightText: 2019 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <cstdint>
#include <optional>

namespace chatterino {

namespace env {

constexpr const char *LOG_TO_FILE = "CHATTERINO_LOG_TO_FILE";

bool readBool(const char *envName, bool defaultValue);

}  // namespace env

class Env
{
    Env();

public:
    static const Env &get();

    const QString recentMessagesApiUrl;
    const QString linkResolverUrl;
    /// Proxy all Twitch connections (HTTP API, PubSub, IRC, EventSub) while
    /// leaving third-party services direct. BAJERINO_PROXY_TWITCH
    const bool proxyTwitch;
    /// Proxy only authenticated Twitch connections while anonymous IRC and
    /// third-party services stay direct. BAJERINO_PROXY_TWITCH_AUTHED_ONLY
    const bool proxyTwitchAuthedOnly;
    const QString twitchServerHost;
    const uint16_t twitchServerPort;
    const bool twitchServerSecure;
    const std::optional<QString> proxyUrl;

    /// Log output from the application to a file at the given path
    const QString logToFile;
};

}  // namespace chatterino
