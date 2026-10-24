// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/EmoteSourcesDialog.hpp"

#include "providers/twitch/TwitchChannel.hpp"

#include <pajlada/signals/scoped-connection.hpp>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace chatterino {

void showEmoteSourcesDialog(const std::shared_ptr<TwitchChannel> &channel,
                            QWidget *parent)
{
    if (!channel)
    {
        return;
    }
    const auto sources = channel->emoteSources();
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle("Emote sources for #" + channel->getName());
    auto *layout = new QVBoxLayout(dialog);
    auto *description = new QLabel(
        "Use another Twitch channel's emotes in #" + channel->getName() +
            ". These settings only affect this channel. "
            "Leave a field blank to use this channel's own emotes.",
        dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *sharedForm = new QFormLayout;
    auto *sharedID = new QLineEdit(sources.sharedTwitchID, dialog);
    sharedID->setObjectName("SharedEmoteSourceID");
    sharedID->setToolTip(
        "A numeric Twitch user ID for 7TV, BTTV, and FFZ. Leave blank "
        "to use this channel's own emotes.");
    sharedForm->addRow("Twitch user ID for all providers", sharedID);
    layout->addLayout(sharedForm);

    auto *individual = new QCheckBox("Override providers individually", dialog);
    individual->setObjectName("IndividualEmoteSources");
    individual->setChecked(sources.individualProviders);
    layout->addWidget(individual);
    auto *providerFields = new QWidget(dialog);
    auto *providerForm = new QFormLayout(providerFields);
    providerForm->setContentsMargins(0, 0, 0, 0);
    const std::array<QString, 3> names{"7TV", "BTTV", "FFZ"};
    std::array<QLineEdit *, 3> inputs{};
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        inputs.at(i) =
            new QLineEdit(sources.providerTwitchIDs.at(i), providerFields);
        inputs.at(i)->setObjectName("EmoteSourceID" + names.at(i));
        providerForm->addRow(names.at(i) + " Twitch user ID", inputs.at(i));
    }
    layout->addWidget(providerFields);
    providerFields->setVisible(individual->isChecked());
    sharedID->setEnabled(!individual->isChecked());
    const auto updatePlaceholders = [weak = std::weak_ptr(channel), sharedID,
                                     inputs] {
        const auto current = weak.lock();
        const auto id = current ? current->roomId() : QString{};
        sharedID->setPlaceholderText(id);
        for (auto *input : inputs)
        {
            input->setPlaceholderText(id);
        }
    };
    updatePlaceholders();
    // Disconnect even if Qt destroys the callback without invoking it.
    auto idConnection = std::make_shared<pajlada::Signals::ScopedConnection>(
        channel->roomIdChanged.connect(updatePlaceholders));
    QObject::connect(dialog, &QObject::destroyed, dialog,
                     [connection = std::move(idConnection)]() mutable {
                         connection.reset();
                     });
    const bool seedProviders =
        !sources.individualProviders &&
        std::ranges::all_of(sources.providerTwitchIDs, [](const auto &id) {
            return id.isEmpty();
        });
    QObject::connect(individual, &QCheckBox::toggled, dialog,
                     [dialog, providerFields, sharedID, inputs,
                      seedProviders = seedProviders](bool checked) mutable {
                         if (checked && seedProviders)
                         {
                             for (auto *input : inputs)
                             {
                                 input->setText(sharedID->text());
                             }
                             seedProviders = false;
                         }
                         sharedID->setEnabled(!checked);
                         providerFields->setVisible(checked);
                         dialog->adjustSize();
                     });

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                 QDialogButtonBox::RestoreDefaults,
                             dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                     &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults),
                     &QPushButton::clicked, dialog,
                     [sharedID, individual, inputs] {
                         sharedID->clear();
                         individual->setChecked(false);
                         for (auto *input : inputs)
                         {
                             input->clear();
                         }
                     });
    QObject::connect(
        buttons, &QDialogButtonBox::accepted, dialog,
        [dialog, channel, sharedID, individual, inputs] {
            ChannelEmoteSources updated{
                .sharedTwitchID = sharedID->text(),
                .individualProviders = individual->isChecked(),
                .providerTwitchIDs = {},
            };
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                updated.providerTwitchIDs.at(i) = inputs.at(i)->text();
            }
            if (!channel->setEmoteSources(std::move(updated)))
            {
                QMessageBox::warning(dialog, "Invalid Twitch user ID",
                                     "Use positive numeric Twitch user IDs. "
                                     "Leave a field blank to use its default.");
                return;
            }
            dialog->accept();
        });
    dialog->setMinimumWidth(420);
    dialog->show();
    sharedID->setFocus();
}

}  // namespace chatterino
