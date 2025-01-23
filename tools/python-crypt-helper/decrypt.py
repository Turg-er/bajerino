# /// script
# dependencies = [
#   "cryptography==44.0.0",
#   "requests==2.32.3",
# ]
# ///

from cryptography.hazmat.primitives.ciphers.modes import CBC
from cryptography.hazmat.primitives.ciphers.algorithms import AES
from cryptography.hazmat.primitives.ciphers import Cipher
from cryptography.hazmat.primitives import padding
from hashlib import blake2b
from pathlib import Path
from requests import get

import argparse
import re


def calculate_encryption_key(encryption_password: str) -> bytes:
    return blake2b(encryption_password.encode("utf-8"), digest_size=32).digest()


class CharOutOfRangeError(ValueError):
    def __init__(self, char: str, idx: int):
        super().__init__(
            f"Input's char at {idx}({char[0]}) is not in correct character. Expected >= {ord('一')} and <= {ord('一') + 255} got {ord(char)}."
        )


def decrypt_input(input_text: str, encryption_key: bytes) -> str:
    """Decrypt the input cipher text using AES with CBC mode."""
    # first 16 bytes are iv, rest are ciphertext
    for idx, char in enumerate(input_text):
        cord = ord(char)
        if cord < ord("一") or cord > ord("一") + 255:
            raise CharOutOfRangeError(char, idx)
    cipher_bytes = bytes([ord(char) - ord("一") for char in input_text])

    cipher = Cipher(AES(encryption_key[0:16]), CBC(cipher_bytes[0:16]))

    decryptor = cipher.decryptor()
    decrypted = decryptor.update(cipher_bytes[16:]) + decryptor.finalize()

    unpadder = padding.PKCS7(128).unpadder()
    unpadded = unpadder.update(decrypted) + unpadder.finalize()

    return unpadded.decode("utf-8")


def singleline(args):
    encryption_key = calculate_encryption_key(args.encryption_password)
    while True:
        input_text = input("Enter cipher text: ")
        if input_text == "exit":
            exit(0)
        try:
            args.output(decrypt_input(input_text, encryption_key))
        except ValueError as e:
            print(f"failed to decrypt message: {e}")


justlog_regex = re.compile(
    r"^(\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] #\w+ \w+: (?:@\w+ )?)(.+)$"
)


def multiline(args):
    encryption_key = calculate_encryption_key(args.encryption_password)

    def multiline_parse(line: str, encryption_key: str):
        line = line.strip()
        parsed = justlog_regex.match(line)
        if parsed is None:
            return line
        return f"{parsed.group(1)}{decrypt_input(parsed.group(2), encryption_key)}"

    if args.file is not None:
        with open(args.file, "r", encoding="utf-8") as file:
            for line in file:
                try:
                    args.output(multiline_parse(line, encryption_key))
                except CharOutOfRangeError:
                    args.output(line)
                except ValueError as e:
                    print(f"failed to decrypt message: {e}, still outputting")
                    args.output(line)
    elif args.url is not None:
        r = get(args.url, stream=True)
        if not r.ok:
            print(f"ERROR: failed to fetch logs, status code returned: {r.status_code}")
            exit(1)
        elif r.headers["content-type"] != "text/plain; charset=utf-8":
            print(
                f"ERROR: fetched file isn't utf-8 plaintext, content-type: {r.headers['content-type']}"
            )
            exit(2)
        for line in r.iter_lines():
            if line:
                decoded = line.decode("utf-8")
                try:
                    args.output(multiline_parse(decoded, encryption_key))
                except CharOutOfRangeError:
                    args.output(decoded)
                except ValueError as e:
                    print(f"failed to decrypt message: {e}, still outputting")
                    args.output(decoded)
    else:
        lines = [(input("Paste your multiline cipher text here:\n"))]
        while True:
            line = input()
            if line == "":
                break
            lines.append(line)
        for line in lines:
            try:
                args.output(multiline_parse(line, encryption_key))
            except CharOutOfRangeError:
                args.output(line)
            except ValueError as e:
                print(f"failed to decrypt message: {e}, still outputting")
                args.output(line)


def append_to_file(path: Path):
    def output(input: str):
        with path.open("a", encoding="utf-8") as f:
            f.write(f"{input.strip()}\n")

    return output


def main() -> None:
    """Main function to handle command-line arguments and decryption."""
    parser = argparse.ArgumentParser(
        description="Decrypt a cipher text with AES and CBC mode."
    )

    parser.add_argument(
        "--encryption_password",
        required=True,
        type=str,
        help="The password used to generate the encryption key",
    )

    parser.add_argument(
        "--output",
        "-o",
        default=None,
        type=str,
        help="The password used to generate the encryption key",
    )

    subparsers = parser.add_subparsers(required=True)

    single_parser = subparsers.add_parser(
        "singleline", help="Will be prompted to enter cipher text one line at a time"
    )
    single_parser.set_defaults(func=singleline)

    multi_parser = subparsers.add_parser(
        "multiline", help="Input cipher text in Rustlog/Justlog format"
    )
    multi_input_group = multi_parser.add_mutually_exclusive_group()
    multi_input_group.add_argument(
        "--paste",
        default=True,
        action="store_true",
        help="Will be prompted to paste in multiple lines of text, empty line, stops input (default multiline option)",
    )
    multi_input_group.add_argument(
        "--file",
        default=None,
        type=str,
        help="Input file that contains the cipher text",
    )
    multi_input_group.add_argument(
        "--url",
        default=None,
        type=str,
        help="URL pointing to remote plaintext thats contains the cipher text",
    )
    multi_parser.set_defaults(func=multiline)

    args = parser.parse_args()

    if args.output is None:
        args.output = print
    else:
        path = Path(args.output)
        if path.is_dir():
            print("output is set to dir, invalid")
            exit(1)
        elif path.exists():
            chose = input("""The file you output to already exists. Would you like to overwrite:
    [Y/y]es: Overwrite file
    [N/n]o: Don't overwrite and exit
    [A/a]ppend: Append to file\n""").lower()
            while chose not in ["y", "n", "a"]:
                chose = input("""Invalid input. Options are:
    [Y/y]es: Overwrite file
    [N/n]o: Don't overwrite and exit
    [A/a]ppend: Append to file\n""").lower()
            if chose == "n":
                exit(0)
            elif chose == "y":
                with path.open("w", encoding="utf-8") as f:
                    f.write("")
            args.output = append_to_file(path)
        else:
            args.output = append_to_file(path)

    args.func(args)


if __name__ == "__main__":
    main()
