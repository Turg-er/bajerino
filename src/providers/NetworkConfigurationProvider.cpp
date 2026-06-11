// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/NetworkConfigurationProvider.hpp"

#include "common/Env.hpp"
#include "common/QLogging.hpp"

#include <QNetworkProxy>
#include <QUrl>

namespace {
/**
 * Creates a QNetworkProxy from a given URL.
 *
 * Creates an HTTP proxy by default, a Socks5 will be created if the scheme is 'socks5'.
 */
QNetworkProxy createProxyFromUrl(const QUrl &url)
{
    QNetworkProxy proxy;
    proxy.setHostName(url.host(QUrl::FullyEncoded));
    proxy.setUser(url.userName(QUrl::FullyEncoded));
    proxy.setPassword(url.password(QUrl::FullyEncoded));
    proxy.setPort(url.port(1080));

    if (url.scheme().compare(QStringLiteral("socks5"), Qt::CaseInsensitive) ==
        0)
    {
        proxy.setType(QNetworkProxy::Socks5Proxy);
    }
    else
    {
        proxy.setType(QNetworkProxy::HttpProxy);
        if (!proxy.user().isEmpty() || !proxy.password().isEmpty())
        {
            // for some reason, Qt doesn't set the Proxy-Authorization header
            const auto auth = proxy.user() + ":" + proxy.password();
            const auto base64 = auth.toUtf8().toBase64();
            proxy.setRawHeader("Proxy-Authorization",
                               QByteArray("Basic ").append(base64));
        }
    }

    return proxy;
}

std::optional<QNetworkProxy> proxyFromUrl(const QString &url)
{
    auto proxyUrl = QUrl(url);
    if (!proxyUrl.isValid() || proxyUrl.isEmpty())
    {
        qCDebug(chatterinoNetwork)
            << "Invalid or empty proxy url: " << proxyUrl;
        return std::nullopt;
    }

    return createProxyFromUrl(proxyUrl);
}

}  // namespace

namespace chatterino {

std::optional<QNetworkProxy> NetworkConfigurationProvider::proxyFromEnv(
    const Env &env)
{
    if (!env.proxyUrl)
    {
        return std::nullopt;
    }

    return proxyFromUrl(*env.proxyUrl);
}

bool NetworkConfigurationProvider::shouldProxy(const Env &env,
                                               ProxyConnection connection)
{
    switch (connection)
    {
        case ProxyConnection::AuthedTwitch:
            // Authenticated Twitch connections are proxied in every mode.
            return true;

        case ProxyConnection::Twitch:
            // Other Twitch connections are proxied in global and
            // BAJERINO_PROXY_TWITCH modes, but not in authed-only mode.
            return !env.proxyTwitchApiOnly;

        case ProxyConnection::ThirdParty:
            // Third-party connections are only proxied in global mode.
            return !env.proxyTwitchApiOnly && !env.proxyTwitch;
    }

    return false;
}

std::optional<WebSocketProxyOptions>
    NetworkConfigurationProvider::webSocketProxyFromEnv(
        const Env &env, ProxyConnection connection)
{
    if (!NetworkConfigurationProvider::shouldProxy(env, connection))
    {
        return std::nullopt;
    }

    const auto proxy = NetworkConfigurationProvider::proxyFromEnv(env);
    if (!proxy)
    {
        return std::nullopt;
    }

    WebSocketProxyOptions options;
    switch (proxy->type())
    {
        case QNetworkProxy::HttpProxy:
            options.type = WebSocketProxyType::Http;
            break;

        case QNetworkProxy::Socks5Proxy:
            options.type = WebSocketProxyType::Socks5;
            break;

        default:
            qCWarning(chatterinoNetwork)
                << "Unsupported proxy type for WebSocket connection" << *proxy;
            return std::nullopt;
    }

    options.host = proxy->hostName();
    options.port = proxy->port();
    options.user = proxy->user();
    options.password = proxy->password();
    return options;
}

void NetworkConfigurationProvider::applyFromEnv(const Env &env)
{
    if (env.proxyTwitchApiOnly || env.proxyTwitch)
    {
        if (!env.proxyUrl)
        {
            qCWarning(chatterinoNetwork)
                << "Selective proxying (BAJERINO_PROXY_TWITCH or "
                   "BAJERINO_PROXY_TWITCH_API_ONLY) is enabled but "
                   "CHATTERINO2_PROXY_URL is not set; requests will be made "
                   "directly";
        }
        else
        {
            qCDebug(chatterinoNetwork)
                << "Selective Twitch proxying enabled; skipping global "
                   "application proxy";
        }
        return;
    }

    if (const auto proxy = NetworkConfigurationProvider::proxyFromEnv(env))
    {
        QNetworkProxy::setApplicationProxy(*proxy);
        qCDebug(chatterinoNetwork) << "Set application proxy to" << *proxy;
    }
}

}  // namespace chatterino
