#!/usr/bin/env python3
"""Wrap the declarations of every hosted libc header in extern "C" for C++ (idempotent).

The opening block goes in front of the first declaration, after the include preamble, so
that headers pulled in by the preamble (libc++ wrappers such as <math.h>) stay outside
the C linkage block. Cases that cannot be handled safely are reported, not edited.
"""
import re
import sys
from pathlib import Path

OPEN = '#ifdef __cplusplus\nextern "C" {\n#endif\n'
CLOSE = '#ifdef __cplusplus\n}\n#endif\n'
WRAPPED_BY_LIBCXX = {"math.h", "stdlib.h", "string.h", "stdio.h", "wchar.h", "ctype.h", "locale.h",
                     "time.h", "complex.h", "fenv.h", "wctype.h", "uchar.h", "setjmp.h", "signal.h"}

def process(path: Path):
    text = path.read_text()
    if "__cplusplus" in text:
        return "skip: already C++-aware"
    lines = text.split("\n")
    depth = 0
    in_comment = False
    first_decl = None
    depth_open = 0
    for index, line in enumerate(lines):
        stripped = line.strip()
        if in_comment:
            if "*/" in stripped:
                in_comment = False
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_comment = True
            continue
        if not stripped or stripped.startswith("//"):
            continue
        if stripped.startswith("#"):
            directive = re.match(r"#\s*(\w+)", stripped).group(1)
            if directive in ("if", "ifdef", "ifndef"):
                depth += 1
            elif directive == "endif":
                depth -= 1
            continue
        first_decl = index
        depth_open = depth
        break
    if first_decl is None:
        return "skip: no declarations"
    close_at = None
    depth = depth_open
    problems = []
    for index in range(first_decl, len(lines)):
        stripped = lines[index].strip()
        if not stripped.startswith("#"):
            continue
        directive = re.match(r"#\s*(\w+)", stripped).group(1)
        if directive in ("if", "ifdef", "ifndef"):
            depth += 1
        elif directive in ("else", "elif", "elifdef", "elifndef") and depth == depth_open and depth_open > 0:
            problems.append(f"#{directive} at guard level, line {index + 1}")
        elif directive == "endif":
            depth -= 1
            if depth < depth_open:
                close_at = index
                break
        elif directive == "include":
            match = re.match(r'#\s*include\s*[<"]([^>"]+)[>"]', stripped)
            if match and match.group(1) in WRAPPED_BY_LIBCXX:
                problems.append(f"include of {match.group(1)} after first declaration, line {index + 1}")
    if close_at is None:
        close_at = len(lines)
        while close_at > 0 and not lines[close_at - 1].strip():
            close_at -= 1
    if problems:
        return "MANUAL: " + "; ".join(problems)
    lines[close_at:close_at] = CLOSE.rstrip("\n").split("\n")
    lines[first_decl:first_decl] = OPEN.rstrip("\n").split("\n")
    path.write_text("\n".join(lines) if text.endswith("\n") is False else "\n".join(lines))
    return "wrapped"

def main():
    root = Path(sys.argv[1])
    for header in sorted(root.rglob("*.h")):
        print(f"{header.relative_to(root)}: {process(header)}")

main()
