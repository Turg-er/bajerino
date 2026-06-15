// SPDX-License-Identifier: MIT

#include "providers/twitch/ChannelPointsFarmModel.hpp"

#include "util/StandardItemHelper.hpp"

namespace chatterino {

ChannelPointsFarmModel::ChannelPointsFarmModel(QObject *parent)
    : SignalVectorModel<QString>(1, parent)
{
}

QString ChannelPointsFarmModel::getItemFromRow(
    std::vector<QStandardItem *> &row, const QString & /*original*/)
{
    return row.at(0)->data(Qt::DisplayRole).toString();
}

void ChannelPointsFarmModel::getRowFromItem(const QString &item,
                                            std::vector<QStandardItem *> &row)
{
    setStringItem(row.at(0), item);
}

}  // namespace chatterino
