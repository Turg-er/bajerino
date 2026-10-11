// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/commands/CommandModel.hpp"

#include "common/SignalVector.hpp"
#include "controllers/commands/Command.hpp"
#include "util/StandardItemHelper.hpp"

namespace chatterino {

// commandmodel
CommandModel::CommandModel(QObject *parent)
    : SignalVectorModel<Command>(Column::COUNT, parent)
{
}

// turn a vector item into a model row
Command CommandModel::getItemFromRow(std::vector<QStandardItem *> &row,
                                     const Command &original)
{
    auto command =
        Command(row[Column::Trigger]->data(Qt::EditRole).toString(),
                row[Column::CommandFunc]->data(Qt::EditRole).toString(),
                row[Column::ShowInMessageContextMenu]
                    ->data(Qt::CheckStateRole)
                    .toBool());
    command.usercardLabel =
        row[Column::UsercardLabel]->data(Qt::EditRole).toString().trimmed();
    command.usercardInMenu =
        row[Column::UsercardInMenu]->data(Qt::CheckStateRole).toBool();
    return command;
}

// turns a row in the model into a vector item
void CommandModel::getRowFromItem(const Command &item,
                                  std::vector<QStandardItem *> &row)
{
    setStringItem(row[Column::Trigger], item.name);
    setStringItem(row[Column::CommandFunc], item.func);
    setBoolItem(row[Column::ShowInMessageContextMenu],
                item.showInMsgContextMenu);
    setStringItem(row[Column::UsercardLabel], item.usercardLabel);
    setBoolItem(row[Column::UsercardInMenu], item.usercardInMenu);
}

}  // namespace chatterino
