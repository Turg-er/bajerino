// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"

#include <QNetworkProxy>

#include <cstdint>
#include <optional>

namespace chatterino {

class Env;

/**
 * The kind of connection being proxied, which together with the environment
 * decides whether a proxy is applied. See NetworkConfigurationProvider for the
 * proxy modes (global, BAJERINO_PROXY_TWITCH, BAJERINO_PROXY_TWITCH_API_ONLY).
 */
enum class ProxyConnection : std::uint8_t {
    /// Authenticated Twitch connections (Twitch HTTP API, PubSub). Proxied in
    /// every proxy mode.
    AuthedTwitch,
    /// Other Twitch connections (IRC, EventSub). Proxied in global and
    /// BAJERINO_PROXY_TWITCH modes, but not BAJERINO_PROXY_TWITCH_API_ONLY,
    /// since they are used anonymously there.
    Twitch,
    /// Third-party connections (7TV, BTTV, Kick). Proxied only in global mode
    /// (proxy everything).
    ThirdParty,
};

/** This class manipulates the global network configuration (e.g. proxies). */
class NetworkConfigurationProvider
{
public:
    /** This class should never be instantiated. */
    NetworkConfigurationProvider() = delete;

    /**
     * Applies the configuration requested from the environment variables.
     *
     * A global proxy is applied if configured, unless a selective proxying mode
     * (BAJERINO_PROXY_TWITCH or BAJERINO_PROXY_TWITCH_API_ONLY) is enabled.
     */
    static void applyFromEnv(const Env &env);

    [[nodiscard]] static std::optional<QNetworkProxy> proxyFromEnv(
        const Env &env);

    /**
     * Whether a connection of the given kind should be proxied, per the
     * configured proxy mode. This does not consider whether a proxy URL is
     * actually set (see proxyFromEnv).
     */
    [[nodiscard]] static bool shouldProxy(const Env &env,
                                          ProxyConnection connection);

    /**
     * Builds WebSocket proxy options from the environment for the given
     * connection kind. The asio-based WebSocket stacks do not honor the global
     * Qt application proxy, so they must be configured explicitly through these
     * options. Returns nullopt when the connection should not be proxied or no
     * proxy is configured.
     */
    [[nodiscard]] static std::optional<WebSocketProxyOptions>
        webSocketProxyFromEnv(const Env &env, ProxyConnection connection);
};

}  // namespace chatterino
