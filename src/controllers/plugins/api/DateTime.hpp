#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <sol/forward.hpp>

namespace chatterino::lua::api::datetime {

/* @lua-fragment
---@class c2.DateTime
c2.DateTime = {}
---@param str string
---@return c2.DateTime
function c2.DateTime.from_iso_string(str) end
---@return string
function c2.DateTime:to_iso_string() end
---@return string
function c2.DateTime:to_iso_string_without_ms() end
---@return c2.DateTime
function c2.DateTime.current_local() end
---@return c2.DateTime
function c2.DateTime.current_utc() end
---@param ts number
---@return c2.DateTime
function c2.DateTime.from_unix_milliseconds(ts) end
---@param ts number
---@return c2.DateTime
function c2.DateTime.from_unix_seconds(ts) end
---@return number
function c2.DateTime:to_unix_milliseconds() end
---@return number
function c2.DateTime:to_unix_seconds() end
---@return boolean
function c2.DateTime:is_local() end
---@return boolean
function c2.DateTime:is_utc() end
---@return c2.DateTime
function c2.DateTime:to_local() end
---@return c2.DateTime
function c2.DateTime:to_utc() end
*/
void createUserTypes(sol::table &c2);

}

#endif
