// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/TwitchChannel.hpp"

namespace chatterino {

class TwitchChannelTestAccess
{
public:
    static void setRoomId(TwitchChannel &channel, const QString &roomId)
    {
        channel.setRoomId(roomId);
    }

    static void refreshPubSub(TwitchChannel &channel)
    {
        channel.refreshPubSub();
    }
};

}  // namespace chatterino
