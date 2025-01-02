#include <QByteArray>

namespace chatterino {

QString encryptMessage(QString &message, const QString &encryptionPassword);

bool checkAndDecryptMessage(QString &message,
                            const QString &encryptionPassword);

}  // namespace chatterino
