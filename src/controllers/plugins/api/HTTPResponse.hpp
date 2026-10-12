// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once
#ifdef CHATTERINO_HAVE_PLUGINS
#    include "common/network/NetworkResult.hpp"

#    include <lua.h>
#    include <sol/sol.hpp>

#    include <memory>

namespace chatterino {
class PluginController;
}

namespace chatterino::lua::api {

/**
 * @lua@class c2.HTTPResponse
 */
class HTTPResponse
{
    NetworkResult result_;

public:
    HTTPResponse(NetworkResult res);
    HTTPResponse(HTTPResponse &&other) = default;
    HTTPResponse &operator=(HTTPResponse &&) = default;
    HTTPResponse &operator=(HTTPResponse &) = delete;
    HTTPResponse(const HTTPResponse &other) = delete;
    ~HTTPResponse() = default;

private:
    static void createUserType(sol::table &c2);
    friend class chatterino::PluginController;

public:

    /**
     * @lua@return string
     * @lua@nodiscard
     * @exposed c2.HTTPResponse:data
     */
    QByteArray data();

    /**
     * @lua@return number|nil
     * @lua@nodiscard
     * @exposed c2.HTTPResponse:status
     */
    std::optional<int> status();

    /**
     * @lua@return string
     * @lua@nodiscard
     * @exposed c2.HTTPResponse:error
     */
    QString error();

    /**
     * @lua@return string
     * @lua@nodiscard
     * @exposed c2.HTTPResponse:__tostring
     */
    QString to_string();
};

}
#endif
