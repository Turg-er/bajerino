// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"
#include "controllers/highlights/types/AnyHighlightForward.hpp"

#include <QObject>
#include <QStandardItemModel>

#include <optional>
#include <vector>

namespace chatterino::highlights {

struct SharedHighlight;

class Model : public SignalVectorModel<AnyHighlight>
{
public:
    static constexpr int DATA_ROLE = Qt::UserRole + 110;
    static constexpr int ID_ROLE = Qt::UserRole + 111;

    explicit Model(QObject *parent);

    // Integer indexes shared with the highlighting table view.
    struct Column {
        static constexpr int ENABLED = 0;
        static constexpr int SOUND = 1;
        static constexpr int NAME = 2;
        static constexpr int COUNT = 3;
    };

protected:
    /// Update the given `row` based on the data in the given `highlight`
    void updateRow(const AnyHighlight &highlight,
                   std::vector<QStandardItem *> &row);

    // turn a vector item into a model row
    AnyHighlight getItemFromRow(std::vector<QStandardItem *> &row,
                                const AnyHighlight &original,
                                const std::optional<QModelIndex> &index,
                                std::optional<int> role) override;

    // turns a row in the model into a vector item
    void getRowFromItem(const AnyHighlight &item,
                        std::vector<QStandardItem *> &row) override;
};

}  // namespace chatterino::highlights
