
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/modes.h>

#include <iostream>

using namespace CryptoPP;

std::string AES_encrypt(const std::string &plaintext, const std::string &key);

std::string AES_decrypt(const std::string &ciphertext, const std::string &key);
