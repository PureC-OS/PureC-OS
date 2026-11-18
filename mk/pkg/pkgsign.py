#!/usr/bin/env python3
import argparse
import hashlib
import os
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


def fingerprint_of_point(point):
    return hashlib.sha512(point).digest()[:20].hex()


def read_point(public_hex):
    with open(public_hex) as f:
        text = f.read().strip()
    point = bytes.fromhex(text)
    if len(point) != 65 or point[0] != 4:
        sys.exit(f"{public_hex}: not an uncompressed P-256 point in hex")
    return point


def fingerprint(public_hex):
    return fingerprint_of_point(read_point(public_hex))


def split_keyring(text):
    lines = text.splitlines()
    payload = []
    sigs = []
    for i, line in enumerate(lines):
        if line.startswith("#sig "):
            sigs = [l for l in lines[i:] if l.startswith("#sig ")]
            break
        payload.append(line)
    body = "\n".join(payload)
    if body and not body.endswith("\n"):
        body += "\n"
    return body, sigs


def keyring_sign(private_pem, path):
    with open(private_pem, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), None)
    fp = fingerprint_of_point(key.public_key().public_bytes(serialization.Encoding.X962,
                                                            serialization.PublicFormat.UncompressedPoint))
    with open(path, encoding="utf-8") as f:
        body, sigs = split_keyring(f.read())
    digest = hashlib.sha512(body.encode()).digest()
    der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA512())))
    sigs = [l for l in sigs if l.split()[1:2] != [fp]]
    sigs.append(f"#sig {fp} {der.hex()}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(body + "\n".join(sigs) + "\n")
    return fp


def keyring_verify(masters_dir, path, threshold):
    masters = {}
    for name in sorted(os.listdir(masters_dir)):
        if name.endswith(".pub"):
            point = read_point(os.path.join(masters_dir, name))
            masters[fingerprint_of_point(point)] = point
    with open(path, encoding="utf-8") as f:
        body, sigs = split_keyring(f.read())
    digest = hashlib.sha512(body.encode()).digest()
    good = set()
    for line in sigs:
        parts = line.split()
        if len(parts) != 3 or parts[1] not in masters:
            continue
        pub = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), masters[parts[1]])
        try:
            pub.verify(bytes.fromhex(parts[2]), digest, ec.ECDSA(utils.Prehashed(hashes.SHA512())))
            good.add(parts[1])
        except Exception:
            pass
    return sorted(good), len(good) >= threshold


def keyring_entry(public_hex, scope):
    point = read_point(public_hex)
    fp = fingerprint_of_point(point)
    if scope == ["official"]:
        return f"key {fp} {point.hex()} official"
    attrs = [a for a in scope if a.startswith("repo=") or a.startswith("names=")]
    if len(attrs) != 2 or len(attrs) != len(scope):
        sys.exit("scope: official | repo=URL names=GLOB[,GLOB...]")
    return f"key {fp} {point.hex()} " + " ".join(attrs)


def main_keyring(argv):
    cmd = argv[0]
    if cmd == "fingerprint" and len(argv) == 2:
        print(fingerprint(argv[1]))
    elif cmd == "keyring-entry" and len(argv) >= 3:
        print(keyring_entry(argv[1], argv[2:]))
    elif cmd == "keyring-sign":
        ap = argparse.ArgumentParser(prog="pkgsign.py keyring-sign")
        ap.add_argument("--key", required=True)
        ap.add_argument("keyring")
        args = ap.parse_args(argv[1:])
        print("signed by " + keyring_sign(args.key, args.keyring))
    elif cmd == "keyring-verify":
        ap = argparse.ArgumentParser(prog="pkgsign.py keyring-verify")
        ap.add_argument("--masters", required=True)
        ap.add_argument("--threshold", type=int, default=1)
        ap.add_argument("keyring")
        args = ap.parse_args(argv[1:])
        good, ok = keyring_verify(args.masters, args.keyring, args.threshold)
        print(f"{len(good)} valid master signature(s), {args.threshold} required: " + ("ok" if ok else "FAILED"))
        for fp in good:
            print("  " + fp)
        sys.exit(0 if ok else 1)
    else:
        sys.exit("usage: pkgsign.py fingerprint PUB | keyring-entry PUB official|repo=URL names=GLOBS | "
                 "keyring-sign --key MASTER.pem KEYRING | keyring-verify --masters DIR [--threshold N] KEYRING")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("fingerprint", "keyring-entry", "keyring-sign", "keyring-verify"):
        main_keyring(sys.argv[1:])
    elif len(sys.argv) == 4 and sys.argv[1] == "genkey":
        generate(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "pubkey":
        print(public_hex_of(sys.argv[2]))
    elif len(sys.argv) == 5 and sys.argv[1] == "sign":
        sign_file(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit("usage: pkgsign.py genkey PRIVATE.pem PUBLIC.hex | pubkey PRIVATE.pem | sign PRIVATE.pem FILE SIGFILE | fingerprint | keyring-entry | keyring-sign | keyring-verify")
