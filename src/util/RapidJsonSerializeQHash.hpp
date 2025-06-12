#pragma once

#include <pajlada/serialize.hpp>
#include "util/RapidJsonSerializeQString.hpp" // IWYU pragma: keep
#include <QString>
#include <QHash>


namespace pajlada {

template <typename ValueType, typename RJValue>
struct Serialize<QHash<QString, ValueType>, RJValue> {
    static RJValue
    get(const QHash<QString, ValueType> &value,
        typename RJValue::AllocatorType &a)
    {
        RJValue ret(rapidjson::kObjectType);

        for (auto it = value.constBegin(); it != value.constEnd(); ++it) {
            detail::AddMember<ValueType, RJValue>(ret, it.key().toUtf8().constData(),
                                                  it.value(), a);
        }

        return ret;
    }
};

template <typename ValueType, typename RJValue>
struct Deserialize<QHash<QString, ValueType>, RJValue> {
    static QHash<QString, ValueType>
    get(const RJValue &value, bool *error = nullptr)
    {
        QHash<QString, ValueType> ret;

        if (!value.IsObject()) {
            PAJLADA_REPORT_ERROR(error)
            return ret;
        }

        for (typename RJValue::ConstMemberIterator it = value.MemberBegin();
             it != value.MemberEnd(); ++it) {
            ret.emplace(QString::fromUtf8(it->name.GetString(),
                                     it->name.GetStringLength()),
                        Deserialize<ValueType, RJValue>::get(it->value, error));
        }

        return ret;
    }
};

}  // namespace pajlada
