#!/usr/bin/env python3
import hashlib
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils


def generate(private_pem, public_hex):
    key = ec.generate_private_key(ec.SECP256R1())
    with open(private_pem, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    point = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    with open(public_hex, "w") as f:
        f.write(point.hex() + "\n")


def public_hex_of(private_pem):
    with open(private_pem, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), None)
    point = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    return point.hex()


def sign_file(private_pem, path, out):
    with open(private_pem, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), None)
    with open(path, "rb") as f:
        digest = hashlib.sha512(f.read()).digest()
    signature = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA512())))
    with open(out, "wb") as f:
        f.write(signature)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "genkey":
        generate(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "pubkey":
        print(public_hex_of(sys.argv[2]))
    elif len(sys.argv) == 5 and sys.argv[1] == "sign":
        sign_file(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit("usage: pkgsign.py genkey PRIVATE.pem PUBLIC.hex | pubkey PRIVATE.pem | sign PRIVATE.pem FILE SIGFILE")
