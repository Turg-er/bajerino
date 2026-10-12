// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once
#ifdef CHATTERINO_HAVE_PLUGINS
#    include "common/Channel.hpp"
#    include "providers/twitch/TwitchChannel.hpp"

#    include <sol/forward.hpp>

namespace chatterino::lua {
class ThisPluginState;
}

namespace chatterino::lua::api {

struct ConnectionHandle;

/**
 * @includefile providers/twitch/TwitchChannel.hpp
 */

/**
 * @lua@class c2.Channel
 */
struct ChannelRef {
public:
    ChannelRef(const std::shared_ptr<Channel> &chan);

    /**
     * @lua@return boolean success
     * @exposed c2.Channel:is_valid
     */
    bool is_valid();

    /**
     * @lua@return string name
     * @exposed c2.Channel:get_name
     */
    QString get_name();

    /**
     * @lua@return c2.ChannelType
     * @exposed c2.Channel:get_type
     */
    Channel::Type get_type();

    /**
     * @lua@return string name
     * @exposed c2.Channel:get_display_name
     */
    QString get_display_name();

    /**
     * @lua@param message string
     * @lua@param execute_commands? boolean Should commands be run on the text?
     * @exposed c2.Channel:send_message
     */
    void send_message(QString text, sol::variadic_args va);

    /**
     * @lua@param message string
     * @exposed c2.Channel:add_system_message
     */
    void add_system_message(QString text);

    /**
     * @lua@param message c2.Message
     * @lua@param context? c2.MessageContext The context of the message being added
     * @lua@param override_flags? c2.MessageFlag|nil Flags to override the message's flags (some splits might filter for this)
     * @exposed c2.Channel:add_message
     */
    void add_message(std::shared_ptr<Message> &message, sol::variadic_args va);

    /**
     * @lua@param n_items number Number of messages to retrieve. This is an upper bound, the actual number of messages returned might be lower.
     * @lua@return c2.Message[]
     * @exposed c2.Channel:message_snapshot
     */
    std::vector<MessagePtrMut> message_snapshot(size_t n_items);

    /**
     * @lua@return c2.Message?
     * @exposed c2.Channel:last_message
     */
    MessagePtrMut last_message();

    /**
     * @lua@param message c2.Message The message to replace.
     * @lua@param replacement c2.Message The replacement.
     * @exposed c2.Channel:replace_message
     */
    void replace_message(const MessagePtrMut &message,
                         const MessagePtrMut &replacement);

    /**
     * @lua@param message c2.Message The message to replace.
     * @lua@param replacement c2.Message The replacement.
     * @lua@param hint number A one-based index (from the start) where the message is probably located. This is checked first. Otherwise the behavior is identical to the overload without this parameter.
     * @exposed c2.Channel:replace_message
     */
    void replace_message_hint(const MessagePtrMut &message,
                              const MessagePtrMut &replacement, size_t hint);

    /**
     * @lua@param index number A one-based index (from the start) of the message to replace.
     * @lua@param replacement c2.Message The replacement.
     * @exposed c2.Channel:replace_message_at
     */
    void replace_message_at(size_t index, const MessagePtrMut &replacement);

    /**
     * @exposed c2.Channel:clear_messages
     */
    void clear_messages();

    /**
     * @lua@param id string
     * @lua@return c2.Message?
     * @exposed c2.Channel:find_message_by_id
     */
    MessagePtrMut find_message_by_id(const QString &id);

    /**
     * @lua@return boolean
     * @exposed c2.Channel:has_messages
     */
    bool has_messages();

    /**
     * @lua@return number
     * @exposed c2.Channel:count_messages
     */
    size_t count_messages();

    /**
     * @lua@return boolean
     * @exposed c2.Channel:is_twitch_channel
     */
    bool is_twitch_channel();

    /**
     * @lua@return RoomModes
     * @exposed c2.Channel:get_room_modes
     */
    sol::table get_room_modes(sol::this_state state);

    /**
     * @lua@return StreamStatus
     * @exposed c2.Channel:get_stream_status
     */
    sol::table get_stream_status(sol::this_state state);

    /**
     * @lua@return string
     * @exposed c2.Channel:get_twitch_id
     */
    QString get_twitch_id();

    /**
     * @lua@return boolean
     * @exposed c2.Channel:is_broadcaster
     */
    bool is_broadcaster();

    /**
     * @lua@return boolean
     * @exposed c2.Channel:is_mod
     */
    bool is_mod();

    /**
     * @lua@return boolean
     * @exposed c2.Channel:is_vip
     */
    bool is_vip();

    /**
     * @lua@return string
     * @exposed c2.Channel:__tostring
     */
    QString to_string();

    bool operator==(const ChannelRef &other) const noexcept;

    /**
     * @lua@param cb fun()
     * @lua@return c2.ConnectionHandle hdl
     * @exposed c2.Channel:on_display_name_changed
     */
    ConnectionHandle on_display_name_changed(ThisPluginState state,
                                             sol::main_protected_function pfn);

    /**
     * @lua@param cb fun()
     * @lua@return c2.ConnectionHandle hdl
     * @exposed c2.Channel:on_messages_cleared
     */
    ConnectionHandle on_messages_cleared(ThisPluginState state,
                                         sol::main_protected_function pfn);

    /**
     * @lua@param cb fun(idx: number, old: c2.Message, replacement: c2.Message) `idx` is a one-based index (from the start)
     * @lua@return c2.ConnectionHandle hdl
     * @exposed c2.Channel:on_message_replaced
     */
    ConnectionHandle on_message_replaced(ThisPluginState state,
                                         sol::main_protected_function pfn);

    /**
     * @lua@param cb fun(msg: c2.Message, override_flags?: c2.MessageFlag)
     * @lua@return c2.ConnectionHandle hdl
     * @exposed c2.Channel:on_message_appended
     */
    ConnectionHandle on_message_appended(ThisPluginState state,
                                         sol::main_protected_function pfn);

    /**
     * @lua@param name string Which channel are you looking for?
     * @lua@return c2.Channel?
     * @exposed c2.Channel.by_name
     */
    static std::optional<ChannelRef> get_by_name(const QString &name);

    /**
     * @lua@param id string ID of the owner of the channel.
     * @lua@return c2.Channel?
     * @exposed c2.Channel.by_twitch_id
     */
    static std::optional<ChannelRef> get_by_twitch_id(const QString &id);

    static void createUserType(sol::table &c2);

private:
    std::weak_ptr<Channel> weak;

    std::shared_ptr<Channel> strong();

    std::shared_ptr<TwitchChannel> twitch();
};

sol::table toTable(lua_State *L, const TwitchChannel::RoomModes &modes);
sol::table toTable(lua_State *L, const TwitchChannel::StreamStatus &status);

}
#endif
