#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import sys
import tempfile
import os


def need_tools():
    missing = []
    try:
        import PIL  # noqa: F401
    except ImportError:
        missing.append("pillow")
    if shutil.which("zbarimg") is None:
        missing.append("zbarimg")
    return missing


def roundtrip(tool, payload):
    from PIL import Image
    with tempfile.TemporaryDirectory() as tmp:
        pbm = os.path.join(tmp, "t.pbm")
        png = os.path.join(tmp, "t.png")
        proc = subprocess.run([tool], input=payload, capture_output=True)
        if proc.returncode != 0:
            return "dump tool failed rc=%d" % proc.returncode
        with open(pbm, "wb") as handle:
            handle.write(proc.stdout)
        Image.open(pbm).convert("1").save(png)
        proc = subprocess.run(["zbarimg", "--raw", png], capture_output=True)
        if proc.returncode != 0:
            return "zbarimg failed rc=%d" % proc.returncode
        out = proc.stdout
        if out.endswith(b"\n"):
            out = out[:-1]
        try:
            back = out.decode("utf-8").encode("latin-1")
        except (UnicodeDecodeError, UnicodeEncodeError):
            back = out
        if back != payload:
            return "mismatch (%d != %d bytes)" % (len(back), len(payload))
    return ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    missing = need_tools()
    if missing:
        print("qr_decode_test: SKIP (%s not available)" % ", ".join(missing))
        return 1 if args.strict else 0
    vectors = [
        b"HELLO-QR-TEST-123",
        b"https://github.com/PureC-OS/PureC-OS/issues/new?title=Kernel%%20panic%%20test&body=" + b"Build%%20x%%0ARIP%%0A" * 40,
        bytes(range(1, 256)),
    ]
    failed = 0
    for index, payload in enumerate(vectors):
        err = roundtrip(args.tool, payload)
        if err:
            print("qr_decode_test: vector %d FAIL (%s)" % (index, err))
            failed += 1
        else:
            print("qr_decode_test: vector %d ok (%d bytes)" % (index, len(payload)))
    if failed:
        print("qr_decode_test: FAIL")
        return 1
    print("qr_decode_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
