// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QNetworkProxy>

#include <optional>

namespace chatterino {

class Env;

/** This class manipulates the global network configuration (e.g. proxies). */
class NetworkConfigurationProvider
{
public:
    /** This class should never be instantiated. */
    NetworkConfigurationProvider() = delete;

    /**
     * Applies the configuration requested from the environment variables.
     *
     * Currently a proxy is applied if configured unless selective Twitch API
     * proxying is enabled.
     */
    static void applyFromEnv(const Env &env);

    [[nodiscard]] static std::optional<QNetworkProxy> proxyFromEnv(
        const Env &env);
};

}  // namespace chatterino
