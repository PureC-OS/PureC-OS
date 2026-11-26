#!/usr/bin/env python3
import argparse
import os
import sys


def parse(path):
    entries = []
    with open(path) as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) != 4:
                sys.exit("%s:%d: expected <nr> <abi> <name> <entry>" % (path, number))
            nr, abi, name, entry = fields
            if int(nr) != len(entries):
                sys.exit("%s:%d: numbers must be sequential from 0 (got %s, want %d)"
                         % (path, number, nr, len(entries)))
            if abi != "common":
                sys.exit("%s:%d: unknown abi %s" % (path, number, abi))
            if name != name.lower() or any(e.name == name for e in entries):
                sys.exit("%s:%d: bad or duplicate name %s" % (path, number, name))
            entries.append(type("E", (), {"nr": int(nr), "name": name, "entry": entry})())
    return entries


def write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def nr_header(entries):
    out = ["#pragma once", ""]
    for e in entries:
        out.append("#define SYS_%s %d" % (e.name.upper(), e.nr))
    out.append("")
    out.append("#define SYS_NR %d" % len(entries))
    out.append("#define SYS_MAX %d" % (len(entries) - 1))
    return "\n".join(out) + "\n"


def proto_header(entries):
    out = ["#pragma once", "", "#include <stdint.h>", "",
           "typedef long (*sys_call_ptr_t)(uint64_t, uint64_t, uint64_t, uint64_t,",
           "                               uint64_t, uint64_t);", "",
           "extern const sys_call_ptr_t sys_call_table[];", ""]
    for e in entries:
        if e.entry != "sys_ni_syscall":
            out.append("long %s(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);" % e.entry)
    return "\n".join(out) + "\n"


def table_source(entries):
    out = ['#include <kernel/syscall/syscall_nr.h>', '#include <kernel/syscall/syscall_table.h>', "",
           "const sys_call_ptr_t sys_call_table[SYS_NR] = {"]
    for e in entries:
        if e.entry != "sys_ni_syscall":
            out.append("    [SYS_%s] = %s," % (e.name.upper(), e.entry))
    out.append("};")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tbl")
    ap.add_argument("--nr", action="append", default=[])
    ap.add_argument("--proto")
    ap.add_argument("--table")
    a = ap.parse_args()
    entries = parse(a.tbl)
    for p in a.nr:
        write_if_changed(p, nr_header(entries))
    if a.proto:
        write_if_changed(a.proto, proto_header(entries))
    if a.table:
        write_if_changed(a.table, table_source(entries))


main()
