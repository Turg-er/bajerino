#include <cryptopp/aes.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/modes.h>

#include <string>
const size_t BLOCK_SIZE = CryptoPP::AES::BLOCKSIZE;
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
    auto encoded = base64Encode(ciphertext);
    encoded.erase(std::remove_if(encoded.begin(), encoded.end(), ::isspace),
                  encoded.end());
    return encoded;
}
std::string AES_decrypt(const std::string &ciphertext, const std::string &key)

{
    std::string decoded = base64Decode(ciphertext);
    if (decoded.size() == 0)
    {
        return "Failed to decrypt the message, message was empty";
    }
    if (decoded.size() % BLOCK_SIZE != 0)
    {
        return "Failed to decrypt the message, incorrect block size, it "
               "was " +
               std::to_string(decoded.size());
    }
    ECB_Mode<AES>::Decryption d;
    d.SetKey((const byte *)key.c_str(), key.size());

    std::string recovered;
    StringSource s(
        decoded, true,
        new StreamTransformationFilter(d, new StringSink(recovered)));
    return recovered;
}
