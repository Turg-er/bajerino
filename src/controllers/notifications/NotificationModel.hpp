// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"

#include <QObject>

#include <cstdint>

namespace chatterino {

class NotificationController;
enum class Platform : uint8_t;

class NotificationModel : public SignalVectorModel<QString>
{
    explicit NotificationModel(QObject *parent, Platform platform);
    Platform platform_;

protected:
    // turn a vector item into a model row
    QString getItemFromRow(std::vector<QStandardItem *> &row,
                           const QString &original) override;

    // turns a row in the model into a vector item
    void getRowFromItem(const QString &item,
                        std::vector<QStandardItem *> &row) override;

    friend class NotificationController;
};

}  // namespace chatterino
