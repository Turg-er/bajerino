#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chatterino::eventsub::lib {

enum class ProxyType : std::uint8_t { Http, Socks5 };

struct ProxyOptions {
    ProxyType type = ProxyType::Http;
    std::string host;
    std::string port;
    std::string user;
    std::string password;
};

/// Result delivered to a ProxyHandshake completion handler. A
/// default-constructed value (no error code and no message) signals success;
/// otherwise exactly one of `ec` or `message` describes the failure.
struct ProxyError {
    boost::beast::error_code ec{};
    std::string message;
    std::string_view op;

    /// True if the handshake failed.
    explicit operator bool() const
    {
        return static_cast<bool>(this->ec) || !this->message.empty();
    }
};

/// Performs an HTTP CONNECT or SOCKS5 proxy handshake over an
/// already-connected TCP stream, leaving it tunneled to the target host so the
/// caller can proceed with a TLS handshake.
///
/// boost::beast/asio provide no proxy support, so the handshakes are
/// implemented here by hand. The object keeps itself alive for the duration of
/// the asynchronous operation by binding `shared_from_this()` into each
/// continuation, mirroring the pattern used by Session. The completion handler
/// is invoked exactly once, on success or on the first failure.
class ProxyHandshake : public std::enable_shared_from_this<ProxyHandshake>
{
public:
    using Stream = boost::beast::tcp_stream;
    using CompletionHandler = std::function<void(const ProxyError &)>;

    /// `stream` must already be connected to the proxy. `host`/`port` are the
    /// final target the proxy should tunnel to. `onComplete` is responsible for
    /// keeping any owning object (e.g. the Session) alive.
    ProxyHandshake(Stream &stream, ProxyOptions proxy, std::string host,
                   std::string port, CompletionHandler onComplete);

    void run();

private:
    void doHttpProxyHandshake();
    void onHttpProxyWrite(boost::beast::error_code ec,
                          std::size_t bytesTransferred);
    void onHttpProxyRead(boost::beast::error_code ec,
                         std::size_t bytesTransferred);

    void doSocksProxyHandshake();
    void onSocksGreetingWrite(boost::beast::error_code ec,
                              std::size_t bytesTransferred);
    void onSocksGreetingRead(boost::beast::error_code ec,
                             std::size_t bytesTransferred);
    void doSocksAuth();
    void onSocksAuthWrite(boost::beast::error_code ec,
                          std::size_t bytesTransferred);
    void onSocksAuthRead(boost::beast::error_code ec,
                         std::size_t bytesTransferred);
    void doSocksConnect();
    void onSocksConnectWrite(boost::beast::error_code ec,
                             std::size_t bytesTransferred);
    void onSocksConnectHeaderRead(boost::beast::error_code ec,
                                  std::size_t bytesTransferred);
    void onSocksConnectDomainLengthRead(boost::beast::error_code ec,
                                        std::size_t bytesTransferred);
    void onSocksConnectAddressRead(boost::beast::error_code ec,
                                   std::size_t bytesTransferred);

    /// Reports success to the completion handler.
    void finish();
    void fail(boost::beast::error_code ec, std::string_view op);
    void fail(std::string message, std::string_view op);

    Stream &stream;
    ProxyOptions proxy;
    std::string host;
    std::string port;
    std::string hostHeader;
    CompletionHandler onComplete;

    boost::beast::flat_buffer proxyBuffer;
    boost::beast::http::request<boost::beast::http::empty_body>
        proxyConnectRequest;
    boost::beast::http::response_parser<boost::beast::http::string_body>
        proxyConnectParser;
    std::vector<char> proxyWriteBuffer;
    std::array<char, 512> proxyReadBuffer{};
};

}  // namespace chatterino::eventsub::lib
