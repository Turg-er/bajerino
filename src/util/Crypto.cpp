#include <cryptopp/aes.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/modes.h>

#include <iostream>
#include <string>

using namespace CryptoPP;
std::string base64Encode(const std::string &input)
{
    std::string encoded;
    StringSource s(input, true, new Base64Encoder(new StringSink(encoded)));
    return encoded;
}
std::string base64Decode(const std::string &input)
{
    std::string decoded;
    StringSource s(input, true, new Base64Decoder(new StringSink(decoded)));
    return decoded;
}
std::string AES_encrypt(const std::string &plaintext, const std::string &key)
{
    ECB_Mode<AES>::Encryption e;
    e.SetKey((const byte *)key.c_str(), key.size());

    std::string ciphertext;

    StringSource s(
        plaintext, true,
        new StreamTransformationFilter(e, new StringSink(ciphertext)));
    return base64Encode(ciphertext);
}
std::string AES_decrypt(const std::string &ciphertext, const std::string &key)

{
    std::string decoded = base64Decode(ciphertext);
    ECB_Mode<AES>::Decryption d;
    d.SetKey((const byte *)key.c_str(), key.size());

    std::string recovered;
    StringSource s(
        decoded, true,
        new StreamTransformationFilter(d, new StringSink(recovered)));
    return recovered;
}
