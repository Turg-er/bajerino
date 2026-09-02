// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/pronouns/UserPronouns.hpp"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace chatterino::pronouns {

class AlejoApi
{
public:
    AlejoApi();

    /// Fetch the user's pronouns from the alejo.io API
    ///
    /// onDone can be invoked from any thread
    ///
    /// The argument is std::nullopt if and only if the request failed.
    void fetch(const QString &username,
               const std::function<void(std::optional<UserPronouns>)> &onDone);

private:
    void loadAvailablePronouns();
    void scheduleAvailablePronounsRetry();

    struct PronounEntry {
        QString subject;
        QString object;
        bool singular;
    };

    /// Maps alejo.io pronoun IDs to their human readable representation (subject, object, and singularity)
    std::unordered_map<QString, PronounEntry> pronouns;
    std::shared_mutex mutex;
    std::atomic_bool pronounsLoadInFlight_{false};
    std::atomic_int pronounsLoadRetryCount_{0};

    /// Parse a pronoun definition from the /users endpoint into a finished UserPronouns
    UserPronouns parsePronoun(const QJsonObject &object);
};

}  // namespace chatterino::pronouns
