// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/IrcHelpers.hpp"

#include "Application.hpp"

namespace {

using namespace chatterino;

QDateTime calculateMessageTimeBase(const Communi::IrcMessage *message)
{
    if (message->tags().has("historical"))
    {
        bool customReceived = false;
        auto ts = message->tags()
                      .getOrEmpty("rm-received-ts")
                      .toLongLong(&customReceived);
        if (!customReceived)
        {
            ts = message->tags().getOrEmpty("tmi-sent-ts").toLongLong();
        }

        return QDateTime::fromMSecsSinceEpoch(ts);
    }

    if (message->tags().has("tmi-sent-ts"))
    {
        auto ts = message->tags().getOrEmpty("tmi-sent-ts").toLongLong();
        return QDateTime::fromMSecsSinceEpoch(ts);
    }

    if (message->tags().has("time"))
    {
        QString timedate = message->tags().getOrEmpty("time");

        auto date = QDateTime::fromString(timedate, Qt::ISODate);
        date.setTimeZone(QTimeZone::utc());
        return date.toLocalTime();
    }

#ifdef CHATTERINO_WITH_TESTS
    if (getApp()->isTest())
    {
        return QDateTime::fromMSecsSinceEpoch(0, QTimeZone::utc());
    }
#endif

    return QDateTime::currentDateTime();
}

}

namespace chatterino {

QDateTime calculateMessageTime(const Communi::IrcMessage *message)
{
    auto dt = calculateMessageTimeBase(message);

#ifdef CHATTERINO_WITH_TESTS
    if (getApp()->isTest())
    {
        return dt.toUTC();
    }
#endif

    return dt;
}

}
