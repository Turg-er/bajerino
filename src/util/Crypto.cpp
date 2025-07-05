#include "util/Crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <QChar>
#include <QCryptographicHash>

namespace {

/*
* end char should be 仿
* https://en.wikibooks.org/wiki/Unicode/Character_reference/4000-4FFF
*/
const QChar STARTING_CHINESE_CHAR(L'一');

using EVP_CIPHER_CTX_ptr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

const EVP_CIPHER *getCipherAlgorithm()
{
    return EVP_aes_128_cbc();
}

QByteArray aesEncrypt(QByteArrayView plaintext, const unsigned char *key,
                      const unsigned char *iv)
{
    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
    int rc =
        EVP_EncryptInit_ex2(ctx.get(), getCipherAlgorithm(), key, iv, nullptr);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_EncryptInit_ex2 failed");
    }

    auto blockSize = EVP_CIPHER_CTX_block_size(ctx.get());

    QByteArray ciphertext(plaintext.size() + blockSize, '\0');
    int outLen1 = static_cast<int>(ciphertext.size());

    rc = EVP_EncryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(ciphertext.data()),
        &outLen1,
        reinterpret_cast<const unsigned char *>(plaintext.constData()),
        static_cast<int>(plaintext.size()));
    if (rc != 1)
    {
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    int outLen2 = static_cast<int>(ciphertext.size()) - outLen1;
    rc = EVP_EncryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(ciphertext.data()) + outLen1,
        &outLen2);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }

    // Set cipher text size now that we know it
    ciphertext.resize(outLen1 + outLen2);
    return ciphertext;
}

QByteArray aesDecrypt(QByteArrayView ciphertext, const unsigned char *key,
                      const unsigned char *iv)
{
    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
    int rc =
        EVP_DecryptInit_ex2(ctx.get(), getCipherAlgorithm(), key, iv, nullptr);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_DecryptInit_ex2 failed");
    }

    auto blockSize = EVP_CIPHER_CTX_block_size(ctx.get());

    QByteArray plaintext(ciphertext.size() + blockSize, '\0');
    int outLen1 = static_cast<int>(plaintext.size());

    rc = EVP_DecryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(plaintext.data()),
        &outLen1,
        reinterpret_cast<const unsigned char *>(ciphertext.constData()),
        static_cast<int>(ciphertext.size()));
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptUpdate failed");

    int outLen2 = static_cast<int>(plaintext.size()) - outLen1;
    rc = EVP_DecryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(plaintext.data()) + outLen1,
        &outLen2);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_DecryptFinal_ex failed");
    }

    // Set cipher text size now that we know it
    plaintext.resize(outLen1 + outLen2);
    return plaintext;
}

QString bytesToChineseChararacters(QByteArrayView bytes)
{
    QString encoded(bytes.length(), '\0');
    for (auto i = 0; i < bytes.length(); i++)
    {
        encoded[i] = QChar(STARTING_CHINESE_CHAR.unicode() +
                           static_cast<unsigned char>(bytes.at(i)));
    }
    return encoded;
}

bool isCharOutsideValidEncodeRange(const QChar &c)
{
    return c < STARTING_CHINESE_CHAR ||
           c > QChar(STARTING_CHINESE_CHAR.unicode() + 0xFF);
}

QByteArray chineseCharactersToBytes(QStringView characters)
{
    QByteArray bytes(characters.length(), '\0');
    for (auto i = 0; i < characters.length(); i++)
    {
        if (isCharOutsideValidEncodeRange(characters.at(i)))
        {
            throw std::runtime_error("Invalid characters input converting from "
                                     "chinese to bytes. Invalid character at " +
                                     std::to_string(i));
        }
        bytes[i] = static_cast<char>(
            (characters.at(i).unicode() - STARTING_CHINESE_CHAR.unicode()) &
            0xFF);
    }
    return bytes;
}

}  // namespace

namespace chatterino {

QString encryptMessage(QStringView message, QStringView encryptionPassword)
{
    try
    {
        /*
        * One might ask why not use `PKCS5_PBKDF2_HMAC` or `EVP_BytesToKey` as those are meant for deriving a key from a password.
        * You're right, but as we're using passwords stored in plaintext on the users computer,
        * there's no need to compute a more cryptographically secure key.
        */
        auto encryptionKey = QCryptographicHash::hash(
            encryptionPassword.toUtf8(), QCryptographicHash::Blake2b_256);
        encryptionKey.resize(16);
        unsigned char iv[16] = {};
        RAND_bytes(iv, 16);
        auto encryptedText =
            aesEncrypt(message.toUtf8(),
                       reinterpret_cast<const unsigned char *>(
                           encryptionKey.constData()),
                       iv)
                .prepend(reinterpret_cast<const char *>(iv), 16);
        return bytesToChineseChararacters(encryptedText);
    }
    catch (const std::runtime_error &e)
    {
        return "";
    }
}

bool isMaybeEncrypted(QStringView message)
{
    return message.length() >= 32 &&
           std::ranges::none_of(message.cbegin(), message.cend(), [](auto c) {
               return isCharOutsideValidEncodeRange(c);
           });
}

bool decryptMessage(QString &message, QStringView encryptionPassword)
{
    try
    {
        auto encryptedBytes = chineseCharactersToBytes(message);
        if (encryptedBytes.size() < 32)
        {
            throw std::runtime_error("Potential text is too small!");
        }
        // if wondering why using non secure hash see reasoning in encryptMessage above
        auto encryptionKey = QCryptographicHash::hash(
            encryptionPassword.toUtf8(), QCryptographicHash::Blake2b_256);
        encryptionKey.resize(16);

        auto cipherText = encryptedBytes.sliced(16);
        // this is just the iv now
        encryptedBytes.resize(16);

        message = QString::fromUtf8(aesDecrypt(
            cipherText,
            reinterpret_cast<const unsigned char *>(encryptionKey.constData()),
            reinterpret_cast<const unsigned char *>(
                encryptedBytes.constData())));
        return true;
    }
    catch (const std::runtime_error &e)
    {
    }
    return false;
}

}  // namespace chatterino
