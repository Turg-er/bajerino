import argparse
from cryptography.hazmat.primitives.ciphers.modes import CBC
from cryptography.hazmat.primitives.ciphers.algorithms import AES
from cryptography.hazmat.primitives.ciphers import Cipher
from cryptography.hazmat.primitives import padding
from hashlib import blake2b

import re

def decrypt_input(input_text: str, encryption_key: bytes) -> str:
    """Decrypt the input cipher text using AES with CBC mode."""
    # first 16 bytes are iv, rest are ciphertext
    for char in input_text:
        cord = ord(char)
        if cord < ord("一") or cord > ord("一") + 255:
            raise ValueError("Input is not in corect character range")
    cipher_bytes = bytearray([ord(char) - ord("一") for char in input_text])

    cipher = Cipher(AES(encryption_key[0:16]), CBC(cipher_bytes[0:16]))

    decryptor = cipher.decryptor()
    decrypted = decryptor.update(cipher_bytes[16:]) + decryptor.finalize()

    unpadder = padding.PKCS7(128).unpadder()
    unpadded = unpadder.update(decrypted) + unpadder.finalize()

    return unpadded.decode("utf-8")

raw_log_regex = re.compile(r"^(\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] #\w+ \w+: (?:@\w+ )?)(.+)$")

def main() -> None:
    """Main function to handle command-line arguments and decryption."""
    parser = argparse.ArgumentParser(description="Decrypt a cipher text with AES and CBC mode.")

    parser.add_argument(
        "encryption_password",
        type=str,
        help="The password used to generate the encryption key"
    )

    parser.add_argument(
        "--multiline",
        action="store_true",
        help="Input cipher text in Rustlog/Justlog format"
    )

    parser.add_argument(
        "--input-file",
        type=str,
        help="Input file containing the cipher text if --multiline is selected"
    )

    args = parser.parse_args()

    # Generate encryption key from password
    encryption_key: bytes = blake2b(args.encryption_password.encode('utf-8'), digest_size=32).digest()

    if args.multiline:
        if args.input_file:
            with open(args.input_file, 'r', encoding='utf-8') as file:
                for line in file:
                    line = line.strip()
                    parsed = raw_log_regex.match(line)
                    if parsed is None:
                        print(line)
                        continue
                    try:
                        print(f"{parsed.group(1)}{decrypt_input(parsed.group(2), encryption_key)}")
                    except ValueError:
                        print(line)
        else:
            lines = [(input("Paste your multiline cipher text here:\n"))]
            while True:
                line = input()
                if line == "":
                    break
                lines.append(line)
            for line in lines:
                parsed = raw_log_regex.match(line)
                if parsed is None:
                    print(line)
                try:
                    print(f"{parsed.group(1)}{decrypt_input(parsed.group(2), encryption_key)}")
                except ValueError:
                    print(line)
    else:
        while True:
            input_text = input("Enter cipher text: ")
            if input_text == "exit":
                exit(0)
            try:
                print(decrypt_input(input_text, encryption_key))
            except ValueError as e:
                print(f"failed to decrypt message: {e}")

if __name__ == "__main__":
    main()
