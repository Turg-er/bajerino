#include "twitch-eventsub-ws/proxy.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>

namespace beast = boost::beast;
namespace http = beast::http;

namespace chatterino::eventsub::lib {

namespace {

std::string base64Encode(std::string_view input)
{
    static constexpr char TABLE[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < input.size())
    {
        const auto a = static_cast<unsigned char>(input[i++]);
        const auto b = static_cast<unsigned char>(input[i++]);
        const auto c = static_cast<unsigned char>(input[i++]);

        output.push_back(TABLE[(a >> 2) & 0x3f]);
        output.push_back(TABLE[((a & 0x03) << 4) | ((b >> 4) & 0x0f)]);
        output.push_back(TABLE[((b & 0x0f) << 2) | ((c >> 6) & 0x03)]);
        output.push_back(TABLE[c & 0x3f]);
    }

    if (i < input.size())
    {
        const auto a = static_cast<unsigned char>(input[i++]);
        output.push_back(TABLE[(a >> 2) & 0x3f]);
        if (i < input.size())
        {
            const auto b = static_cast<unsigned char>(input[i]);
            output.push_back(TABLE[((a & 0x03) << 4) | ((b >> 4) & 0x0f)]);
            output.push_back(TABLE[(b & 0x0f) << 2]);
            output.push_back('=');
        }
        else
        {
            output.push_back(TABLE[(a & 0x03) << 4]);
            output.push_back('=');
            output.push_back('=');
        }
    }

    return output;
}

}  // namespace

ProxyHandshake::ProxyHandshake(Stream &stream, ProxyOptions proxy_,
                               std::string host_, std::string port_,
                               CompletionHandler onComplete_)
    : stream(stream)
    , proxy(std::move(proxy_))
    , host(std::move(host_))
    , port(std::move(port_))
    , hostHeader(this->host + ':' + this->port)
    , onComplete(std::move(onComplete_))
{
}

void ProxyHandshake::run()
{
    switch (this->proxy.type)
    {
        case ProxyType::Http:
            this->doHttpProxyHandshake();
            return;

        case ProxyType::Socks5:
            this->doSocksProxyHandshake();
            return;
    }
}

void ProxyHandshake::doHttpProxyHandshake()
{
    const auto target = this->hostHeader;

    this->proxyBuffer.clear();
    // A 200 response to a CONNECT request has no body; tell the parser to skip
    // it so the read completes after the headers instead of waiting for an
    // end-of-stream that never arrives on a successfully tunneled connection.
    this->proxyConnectParser.skip(true);
    this->proxyConnectRequest = {};
    this->proxyConnectRequest.version(11);
    this->proxyConnectRequest.method(http::verb::connect);
    this->proxyConnectRequest.target(target);
    this->proxyConnectRequest.set(http::field::host, target);

    if (!this->proxy.user.empty() || !this->proxy.password.empty())
    {
        const auto auth = this->proxy.user + ':' + this->proxy.password;
        this->proxyConnectRequest.set(http::field::proxy_authorization,
                                      "Basic " + base64Encode(auth));
    }

    this->stream.expires_after(std::chrono::seconds(30));
    http::async_write(
        this->stream, this->proxyConnectRequest,
        beast::bind_front_handler(&ProxyHandshake::onHttpProxyWrite,
                                  shared_from_this()));
}

void ProxyHandshake::onHttpProxyWrite(beast::error_code ec,
                                      std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "HTTP proxy CONNECT write");
        return;
    }

    http::async_read(this->stream, this->proxyBuffer, this->proxyConnectParser,
                     beast::bind_front_handler(&ProxyHandshake::onHttpProxyRead,
                                               shared_from_this()));
}

void ProxyHandshake::onHttpProxyRead(beast::error_code ec,
                                     std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "HTTP proxy CONNECT read");
        return;
    }

    // Any 2xx status indicates the tunnel was established (RFC 7231 §4.3.6);
    // proxies most commonly use 200, but 2xx in general means success.
    const auto &response = this->proxyConnectParser.get();
    if (response.result_int() < 200 || response.result_int() >= 300)
    {
        this->fail("HTTP proxy CONNECT returned " +
                       std::to_string(response.result_int()),
                   "HTTP proxy CONNECT");
        return;
    }

    this->finish();
}

