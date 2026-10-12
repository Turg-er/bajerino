#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <pajlada/signals/scoped-connection.hpp>
#    include <sol/forward.hpp>

#    include <memory>

namespace chatterino::lua::api {

/* @lua-fragment
---@class c2.ConnectionHandle
c2.ConnectionHandle = {}
function c2.ConnectionHandle:disconnect() end
function c2.ConnectionHandle:block() end
function c2.ConnectionHandle:unblock() end
---@return boolean is_blocked
function c2.ConnectionHandle:is_blocked() end
---@return boolean is_connected
function c2.ConnectionHandle:is_connected() end
*/
struct ConnectionHandle {
    std::weak_ptr<pajlada::Signals::ScopedConnection> connection;

    static void createUserType(sol::table &c2);
};

}

#endif
