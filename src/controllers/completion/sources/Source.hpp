// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>

namespace chatterino {
class GenericListModel;
}  // namespace chatterino

namespace chatterino::completion {

struct EmoteItem;

class Source
{
public:
    virtual ~Source() = default;

    /// @brief Updates the internal completion suggestions for the given query
    /// @param query Query to complete against
    virtual void update(const QString &query) = 0;

    /// @brief Appends the internal completion suggestions to a GenericListModel
    /// @param model GenericListModel to add suggestions to
    /// @param maxCount Maximum number of suggestions. Zero indicates unlimited.
    virtual void addToListModel(GenericListModel &model,
                                size_t maxCount = 0) const = 0;

    /// @brief Appends the internal completion suggestions to a QStringList
    /// @param list QStringList to add suggestions to
    /// @param maxCount Maximum number of suggestions. Zero indicates unlimited.
    /// @param isFirstWord Whether the completion is the first word in the input
    virtual void addToStringList(QStringList &list, size_t maxCount = 0,
                                 bool isFirstWord = false) const = 0;
    virtual qsizetype tabCompletionCount() const = 0;
    virtual const EmoteItem *emoteAtTabCompletionIndex(
        qsizetype index) const = 0;
};

};  // namespace chatterino::completion
