// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/serialize.hpp>
#include <QStringView>

namespace pajlada {

template <>
struct Serialize<QStringView> {
    static rapidjson::Value get(QStringView value,
                                rapidjson::Document::AllocatorType &a)
    {
        const auto utf8 = value.toUtf8();
        return {utf8.constData(), static_cast<rapidjson::SizeType>(utf8.size()),
                a};
    }
};

}  // namespace pajlada
