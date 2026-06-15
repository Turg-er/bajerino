// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"

#include <QObject>

namespace chatterino {

class ChannelPointsFarm;

/// Model backing the channel points farm priority list. The row order is the
/// priority order: open & live channels earlier in the list are farmed first
/// when more channels are live than Twitch allows points to be earned on at
/// once.
class ChannelPointsFarmModel : public SignalVectorModel<QString>
{
    explicit ChannelPointsFarmModel(QObject *parent);

protected:
    QString getItemFromRow(std::vector<QStandardItem *> &row,
                           const QString &original) override;

    void getRowFromItem(const QString &item,
                        std::vector<QStandardItem *> &row) override;

    friend class ChannelPointsFarm;
};

}  // namespace chatterino
