// SPDX-FileCopyrightText: 2020 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/network/NetworkRequest.hpp"
#include "providers/twitch/api/Helix.hpp"

#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <optional>

namespace chatterino {

using IvrFailureCallback = std::function<void()>;
template <typename... T>
using ResultCallback = std::function<void(T...)>;

struct IvrSubage {
    const bool isSubHidden;
    const bool isSubbed;
    const QString subTier;
    const int totalSubMonths;
    const QString followingSince;

    IvrSubage(const QJsonObject &root)
        : isSubHidden(root.value("statusHidden").toBool())
        , isSubbed(!root.value("meta").isNull())
        , subTier(root.value("meta").toObject().value("tier").toString())
        , totalSubMonths(
              root.value("cumulative").toObject().value("months").toInt())
        , followingSince(root.value("followedAt").toString())
    {
    }
};

struct IvrUserProfile {
    bool banned = false;
    std::optional<int> chatterCount;
    QString chatColor;
    bool isAffiliate = false;
    bool isPartner = false;
    bool isStaff = false;
    QString lastBroadcastStartedAt;
    QString lastBroadcastTitle;

    IvrUserProfile() = default;
    explicit IvrUserProfile(const QJsonObject &root)
        : banned(root.value("banned").toBool())
        , chatColor(root.value("chatColor").toString())
    {
        const auto chatterCountValue = root.value("chatterCount");
        if (!chatterCountValue.isNull() && !chatterCountValue.isUndefined())
        {
            this->chatterCount = chatterCountValue.toInt();
        }

        const auto roles = root.value("roles").toObject();
        this->isAffiliate = roles.value("isAffiliate").toBool();
        this->isPartner = roles.value("isPartner").toBool();
        this->isStaff = roles.value("isStaff").toBool();

        const auto lastBroadcast = root.value("lastBroadcast").toObject();
        this->lastBroadcastStartedAt =
            lastBroadcast.value("startedAt").toString();
        this->lastBroadcastTitle = lastBroadcast.value("title").toString();
    }
};

class IvrApi final
{
public:
    // https://api.ivr.fi/v2/docs/static/index.html#/Twitch/get_twitch_subage__user___channel_
    void getSubage(QString userName, QString channelName,
                   ResultCallback<IvrSubage> resultCallback,
                   IvrFailureCallback failureCallback);
    // https://api.ivr.fi/v2/docs/#tag/twitch/GET/twitch/modvip/{channel}
    void getModVip(const QString &channelName,
                   const ResultCallback<std::vector<HelixModerator>,
                                        std::vector<HelixVip>> &successCallback,
                   const IvrFailureCallback &failureCallback);
    // https://api.ivr.fi/v2/docs/#tag/twitch/GET/twitch/founders/{login}
    void getFounders(
        const QString &channelName,
        const ResultCallback<std::vector<HelixModerator>> &successCallback,
        const IvrFailureCallback &failureCallback);
    // https://api.ivr.fi/v2/docs/#tag/twitch/GET/twitch/user
    void getUser(const QString &userName,
                 const ResultCallback<IvrUserProfile> &successCallback,
                 const IvrFailureCallback &failureCallback);

    static void initialize();

    IvrApi() = default;

    IvrApi(const IvrApi &) = delete;
    IvrApi &operator=(const IvrApi &) = delete;

    IvrApi(IvrApi &&) = delete;
    IvrApi &operator=(IvrApi &&) = delete;

private:
    static NetworkRequest makeRequest(const QString &url,
                                      const QUrlQuery &urlQuery);
};

IvrApi *getIvr();

}  // namespace chatterino
