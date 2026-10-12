#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <sol/forward.hpp>

namespace chatterino::lua::api::menu {

/* @lua-fragment
---@class c2.Menu
c2.Menu = {}
---@param text string
---@param cb fun()
function c2.Menu:add_action(text, cb) end
---@param before string|integer
---@param text string
---@param cb fun()
function c2.Menu:insert_action(before, text, cb) end
---@param title string
---@return c2.Menu
function c2.Menu:add_menu(title) end
---@param before string|integer
---@param title string
---@return c2.Menu
function c2.Menu:insert_menu(before, title) end
function c2.Menu:add_separator() end
---@param before string|integer
function c2.Menu:insert_separator(before) end
*/
void createUserType(sol::table &c2);

}

#endif
