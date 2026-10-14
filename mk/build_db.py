#!/usr/bin/env python3
import argparse
import os
import sqlite3
import subprocess
import sys

SCHEMA = (
    "CREATE TABLE IF NOT EXISTS builds ("
    "uuid TEXT PRIMARY KEY, "
    "built_at TEXT NOT NULL, "
    "kernel_map TEXT NOT NULL, "
    "git_rev TEXT)"
)


def connect(db):
    os.makedirs(os.path.dirname(os.path.abspath(db)), exist_ok=True)
    handle = sqlite3.connect(db)
    handle.execute(SCHEMA)
    return handle


def git_rev():
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True).stdout.strip()
        return out if out else None
    except Exception:
        return None


def cmd_record(args):
    handle = connect(args.db)
    handle.execute(
        "INSERT OR REPLACE INTO builds (uuid, built_at, kernel_map, git_rev)"
        " VALUES (?, ?, ?, ?)",
        (args.uuid, args.time, args.map, git_rev()))
    handle.commit()
    handle.close()
    print("build_db: %s %s -> %s" % (args.uuid, args.time, args.db))


def cmd_lookup(args):
    handle = connect(args.db)
    row = handle.execute(
        "SELECT uuid, built_at, kernel_map, git_rev FROM builds WHERE uuid = ?",
        (args.uuid,)).fetchone()
    handle.close()
    if not row:
        print("build_db: unknown uuid %s" % args.uuid)
        return 1
    print("uuid:       %s" % row[0])
    print("built_at:   %s" % row[1])
    print("kernel_map: %s" % row[2])
    print("git_rev:    %s" % (row[3] if row[3] else "-"))
    return 0


def cmd_list(args):
    handle = connect(args.db)
    rows = handle.execute(
        "SELECT uuid, built_at, kernel_map, git_rev FROM builds"
        " ORDER BY built_at DESC LIMIT ?", (args.limit,)).fetchall()
    handle.close()
    for row in rows:
        print("%s  %s  %s  %s" % (row[1], row[0], row[2],
                                  row[3] if row[3] else "-"))
    return 0


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    record = sub.add_parser("record")
    record.add_argument("--db", required=True)
    record.add_argument("--uuid", required=True)
    record.add_argument("--time", required=True)
    record.add_argument("--map", required=True)
    record.set_defaults(fn=cmd_record)
    lookup = sub.add_parser("lookup")
    lookup.add_argument("--db", required=True)
    lookup.add_argument("uuid")
    lookup.set_defaults(fn=cmd_lookup)
    listing = sub.add_parser("list")
    listing.add_argument("--db", required=True)
    listing.add_argument("--limit", type=int, default=20)
    listing.set_defaults(fn=cmd_list)
    args = parser.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
