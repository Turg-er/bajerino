// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

class QWidget;

namespace chatterino {

class TwitchChannel;

void showEmoteSourcesDialog(const std::shared_ptr<TwitchChannel> &channel,
                            QWidget *parent);

}  // namespace chatterino
