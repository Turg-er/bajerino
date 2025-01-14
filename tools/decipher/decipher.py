from cryptography.hazmat.primitives.ciphers.modes import CBC
from cryptography.hazmat.primitives.ciphers.algorithms import AES
from cryptography.hazmat.primitives.ciphers import Cipher
from hashlib import blake2b

encryption_key = blake2b(input("Encryption Password: ").encode('utf-8'), digest_size=32).digest()

while True:
    # Prompt the user for input
    input_text = input("Enter cipher text: ")

    # first 16 bytes are iv, rest are ciphertext
    cipher_bytes = bytearray([(ord(char) - ord("一")) % 256 for char in input_text])

    cipher = Cipher(AES(encryption_key[0:16]), CBC(cipher_bytes[0:16]))

    decryptor = cipher.decryptor()
    decrypted = decryptor.update(cipher_bytes[16:]) + decryptor.finalize()

    print(decrypted.decode("utf-8"))