void ProxyHandshake::doSocksProxyHandshake()
{
    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.push_back(char(0x05));
    if (!this->proxy.user.empty() || !this->proxy.password.empty())
    {
        this->proxyWriteBuffer.push_back(char(0x02));
        this->proxyWriteBuffer.push_back(char(0x00));
        this->proxyWriteBuffer.push_back(char(0x02));
    }
    else
    {
        this->proxyWriteBuffer.push_back(char(0x01));
        this->proxyWriteBuffer.push_back(char(0x00));
    }

    this->stream.expires_after(std::chrono::seconds(30));
    boost::asio::async_write(
        this->stream,
        boost::asio::buffer(this->proxyWriteBuffer.data(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(&ProxyHandshake::onSocksGreetingWrite,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksGreetingWrite(beast::error_code ec,
                                          std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 greeting write");
        return;
    }

    boost::asio::async_read(
        this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 2),
        beast::bind_front_handler(&ProxyHandshake::onSocksGreetingRead,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksGreetingRead(beast::error_code ec,
                                         std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 greeting read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x05))
    {
        this->fail("Invalid SOCKS5 greeting version", "SOCKS5 greeting");
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
               "SOCKS5 greeting");
}

void ProxyHandshake::doSocksAuth()
{
    if (this->proxy.user.size() > 255 || this->proxy.password.size() > 255)
    {
        this->fail("SOCKS5 username/password is too long", "SOCKS5 auth");
        return;
    }

    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.push_back(char(0x01));
    this->proxyWriteBuffer.push_back(char(this->proxy.user.size()));
    this->proxyWriteBuffer.insert(this->proxyWriteBuffer.end(),
                                  this->proxy.user.begin(),
                                  this->proxy.user.end());
    this->proxyWriteBuffer.push_back(char(this->proxy.password.size()));
    this->proxyWriteBuffer.insert(this->proxyWriteBuffer.end(),
                                  this->proxy.password.begin(),
                                  this->proxy.password.end());

    boost::asio::async_write(
        this->stream,
        boost::asio::buffer(this->proxyWriteBuffer.data(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(&ProxyHandshake::onSocksAuthWrite,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksAuthWrite(beast::error_code ec,
                                      std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 auth write");
        return;
    }

    boost::asio::async_read(
        this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 2),
        beast::bind_front_handler(&ProxyHandshake::onSocksAuthRead,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksAuthRead(beast::error_code ec,
                                     std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 auth read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x01) ||
        this->proxyReadBuffer[1] != char(0x00))
    {
        this->fail("SOCKS5 username/password authentication failed",
                   "SOCKS5 auth");
        return;
    }

    this->doSocksConnect();
}

void ProxyHandshake::doSocksConnect()
{
    if (this->host.size() > 255)
    {
        this->fail("SOCKS5 target host is too long", "SOCKS5 connect");
        return;
    }

    // Parse the port without throwing: from_chars into a uint16_t rejects both
    // non-numeric and out-of-range (>65535) values. std::stoi would throw on
    // bad input, and an exception escaping this async handler would terminate.
    std::uint16_t portValue = 0;
    const auto *portBegin = this->port.data();
    const auto *portEnd = portBegin + this->port.size();
    auto [parseEnd, parseErr] = std::from_chars(portBegin, portEnd, portValue);
    if (parseErr != std::errc{} || parseEnd != portEnd)
    {
        this->fail("Invalid SOCKS5 target port", "SOCKS5 connect");
        return;
    }

    this->proxyWriteBuffer.clear();
    this->proxyWriteBuffer.push_back(char(0x05));
    this->proxyWriteBuffer.push_back(char(0x01));
    this->proxyWriteBuffer.push_back(char(0x00));
    this->proxyWriteBuffer.push_back(char(0x03));
    this->proxyWriteBuffer.push_back(char(this->host.size()));
    this->proxyWriteBuffer.insert(this->proxyWriteBuffer.end(),
                                  this->host.begin(), this->host.end());
    this->proxyWriteBuffer.push_back(char((portValue >> 8) & 0xff));
    this->proxyWriteBuffer.push_back(char(portValue & 0xff));

    boost::asio::async_write(
        this->stream,
        boost::asio::buffer(this->proxyWriteBuffer.data(),
                            this->proxyWriteBuffer.size()),
        beast::bind_front_handler(&ProxyHandshake::onSocksConnectWrite,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksConnectWrite(beast::error_code ec,
                                         std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 connect write");
        return;
    }

    boost::asio::async_read(
        this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 4),
        beast::bind_front_handler(&ProxyHandshake::onSocksConnectHeaderRead,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksConnectHeaderRead(beast::error_code ec,
                                              std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 connect header read");
        return;
    }

    if (this->proxyReadBuffer[0] != char(0x05))
    {
        this->fail("Invalid SOCKS5 connect response version", "SOCKS5 connect");
        return;
    }
    if (this->proxyReadBuffer[1] != char(0x00))
    {
        this->fail("SOCKS5 proxy failed to connect to target",
                   "SOCKS5 connect");
        return;
    }

    const auto addressType =
        static_cast<unsigned char>(this->proxyReadBuffer[3]);
    if (addressType == 0x01)
    {
        boost::asio::async_read(
            this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 6),
            beast::bind_front_handler(
                &ProxyHandshake::onSocksConnectAddressRead,
                shared_from_this()));
        return;
    }
    if (addressType == 0x04)
    {
        boost::asio::async_read(
            this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 18),
            beast::bind_front_handler(
                &ProxyHandshake::onSocksConnectAddressRead,
                shared_from_this()));
        return;
    }
    if (addressType == 0x03)
    {
        boost::asio::async_read(
            this->stream, boost::asio::buffer(this->proxyReadBuffer.data(), 1),
            beast::bind_front_handler(
                &ProxyHandshake::onSocksConnectDomainLengthRead,
                shared_from_this()));
        return;
    }

    this->fail("Unsupported SOCKS5 connect address type", "SOCKS5 connect");
}

void ProxyHandshake::onSocksConnectDomainLengthRead(
    beast::error_code ec, std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 connect domain length read");
        return;
    }

    const auto domainLength =
        static_cast<unsigned char>(this->proxyReadBuffer[0]);
    boost::asio::async_read(
        this->stream,
        boost::asio::buffer(this->proxyReadBuffer.data(), domainLength + 2),
        beast::bind_front_handler(&ProxyHandshake::onSocksConnectAddressRead,
                                  shared_from_this()));
}

void ProxyHandshake::onSocksConnectAddressRead(beast::error_code ec,
                                               std::size_t /*bytesTransferred*/)
{
    if (ec)
    {
        this->fail(ec, "SOCKS5 connect address read");
        return;
    }

    this->finish();
}

void ProxyHandshake::finish()
{
    this->onComplete(ProxyError{});
}

void ProxyHandshake::fail(beast::error_code ec, std::string_view op)
{
    this->onComplete(ProxyError{.ec = ec, .message = {}, .op = op});
}

void ProxyHandshake::fail(std::string message, std::string_view op)
{
    this->onComplete(
        ProxyError{.ec = {}, .message = std::move(message), .op = op});
}

}  // namespace chatterino::eventsub::lib
