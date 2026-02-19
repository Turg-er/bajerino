#pragma once

#include <QString>
#include <QStringView>

namespace chatterino {

QString encryptMessage(QStringView message, QStringView encryptionPassword);

bool decryptMessage(QString &message, QStringView encryptionPassword);

bool isMaybeEncrypted(QStringView message);

}  // namespace chatterino
