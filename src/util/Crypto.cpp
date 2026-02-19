#include "util/Crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <QChar>
#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace {

const QChar STARTING_CHAR(L'一');

using EVP_CIPHER_CTX_ptr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

const EVP_CIPHER *getCipherAlgorithm()
{
    return EVP_aes_128_cbc();
}

bool isCharOutsideValidEncodeRange(const QChar &c)
{
    return c < STARTING_CHAR || c > QChar(STARTING_CHAR.unicode() + 0xFF);
}

QByteArray aesEncrypt(const QByteArray &plaintext, const unsigned char *key,
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

    int outLen1 = ciphertext.size();
    rc = EVP_EncryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(ciphertext.data()),
        &outLen1,
        reinterpret_cast<const unsigned char *>(plaintext.constData()),
        plaintext.size());
    if (rc != 1)
    {
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    int outLen2 = ciphertext.size() - outLen1;
    rc = EVP_EncryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(ciphertext.data()) + outLen1,
        &outLen2);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }

    ciphertext.resize(outLen1 + outLen2);
    return ciphertext;
}

QByteArray aesDecrypt(const QByteArray &ciphertext, const unsigned char *key,
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

    int outLen1 = plaintext.size();
    rc = EVP_DecryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(plaintext.data()),
        &outLen1,
        reinterpret_cast<const unsigned char *>(ciphertext.constData()),
        ciphertext.size());
    if (rc != 1)
    {
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }

    int outLen2 = plaintext.size() - outLen1;
    rc = EVP_DecryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(plaintext.data()) + outLen1,
        &outLen2);
    if (rc != 1)
    {
        throw std::runtime_error("EVP_DecryptFinal_ex failed");
    }

    plaintext.resize(outLen1 + outLen2);
    return plaintext;
}

QString bytesToChineseCharacters(QByteArrayView bytes)
{
    QString encoded;
    encoded.reserve(bytes.size());

    for (const auto byte : bytes)
    {
        encoded +=
            QChar(STARTING_CHAR.unicode() + static_cast<unsigned char>(byte));
    }

    return encoded;
}

QByteArray chineseCharactersToBytes(QStringView chars)
{
    QByteArray bytes(chars.length(), 0);
    for (int i = 0; i < chars.length(); i++)
    {
        const auto c = chars.at(i);
        if (isCharOutsideValidEncodeRange(c))
        {
            throw std::runtime_error("Invalid character in encrypted payload");
        }

        bytes[i] = static_cast<unsigned char>(
            (c.unicode() - STARTING_CHAR.unicode()) & 0xFF);
    }

    return bytes;
}

QByteArray deriveEncryptionKey(QStringView encryptionPassword)
{
    auto key = QCryptographicHash::hash(encryptionPassword.toUtf8(),
                                        QCryptographicHash::Blake2b_256);
    key.resize(16);
    return key;
}

}  // namespace

namespace chatterino {

QString encryptMessage(QStringView message, QStringView encryptionPassword)
{
    try
    {
        auto encryptionKey = deriveEncryptionKey(encryptionPassword);

        std::array<unsigned char, 16> iv{};
        RAND_bytes(iv.data(), static_cast<int>(iv.size()));

        auto encryptedText = aesEncrypt(message.toUtf8(),
                                        reinterpret_cast<const unsigned char *>(
                                            encryptionKey.constData()),
                                        iv.data())
                                 .prepend(QByteArray::fromRawData(
                                     reinterpret_cast<const char *>(iv.data()),
                                     static_cast<int>(iv.size())));

        return bytesToChineseCharacters(encryptedText);
    }
    catch (const std::runtime_error &)
    {
        return "";
    }
}

bool isMaybeEncrypted(QStringView message)
{
    return message.length() >= 32 &&
           std::ranges::none_of(message, isCharOutsideValidEncodeRange);
}

bool decryptMessage(QString &message, QStringView encryptionPassword)
{
    try
    {
        auto encryptedBytes = chineseCharactersToBytes(message);
        if (encryptedBytes.size() < 32)
        {
            throw std::runtime_error("Potential text is too small");
        }

        auto encryptionKey = deriveEncryptionKey(encryptionPassword);
        auto cipherText = encryptedBytes.sliced(16);
        encryptedBytes.resize(16);

        message = QString::fromUtf8(aesDecrypt(
            cipherText,
            reinterpret_cast<const unsigned char *>(encryptionKey.constData()),
            reinterpret_cast<const unsigned char *>(
                encryptedBytes.constData())));
        return true;
    }
    catch (const std::runtime_error &)
    {
    }

    return false;
}

}  // namespace chatterino
