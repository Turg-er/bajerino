// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>
#include <rapidjson/document.h>

#include <optional>

namespace chatterino {

class NetworkResult
{
public:
    using NetworkError = QNetworkReply::NetworkError;

    NetworkResult(NetworkError error, const QVariant &httpStatusCode,
                  QByteArray data, QByteArray etag = {}, QUrl url = {});

    /// Parses the result as json and returns the root as an object.
    /// Returns empty object if parsing failed.
    QJsonObject parseJson() const;
    /// Parses the result as json and returns the root as an array.
    /// Returns empty object if parsing failed.
    QJsonArray parseJsonArray() const;

    /// Parses the result as json and returns the root as a generic value.
    /// Returns empty object if parsing failed.
    ///
    /// In Qt 6.9+, this will parse scalar values as well.
    QJsonValue parseJsonValue() const;

    /// Parses the result as json and returns the document.
    rapidjson::Document parseRapidJson() const;
    const QByteArray &getData() const;
    const QByteArray &etag() const;
    const QUrl &url() const;

    /// The error code of the reply.
    /// In case of a successful reply, this will be NoError (0)
    NetworkError error() const
    {
        return this->error_;
    }

    /// The HTTP status code if a response was received.
    std::optional<int> status() const
    {
        return this->status_;
    }

    /// Formats the error.
    /// If a reply is received, returns the HTTP status otherwise, the network error.
    QString formatError() const;

private:
    QByteArray data_;
    QByteArray etag_;
    QUrl url_;

    NetworkError error_;
    std::optional<int> status_;
};

}  // namespace chatterino
