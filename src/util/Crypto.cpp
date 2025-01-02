#include "singletons/Settings.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <QChar>
#include <QCryptographicHash>
static const QString ENCRYPTED_MESSAGE_PREFIX_DEPRECATED("~!");
static const QString LEGACY_ENCRYPTED_MESSAGE_PREFIX("~#");

/*
* end char should be 仿
* https://www.khngai.com/chinese/charmap/tbluni.php?page=0
*/
static const QChar STARTING_CHINESE_CHAR(L'一');

using EVP_CIPHER_CTX_ptr =
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

const EVP_CIPHER *get_cipher_algorithm()
{
    return EVP_aes_128_cbc();
}

QByteArray aes_encrypt(const QByteArray &plaintext, const QByteArray &key,
                       unsigned char *iv)
{
    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
    int rc = EVP_EncryptInit_ex2(
        ctx.get(), get_cipher_algorithm(),
        reinterpret_cast<unsigned const char *>(key.constData()), iv, nullptr);
    if (rc != 1)
        throw std::runtime_error("EVP_EncryptInit_ex2 failed");

    auto BLOCK_SIZE = EVP_CIPHER_CTX_block_size(ctx.get());

    QByteArray ciphertext(plaintext.size() + BLOCK_SIZE, '\0');
    int out_len1 = ciphertext.size();

    rc = EVP_EncryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(ciphertext.data()),
        &out_len1,
        reinterpret_cast<unsigned const char *>(plaintext.constData()),
        plaintext.size());
    if (rc != 1)
        throw std::runtime_error("EVP_EncryptUpdate failed");

    int out_len2 = ciphertext.size() - out_len1;
    rc = EVP_EncryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(ciphertext.data()) + out_len1,
        &out_len2);
    if (rc != 1)
        throw std::runtime_error("EVP_EncryptFinal_ex failed");

    // Set cipher text size now that we know it
    ciphertext.resize(out_len1 + out_len2);
    return ciphertext;
}

QByteArray aes_decrypt(const QByteArray &ciphertext, const QByteArray &key,
                       const QByteArray &iv)
{
    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
    int rc = EVP_DecryptInit_ex2(
        ctx.get(), get_cipher_algorithm(),
        reinterpret_cast<unsigned const char *>(key.constData()),
        reinterpret_cast<unsigned const char *>(iv.constData()), nullptr);
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptInit_ex2 failed");

    auto BLOCK_SIZE = EVP_CIPHER_CTX_block_size(ctx.get());

    QByteArray plaintext(ciphertext.size() + BLOCK_SIZE, '\0');
    int out_len1 = plaintext.size();

    rc = EVP_DecryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(plaintext.data()),
        &out_len1,
        reinterpret_cast<unsigned const char *>(ciphertext.constData()),
        ciphertext.size());
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptUpdate failed");

    int out_len2 = plaintext.size() - out_len1;
    rc = EVP_DecryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(plaintext.data()) + out_len1,
        &out_len2);
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptFinal_ex failed");

    // Set cipher text size now that we know it
    plaintext.resize(out_len1 + out_len2);
    return plaintext;
}

/*
* legacy used a 24 byte key
*/
QByteArray legacy_aes_decrypt(const QByteArray &ciphertext, const QString &key)
{
    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
    auto keyByteArray = key.toUtf8();
    int rc = EVP_DecryptInit_ex2(
        ctx.get(), EVP_aes_192_ecb(),
        reinterpret_cast<unsigned const char *>(keyByteArray.constData()),
        nullptr, nullptr);
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptInit_ex2 failed");

    auto BLOCK_SIZE = EVP_CIPHER_CTX_block_size(ctx.get());
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptInit_ex2 to set key failed");
    QByteArray plaintext(ciphertext.size() + BLOCK_SIZE, '\0');
    int out_len1 = plaintext.size();

    rc = EVP_DecryptUpdate(
        ctx.get(), reinterpret_cast<unsigned char *>(plaintext.data()),
        &out_len1,
        reinterpret_cast<unsigned const char *>(ciphertext.constData()),
        ciphertext.size());
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptUpdate failed");

    int out_len2 = plaintext.size() - out_len1;
    rc = EVP_DecryptFinal_ex(
        ctx.get(),
        reinterpret_cast<unsigned char *>(plaintext.data()) + out_len1,
        &out_len2);
    if (rc != 1)
        throw std::runtime_error("EVP_DecryptFinal_ex failed");

    // Set cipher text size now that we know it
    plaintext.resize(out_len1 + out_len2);
    return plaintext;
}

