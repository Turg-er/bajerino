// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/websockets/detail/WebSocketConnectionImpl.hpp"

#include "common/QLogging.hpp"
#include "common/Version.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket/ssl.hpp>

using namespace std::literals::string_view_literals;

namespace chatterino::ws::detail {

namespace asio = boost::asio;
namespace beast = boost::beast;

// MARK: WebSocketConnectionHelper

template <typename Derived, typename Inner>
WebSocketConnectionHelper<Derived, Inner>::WebSocketConnectionHelper(
    WebSocketOptions options, int id,
    std::unique_ptr<WebSocketListener> listener, WebSocketPoolImpl *pool,
    asio::io_context &ioc, Stream stream)
    : WebSocketConnection(std::move(options), id, std::move(listener), pool,
                          ioc)
    , stream(std::move(stream))
{
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::post(auto &&fn)
{
    asio::post(this->stream.get_executor(), std::forward<decltype(fn)>(fn));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::run()
{
    auto host = this->targetHost();
    if constexpr (requires { this->derived()->setupStream(host); })
    {
        if (!this->derived()->setupStream(host))
        {
            return;
        }
    }

    auto resolveHost = host;
    auto resolvePort = this->targetPort();
    if (this->options.proxy)
    {
        resolveHost = this->options.proxy->host.toStdString();
        resolvePort = std::to_string(this->options.proxy->port);
    }

    this->resolver.async_resolve(
        resolveHost, resolvePort,
        beast::bind_front_handler(&WebSocketConnectionHelper::onResolve,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::close()
{
    this->post([self{this->shared_from_this()}] {
        self->closeImpl();
    });
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::sendText(const QByteArray &data)
{
    this->post([self{this->shared_from_this()}, data] {
        self->queuedMessages.emplace_back(true, data);
        self->trySend();
    });
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::sendBinary(
    const QByteArray &data)
{
    this->post([self{this->shared_from_this()}, data] {
        self->queuedMessages.emplace_back(false, data);
        self->trySend();
    });
}

template <typename Derived, typename Inner>
Derived *WebSocketConnectionHelper<Derived, Inner>::derived()
{
    return static_cast<Derived *>(this);
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onResolve(
    boost::system::error_code ec,
    const asio::ip::tcp::resolver::results_type &results)
{
    if (ec)
    {
        this->fail(ec, u"resolve");
        return;
    }

    this->resolvedEndpoints = BalancedResolverResults(results);

    this->tryConnect(this->resolvedEndpoints.advanceEntry());
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::tryConnect(
    std::optional<BalancedResolverResults::Entry> entry)
{
    if (!entry)
    {
        this->fail("Ran out of resolved endpoints"sv, u"connect");
        return;
    }

    auto endpoint = entry->endpoint();

    qCDebug(chatterinoWebsocket)
        << *this << "connect to" << endpoint.address().to_string();

    beast::get_lowest_layer(this->stream)
        .expires_after(std::chrono::seconds{30});

    beast::get_lowest_layer(this->stream)
        .async_connect(endpoint,
                       beast::bind_front_handler(
                           &WebSocketConnectionHelper::onTcpHandshake,
                           this->shared_from_this(), *std::move(entry)));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onTcpHandshake(
    const BalancedResolverResults::Entry &entry, boost::system::error_code ec)
{
    const auto &ep = entry.endpoint();

    if (ec)
    {
        qCDebug(chatterinoWebsocket)
            << *this << "error in tcp handshake" << ep.address().to_string()
            << ec.message();

        beast::get_lowest_layer(this->stream).socket().close(ec);
        if (ec)
        {
            qCDebug(chatterinoWebsocket)
                << *this << "closing websocket after error" << ec.message();
        }

        this->tryConnect(this->resolvedEndpoints.advanceEntry());
        return;
    }

    qCDebug(chatterinoWebsocket)
        << *this << "TCP handshake done" << ep.address().to_string();

    this->resolvedEndpoints = {};

    if (this->options.proxy)
    {
        switch (this->options.proxy->type)
        {
            case WebSocketProxyType::Http:
                this->doHttpProxyHandshake();
                return;

            case WebSocketProxyType::Socks5:
                this->doSocksProxyHandshake();
                return;
        }
    }

    this->options.url.setPort(ep.port());
    this->derived()->afterTcpHandshake();
}

template <typename Derived, typename Inner>
std::string WebSocketConnectionHelper<Derived, Inner>::targetHost() const
{
    return this->options.url.host(QUrl::FullyEncoded).toStdString();
}

template <typename Derived, typename Inner>
std::string WebSocketConnectionHelper<Derived, Inner>::targetPort() const
{
    return std::to_string(this->options.url.port(Derived::DEFAULT_PORT));
}

template <typename Derived, typename Inner>
std::string WebSocketConnectionHelper<Derived, Inner>::targetHostAndPort() const
{
    return this->targetHost() + ':' + this->targetPort();
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::doHttpProxyHandshake()
{
    const auto target = this->targetHostAndPort();

    this->proxyBuffer.clear();
    this->proxyConnectResponseParser.emplace();
    this->proxyConnectResponseParser->skip(true);
    this->proxyConnectRequest = {};
    this->proxyConnectRequest.version(11);
    this->proxyConnectRequest.method(beast::http::verb::connect);
    this->proxyConnectRequest.target(target);
    this->proxyConnectRequest.set(beast::http::field::host, target);

    if (!this->options.proxy->user.isEmpty() ||
        !this->options.proxy->password.isEmpty())
    {
        const auto auth =
            this->options.proxy->user + ':' + this->options.proxy->password;
        const auto base64 = auth.toUtf8().toBase64().toStdString();
        this->proxyConnectRequest.set(beast::http::field::proxy_authorization,
                                      "Basic " + base64);
    }

    beast::get_lowest_layer(this->stream)
        .expires_after(std::chrono::seconds{30});
    beast::http::async_write(
        beast::get_lowest_layer(this->stream), this->proxyConnectRequest,
        beast::bind_front_handler(&WebSocketConnectionHelper::onHttpProxyWrite,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onHttpProxyWrite(
    boost::system::error_code ec, size_t /*bytesWritten*/)
{
    if (ec)
    {
        this->fail(ec, u"HTTP proxy CONNECT write");
        return;
    }

    assert(this->proxyConnectResponseParser.has_value());
    beast::http::async_read_header(
        beast::get_lowest_layer(this->stream), this->proxyBuffer,
        *this->proxyConnectResponseParser,
        beast::bind_front_handler(&WebSocketConnectionHelper::onHttpProxyRead,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onHttpProxyRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"HTTP proxy CONNECT read");
        return;
    }

    assert(this->proxyConnectResponseParser.has_value());
    const auto &response = this->proxyConnectResponseParser->get();
    if (response.result() != beast::http::status::ok)
    {
        this->fail(std::string("HTTP proxy CONNECT returned ") +
                       std::to_string(response.result_int()),
                   u"HTTP proxy CONNECT");
        return;
    }

    qCDebug(chatterinoWebsocket) << *this << "HTTP proxy CONNECT done";
    this->derived()->afterTcpHandshake();
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::doSocksProxyHandshake()
{
    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.append(char(0x05));
    if (!this->options.proxy->user.isEmpty() ||
        !this->options.proxy->password.isEmpty())
    {
        this->proxyWriteBuffer.append(char(0x02));
        this->proxyWriteBuffer.append(char(0x00));
        this->proxyWriteBuffer.append(char(0x02));
    }
    else
    {
        this->proxyWriteBuffer.append(char(0x01));
        this->proxyWriteBuffer.append(char(0x00));
    }

    beast::get_lowest_layer(this->stream)
        .expires_after(std::chrono::seconds{30});
    boost::asio::async_write(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyWriteBuffer.constData(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(
            &WebSocketConnectionHelper::onSocksGreetingWrite,
            this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksGreetingWrite(
    boost::system::error_code ec, size_t /*bytesWritten*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 greeting write");
        return;
    }

    boost::asio::async_read(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyReadBuffer.data(), 2),
        beast::bind_front_handler(
            &WebSocketConnectionHelper::onSocksGreetingRead,
            this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksGreetingRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 greeting read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x05))
    {
        this->fail("Invalid SOCKS5 greeting version", u"SOCKS5 greeting");
        return;
    }

    const auto method = static_cast<unsigned char>(this->proxyReadBuffer[1]);
    if (method == 0x00)
    {
        this->doSocksConnect();
        return;
    }
    if (method == 0x02)
    {
        this->doSocksAuth();
        return;
    }

    this->fail("SOCKS5 proxy rejected authentication methods",
               u"SOCKS5 greeting");
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::doSocksAuth()
{
    const auto user = this->options.proxy->user.toUtf8();
    const auto password = this->options.proxy->password.toUtf8();
    if (user.size() > 255 || password.size() > 255)
    {
        this->fail("SOCKS5 username/password is too long", u"SOCKS5 auth");
        return;
    }

    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.append(char(0x01));
    this->proxyWriteBuffer.append(char(user.size()));
    this->proxyWriteBuffer.append(user);
    this->proxyWriteBuffer.append(char(password.size()));
    this->proxyWriteBuffer.append(password);

    boost::asio::async_write(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyWriteBuffer.constData(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(&WebSocketConnectionHelper::onSocksAuthWrite,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksAuthWrite(
    boost::system::error_code ec, size_t /*bytesWritten*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 auth write");
        return;
    }

    boost::asio::async_read(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyReadBuffer.data(), 2),
        beast::bind_front_handler(&WebSocketConnectionHelper::onSocksAuthRead,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksAuthRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 auth read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x01) ||
        this->proxyReadBuffer[1] != char(0x00))
    {
        this->fail("SOCKS5 username/password authentication failed",
                   u"SOCKS5 auth");
        return;
    }

    this->doSocksConnect();
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::doSocksConnect()
{
    const auto host = this->targetHost();
    const auto port = this->options.url.port(Derived::DEFAULT_PORT);
    if (host.size() > 255)
    {
        this->fail("SOCKS5 target host is too long", u"SOCKS5 connect");
        return;
    }

    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.append(char(0x05));
    this->proxyWriteBuffer.append(char(0x01));
    this->proxyWriteBuffer.append(char(0x00));
    this->proxyWriteBuffer.append(char(0x03));
    this->proxyWriteBuffer.append(char(host.size()));
    this->proxyWriteBuffer.append(host.data(), qsizetype(host.size()));
    this->proxyWriteBuffer.append(char((port >> 8) & 0xff));
    this->proxyWriteBuffer.append(char(port & 0xff));

    boost::asio::async_write(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyWriteBuffer.constData(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(
            &WebSocketConnectionHelper::onSocksConnectWrite,
            this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksConnectWrite(
    boost::system::error_code ec, size_t /*bytesWritten*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 connect write");
        return;
    }

    boost::asio::async_read(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyReadBuffer.data(), 4),
        beast::bind_front_handler(
            &WebSocketConnectionHelper::onSocksConnectHeaderRead,
            this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksConnectHeaderRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 connect header read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x05))
    {
        this->fail("Invalid SOCKS5 connect response version",
                   u"SOCKS5 connect");
        return;
    }
    if (this->proxyReadBuffer[1] != char(0x00))
    {
        this->fail("SOCKS5 proxy failed to connect to target",
                   u"SOCKS5 connect");
        return;
    }

    const auto addressType =
        static_cast<unsigned char>(this->proxyReadBuffer[3]);
    if (addressType == 0x01)
    {
        boost::asio::async_read(
            beast::get_lowest_layer(this->stream),
            boost::asio::buffer(this->proxyReadBuffer.data(), 6),
            beast::bind_front_handler(
                &WebSocketConnectionHelper::onSocksConnectAddressRead,
                this->shared_from_this()));
        return;
    }
    if (addressType == 0x04)
    {
        boost::asio::async_read(
            beast::get_lowest_layer(this->stream),
            boost::asio::buffer(this->proxyReadBuffer.data(), 18),
            beast::bind_front_handler(
                &WebSocketConnectionHelper::onSocksConnectAddressRead,
                this->shared_from_this()));
        return;
    }
    if (addressType == 0x03)
    {
        boost::asio::async_read(
            beast::get_lowest_layer(this->stream),
            boost::asio::buffer(this->proxyReadBuffer.data(), 1),
            beast::bind_front_handler(
                &WebSocketConnectionHelper::onSocksConnectDomainLengthRead,
                this->shared_from_this()));
        return;
    }

    this->fail("Unsupported SOCKS5 connect address type", u"SOCKS5 connect");
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksConnectDomainLengthRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 connect domain length read");
        return;
    }

    const auto domainLength =
        static_cast<unsigned char>(this->proxyReadBuffer[0]);
    boost::asio::async_read(
        beast::get_lowest_layer(this->stream),
        boost::asio::buffer(this->proxyReadBuffer.data(), domainLength + 2),
        beast::bind_front_handler(
            &WebSocketConnectionHelper::onSocksConnectAddressRead,
            this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onSocksConnectAddressRead(
    boost::system::error_code ec, size_t /*bytesRead*/)
{
    if (ec)
    {
        this->fail(ec, u"SOCKS5 connect address read");
        return;
    }

    qCDebug(chatterinoWebsocket) << *this << "SOCKS5 proxy connect done";
    this->derived()->afterTcpHandshake();
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::doWsHandshake()
{
    beast::get_lowest_layer(this->stream).expires_never();
    this->stream.set_option(beast::websocket::stream_base::timeout::suggested(
        beast::role_type::client));
    this->stream.set_option(beast::websocket::stream_base::decorator{
        [this](beast::websocket::request_type &req) {
            bool hasUa = false;
            for (const auto &[key, value] : this->options.headers)
            {
                // TODO(Qt 6.5): Use QUtf8StringView
                QLatin1StringView keyView(key.c_str());
                if (QLatin1StringView("user-agent")
                        .compare(keyView, Qt::CaseInsensitive) == 0)
                {
                    hasUa = true;
                }

                try
                {
                    // this can fail if the key or value exceed the maximum size
                    req.set(key, value);
                }
                catch (const boost::system::system_error &err)
                {
                    qCWarning(chatterinoWebsocket)
                        << "Invalid header - name:" << QUtf8StringView(key)
                        << "value:" << QUtf8StringView(value)
                        << "error:" << QUtf8StringView(err.what());
                }
            }

            // default UA
            if (!hasUa)
            {
                auto ua = QStringLiteral("Chatterino/%1 (%2)")
                              .arg(Version::instance().version(),
                                   Version::instance().commitHash())
                              .toStdString();
                req.set(beast::http::field::user_agent, ua);
            }
        },
    });

    auto host = this->options.url.host(QUrl::FullyEncoded).toStdString() + ':' +
                std::to_string(this->options.url.port(Derived::DEFAULT_PORT));
    auto path = this->options.url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
    {
        path = "/";
    }
    if (this->options.url.hasQuery())
    {
        path += '?';
        path += this->options.url.query(QUrl::FullyEncoded);
    }
    this->stream.async_handshake(
        host, path.toStdString(),
        beast::bind_front_handler(&WebSocketConnectionHelper::onWsHandshake,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onWsHandshake(
    boost::system::error_code ec)
{
    if (!this->listener || this->isClosing)
    {
        return;
    }
    if (ec)
    {
        this->fail(ec, u"WS handshake");
        return;
    }

    qCDebug(chatterinoWebsocket) << *this << "WS handshake done";

    this->listener->onOpen();
    this->trySend();
    this->stream.async_read(
        this->readBuffer,
        beast::bind_front_handler(&WebSocketConnectionHelper::onReadDone,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onReadDone(
    boost::system::error_code ec, size_t bytesRead)
{
    if (!this->listener || this->isClosing)
    {
        return;
    }
    if (ec)
    {
        this->fail(ec, u"read");
        return;
    }

    // XXX: this copies - we could read directly into a QByteArray
    QByteArray data{
        static_cast<const char *>(this->readBuffer.cdata().data()),
        static_cast<QByteArray::size_type>(bytesRead),
    };
    this->readBuffer.consume(bytesRead);

    if (this->stream.got_text())
    {
        this->listener->onTextMessage(std::move(data));
    }
    else
    {
        this->listener->onBinaryMessage(std::move(data));
    }

    this->stream.async_read(
        this->readBuffer,
        beast::bind_front_handler(&WebSocketConnectionHelper::onReadDone,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::onWriteDone(
    boost::system::error_code ec, size_t /*bytesWritten*/)
{
    if (!this->queuedMessages.empty())
    {
        this->queuedMessages.pop_front();
    }
    else
    {
        assert(false);
    }
    this->isSending = false;

    if (ec)
    {
        this->fail(ec, u"write");
        return;
    }

    this->trySend();
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::trySend()
{
    if (this->queuedMessages.empty() || this->isSending ||
        !this->stream.is_open())
    {
        return;
    }

    this->isSending = true;
    this->stream.text(this->queuedMessages.front().first);
    this->stream.async_write(
        this->queuedMessages.front().second,
        beast::bind_front_handler(&WebSocketConnectionHelper::onWriteDone,
                                  this->shared_from_this()));
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::closeImpl()
{
    if (this->isClosing)
    {
        return;
    }
    this->isClosing = true;

    qCDebug(chatterinoWebsocket) << *this << "Closing...";

    // cancel all pending operations
    this->resolver.cancel();
    beast::get_lowest_layer(this->stream).cancel();

    this->stream.async_close(
        beast::websocket::close_code::normal,
        [this, lifetime{this->shared_from_this()}](auto ec) {
            if (ec)
            {
                qCWarning(chatterinoWebsocket) << *this << "Failed to close"
                                               << QUtf8StringView(ec.message());
                // make sure we cancel all operations
                beast::get_lowest_layer(this->stream).close();
            }
            else
            {
                qCDebug(chatterinoWebsocket) << *this << "Closed";
            }
            this->detach();
        });
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::fail(
    boost::system::error_code ec, QStringView op)
{
    this->fail(ec.message(), op);
}

template <typename Derived, typename Inner>
void WebSocketConnectionHelper<Derived, Inner>::fail(std::string_view ec,
                                                     QStringView op)
{
    qCWarning(chatterinoWebsocket)
        << *this << "Failed:" << op << QUtf8StringView(ec);
    if (this->stream.is_open())
    {
        this->closeImpl();
    }
    this->detach();
}

// MARK: TlsWebSocketConnection

TlsWebSocketConnection::TlsWebSocketConnection(
    WebSocketOptions options, int id,
    std::unique_ptr<WebSocketListener> listener, WebSocketPoolImpl *pool,
    asio::io_context &ioc, asio::ssl::context &ssl)
    : WebSocketConnectionHelper(std::move(options), id, std::move(listener),
                                pool, ioc, Stream{asio::make_strand(ioc), ssl})
{
}

bool TlsWebSocketConnection::setupStream(const std::string &host)
{
    // Set SNI Hostname (many hosts need this to handshake successfully)
    if (::SSL_set_tlsext_host_name(this->stream.next_layer().native_handle(),
                                   host.c_str()) == 0)
    {
        this->fail({static_cast<int>(::ERR_get_error()),
                    asio::error::get_ssl_category()},
                   u"Setting SNI hostname");
        return false;
    }
    return true;
}

void TlsWebSocketConnection::afterTcpHandshake()
{
    beast::get_lowest_layer(this->stream)
        .expires_after(std::chrono::seconds{30});
    this->stream.next_layer().async_handshake(
        asio::ssl::stream_base::client,
        [this,
         lifetime{this->shared_from_this()}](boost::system::error_code ec) {
            if (ec)
            {
                this->fail(ec, u"TLS handshake");
                return;
            }

            qCDebug(chatterinoWebsocket)
                << *this << "TLS handshake done, using"
                << ::SSL_get_version(this->stream.next_layer().native_handle());
            this->doWsHandshake();
        });
}

// MARK: TcpWebSocketConnection

TcpWebSocketConnection::TcpWebSocketConnection(
    WebSocketOptions options, int id,
    std::unique_ptr<WebSocketListener> listener, WebSocketPoolImpl *pool,
    asio::io_context &ioc)
    : WebSocketConnectionHelper(std::move(options), id, std::move(listener),
                                pool, ioc, Stream{asio::make_strand(ioc)})
{
}

void TcpWebSocketConnection::afterTcpHandshake()
{
    this->doWsHandshake();
}

}  // namespace chatterino::ws::detail
