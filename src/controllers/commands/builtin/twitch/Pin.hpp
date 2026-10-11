// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

struct PinDurationParseResult {
    bool matched = false;
    QString error;
    int durationSeconds = 0;
};

PinDurationParseResult parsePinDuration(const QString &text);

int normalizePinDuration(int durationSeconds);

QString pinMessage(const CommandContext &ctx);

QString unpinMessage(const CommandContext &ctx);

/// /pin
QString pin(const CommandContext &ctx);

/// /unpin
QString unpin(const CommandContext &ctx);

}  // namespace chatterino::commands
