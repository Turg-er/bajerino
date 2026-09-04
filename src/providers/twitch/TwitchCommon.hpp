// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QString>

#include <cstdint>
#include <vector>

namespace chatterino {

[[maybe_unused]] inline const char *const ANONYMOUS_USERNAME = "justinfan64537";

enum class TwitchChannelMode : std::uint8_t {
    Authenticated,
    AnonymousRead,
    BajerinoAnonymous,
};

inline constexpr int TWITCH_MESSAGE_LIMIT = 500;

inline QByteArray getDefaultClientID()
{
    return QByteArrayLiteral("7ue61iz46fz11y3cugd0l3tawb4taal");
}

extern const std::vector<QColor> TWITCH_USERNAME_COLORS;

extern const QStringList TWITCH_DEFAULT_COMMANDS;

extern const QStringList TWITCH_WHISPER_COMMANDS;

}  // namespace chatterino
