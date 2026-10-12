// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <sol/forward.hpp>

namespace chatterino::lua::api {

/* @lua-fragment
---@class c2.TwitchAccount
c2.TwitchAccount = {}
---@return boolean success
function c2.TwitchAccount:is_valid() end
---@return string user_login The (login) name of the account
function c2.TwitchAccount:login() end
---@return string user_id The Twitch user ID of the account
function c2.TwitchAccount:id() end
---@return string? color Color in chat of this account. `nil` if not yet known
function c2.TwitchAccount:color() end
---@return boolean is_anon `true` if this account is an anonymous account (no associated Twitch user)
function c2.TwitchAccount:is_anon() end
---@return string str
function c2.TwitchAccount:__tostring() end
---@return c2.TwitchAccount account
function c2.current_account() end
*/
void createAccounts(sol::table &c2);

}

#endif
