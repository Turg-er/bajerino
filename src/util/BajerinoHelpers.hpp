#pragma once

#include <QString>
#include <QStringView>

namespace chatterino {

bool isBig3(QStringView userID);

QString noticeBig3(const QString &username);

}  // namespace chatterino
