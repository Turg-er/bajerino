# /// script
# dependencies = [
#   "cryptography==44.0.0",
# ]
# ///

import argparse
from cryptography.hazmat.primitives.ciphers.modes import CBC
from cryptography.hazmat.primitives.ciphers.algorithms import AES
from cryptography.hazmat.primitives.ciphers import Cipher
from cryptography.hazmat.primitives import padding
from os import urandom
from hashlib import blake2b


def encrypt_input(input_text: str, encryption_key: bytes) -> str:
    """Encrypt the input cipher text using AES with CBC mode."""

    padder = padding.PKCS7(128).padder()
    padded = padder.update(input_text.encode("utf-8")) + padder.finalize()

    iv = urandom(16)

    cipher = Cipher(AES(encryption_key[0:16]), CBC(iv))

    encryptor = cipher.encryptor()
    encrypted = encryptor.update(padded) + encryptor.finalize()

    return "".join([chr(b + ord("一")) for b in iv]) + "".join(
        [chr(b + ord("一")) for b in encrypted]
    )


def main() -> None:
    """Main function to handle command-line arguments and decryption."""
    parser = argparse.ArgumentParser(
        description="Encrypt a cipher text with AES and CBC mode."
    )

    parser.add_argument(
        "--encryption_password",
        type=str,
        help="The password used to generate the encryption key",
    )

    args = parser.parse_args()

    # Generate encryption key from password
    encryption_key: bytes = blake2b(
        args.encryption_password.encode("utf-8"), digest_size=32
    ).digest()

    while True:
        input_text = input("Enter cipher text: ")
        try:
            print(encrypt_input(input_text, encryption_key))
        except ValueError as e:
            print(f"failed to decrypt message: {e}")


if __name__ == "__main__":
    main()
