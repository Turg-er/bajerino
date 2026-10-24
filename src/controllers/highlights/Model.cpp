// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/Model.hpp"

#include "common/SignalVectorModel.hpp"
#include "controllers/highlights/types/AnyHighlight.hpp"  // IWYU pragma: keep
#include "util/StandardItemHelper.hpp"
#include "util/Variant.hpp"

#include <QPalette>
#include <QPointer>

namespace chatterino::highlights {

Model::Model(QObject *parent)
    : SignalVectorModel<AnyHighlight>(Column::COUNT, parent)
{
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Model::updateRow(const AnyHighlight &highlight,
                      std::vector<QStandardItem *> &row)
{
    auto soundIcon = [highlight] {
        if (shouldPlaySound(highlight))
        {
            return QIcon{":/buttons/music-note-1.svg"};
        }

        return QIcon{":/buttons/speaker-mute.svg"};
    }();

    auto enabled = isEnabled(highlight);

    QPalette palette;

    if (auto error = getError(highlight); !error.isEmpty())
    {
        // Highlight has an error
        row[Column::NAME]->setData(error, Qt::ToolTipRole);
        row[Column::ENABLED]->setData(error, Qt::ToolTipRole);

        QFont f;
        f.setStrikeOut(true);
        row[Column::NAME]->setData(f, Qt::FontRole);

        row[Column::ENABLED]->setData("Error", Qt::EditRole);
    }
    else
    {
        row[Column::NAME]->setData(QString{}, Qt::ToolTipRole);
        row[Column::ENABLED]->setData(QString{}, Qt::ToolTipRole);

        QFont f;
        row[Column::NAME]->setData(f, Qt::FontRole);

        if (enabled)
        {
            // Highlight is enabled
            row[Column::ENABLED]->setData("Enabled", Qt::EditRole);

            // Undim name
            const auto &b = palette.text();
            row[Column::NAME]->setData(b, Qt::ForegroundRole);
        }
        else
        {
            // Highlight is disabled
            row[Column::ENABLED]->setData("Disabled", Qt::EditRole);

            // Dim name
            const auto &b = palette.placeholderText();
            row[Column::NAME]->setData(b, Qt::ForegroundRole);
        }
    }

    row[Column::ENABLED]->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                   Qt::ItemIsUserCheckable);
    row[Column::ENABLED]->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

    auto backgroundColor = highlights::getBackgroundColor(highlight);
    if (backgroundColor && backgroundColor->isValid())
    {
        row[Column::SOUND]->setData(*backgroundColor, Qt::BackgroundRole);
    }
    else
    {
        row[Column::SOUND]->setData(QVariant(), Qt::BackgroundRole);
    }

    row[Column::NAME]->setData(getIcon(highlight), Qt::DecorationRole);

    /*
    if (const auto *h = std::get_if<BadgeHighlight>(&highlight))
    {
        // TODO: If the badge fails to load, it would be nice if the loading icon could be replaced with an error icon of some kind
        getApp()->getTwitchBadges()->getBadgeIcon(
            h->getBadgeName(),
            [model = QPointer(this), id = h->getID()](
                const QString &name, const std::shared_ptr<QIcon> &icon) {
                NOTE!!!!!!!!! This code doesn't work, it can override highlights that come after it.
                (void)name;  // unused

                runInGuiThread([model, id, icon] {
                    if (!model)
                    {
                        return;
                    }

                    auto matches = model->match(
                        model->index(0, highlights::Model::Column::ENABLED),
                        highlights::Model::ID_ROLE, QVariant::fromValue(id), 1,
                        Qt::MatchExactly | Qt::MatchWrap);
                    if (matches.isEmpty())
                    {
                        qCWarning(LOG)
                            << "Attempted to set badge icon for" << id
                            << "but it is missing. Was it removed?";
                        return;
                    }
                    auto matchingCell = matches.first();
                    assert(matchingCell.isValid());
                    auto nameCell = matchingCell.siblingAtColumn(Column::NAME);
                    assert(nameCell.isValid());
                    model->setData(nameCell, *icon, Qt::DecorationRole);
                });
            });
    }
    */

    setStringItem(row[Column::NAME], getName(highlight), false);
    setStringItem(row[Column::SOUND], "");
    row[Column::SOUND]->setData(soundIcon, Qt::DecorationRole);
}

AnyHighlight Model::getItemFromRow(std::vector<QStandardItem *> &row,
                                   const AnyHighlight &original,
                                   const std::optional<QModelIndex> &index,
                                   std::optional<int> role)
{
    (void)original;  // unused

    auto item = get<AnyHighlight>(row[Column::ENABLED]->data(DATA_ROLE));

    if (role.has_value() && role.value() == Qt::CheckStateRole &&
        index.has_value() && index.value().column() == Column::ENABLED)
    {
        auto checkstate =
            row[Column::ENABLED]->data(Qt::CheckStateRole).toBool();
        std::visit(variant::Overloaded{
                       [](InvalidHighlight & /*h*/) {
                           //
                       },
                       [checkstate](auto &&h) {
                           h.enabled = checkstate;
                       },
                   },
                   item);

        // Since the enabled flag is updated, force update DATA
        row[Column::ENABLED]->setData(QVariant::fromValue(item), DATA_ROLE);
    }

    this->updateRow(item, row);

    return item;
}

void Model::getRowFromItem(const AnyHighlight &item,
                           std::vector<QStandardItem *> &row)
{
    row[Column::ENABLED]->setData(QVariant::fromValue(item), DATA_ROLE);
    row[Column::ENABLED]->setData(QVariant::fromValue(getID(item)), ID_ROLE);

    this->updateRow(item, row);
}
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

}  // namespace chatterino::highlights