QString bytesToChineseChararacters(QByteArrayView bytes)
{
    QString encoded = "";
    for (auto it = bytes.begin(); it != bytes.end(); it++)
    {
        encoded += QChar(STARTING_CHINESE_CHAR.unicode() +
                         static_cast<unsigned char>(*it));
    }
    return encoded;
}

QByteArray chineseCharactersToBytes(QStringView characters)
{
    QByteArray bytes(characters.length(), 0);
    for (int i = 0; i < characters.length(); i++)
    {
        if (characters.at(i) < STARTING_CHINESE_CHAR ||
            characters.at(i) > QChar(STARTING_CHINESE_CHAR.unicode() + 0xFF))
        {
            throw std::runtime_error("Invalid characters input converting from "
                                     "chinese to bytes. Invalid character at " +
                                     std::to_string(i));
        }
        bytes[i] = static_cast<unsigned char>(
            (characters.at(i).unicode() - STARTING_CHINESE_CHAR.unicode()) &
            0xFF);
    }
    return bytes;
}

QString encryptMessage(QString &message, const QString &encryptionPassword)
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
        auto encryptedText = aes_encrypt(message.toUtf8(), encryptionKey, iv)
                                 .prepend(QByteArray::fromRawData(
                                     reinterpret_cast<const char *>(iv), 16));
        if (chatterino::getSettings()->legacyEncryptionPrefix)
        {
            return ENCRYPTED_MESSAGE_PREFIX_DEPRECATED %
                   bytesToChineseChararacters(encryptedText);
        }
        else
        {
            return bytesToChineseChararacters(encryptedText);
        }
    }
    catch (const std::runtime_error &e)
    {
        return "";
    }
}

bool containsOnlyPrintable(const QString &str)
{
    return str.contains(QRegularExpression("^[\\x{0000}-\\x{0600}]*$"));
}

bool checkAndDecryptMessage(QString &message, const QString &encryptionPassword)
{
    if (message.startsWith(LEGACY_ENCRYPTED_MESSAGE_PREFIX))
    {
        try
        {
            /*
            * legacy, encryptionPassword is assumed to be a 24 byte key
            */
            if (encryptionPassword.size() != 24)
            {
                throw std::runtime_error(
                    "Encryption key is not correct length. Encryption key must "
                    "be 24 bytes long!");
            }
            auto encryptedBytes =
                QByteArray::fromBase64(message.mid(2).toUtf8());
            message = QString::fromUtf8(
                legacy_aes_decrypt(encryptedBytes, encryptionPassword));
            return true;
        }
        catch (const std::runtime_error &e)
        {
        }
    }
    // in theory we dont need to check for prefixes and just do the decode on
    // every message and just check if it makes sense.
    // the biggest downside of that method is performance, question is how much
    // of a performance decrease it that
    try
    {
        // if wondering why using non secure hash see reasoning in encryptMessage above
        auto encryptionKey = QCryptographicHash::hash(
            encryptionPassword.toUtf8(), QCryptographicHash::Blake2b_256);
        encryptionKey.resize(16);

        if (message.startsWith(ENCRYPTED_MESSAGE_PREFIX_DEPRECATED))
        {
            message = message.mid(2);
        }
        auto encryptedBytes = chineseCharactersToBytes(message);
        if (encryptedBytes.size() < 17)
        {
            throw std::runtime_error("Potential text is too small!");
        }
        auto cipherText = encryptedBytes.sliced(16);
        encryptedBytes.resize(16);  // this is the IV
        auto message_decrypted = QString::fromUtf8(
            aes_decrypt(cipherText, encryptionKey, encryptedBytes));

        // finally check if the decrypted message makes sense for english
        if (!containsOnlyPrintable(message_decrypted))
        {
            return false;
        }
        message = message_decrypted;
        return true;
    }
    catch (const std::runtime_error &e)
    {
    }
    return false;
}
