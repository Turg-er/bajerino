#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

namespace chatterino {

QString encryptMessage(QStringView message, QStringView encryptionPassword);

bool checkAndDecryptMessage(QString &message, QStringView encryptionPassword);

}  // namespace chatterino
