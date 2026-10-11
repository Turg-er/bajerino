// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/hotkeys/ActionNames.hpp"

#include <QKeySequence>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino {

std::vector<QString> parseHotkeyArguments(QString argumentString);
std::optional<ActionDefinition> findHotkeyActionDefinition(
    HotkeyCategory category, const QString &action);

// convert key_enter to key_return so that both keys function the same. preserves multi-key sequences and combinations.
QKeySequence normalizeKeySequence(const QKeySequence &seq);

std::optional<Qt::Key> physicalNumberRowKey(const QString &platformName,
                                            int logicalKey,
                                            quint32 nativeScanCode,
                                            quint32 nativeVirtualKey);
std::vector<QString> remapIndexedHotkeyArguments(
    const std::vector<QString> &arguments, const std::vector<int> &oldToNew);

}  // namespace chatterino
