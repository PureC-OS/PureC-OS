#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QT="${QT_SRC_DIR:-$ROOT/qt-src}/qtbase"
[ -d "$QT/cmake" ] || { echo "qtbase sources missing: run mk/qt/fetch.sh" >&2; exit 1; }

mkdir -p "$QT/mkspecs/purec-clang"
cp "$ROOT/mk/qt/mkspecs/purec-clang/"* "$QT/mkspecs/purec-clang/"

python3 - "$QT" <<'PY'
import sys
qt = sys.argv[1]

def patch(path, marker, anchor, addition, after=True):
    p = qt + "/" + path
    s = open(p).read()
    if marker in s:
        return
    assert anchor in s, (path, anchor)
    s = s.replace(anchor, anchor + addition if after else addition + anchor, 1)
    open(p, "w").write(s)

patch("cmake/QtPlatformSupport.cmake", "PureC OS", """if(CMAKE_SYSTEM_NAME STREQUAL "GNU")
    set(HURD 1)
else()
    set(HURD 0)
endif()
""", """
# PureC OS
if(CMAKE_SYSTEM_NAME STREQUAL "PureC")
    set(PUREC 1)
else()
    set(PUREC 0)
endif()
""")

patch("cmake/QtMkspecHelpers.cmake", "purec-clang", """    elseif(HURD)
        set(QT_DEFAULT_MKSPEC hurd-g++)
""", """    elseif(PUREC)
        set(QT_DEFAULT_MKSPEC purec-clang)
""")

patch("src/corelib/global/qsystemdetection.h", "Q_OS_PUREC", """#elif defined(__HAIKU__)
#  define Q_OS_HAIKU
""", """#elif defined(__purec__)
#  define Q_OS_PUREC
""")
PY
echo "qtbase patched for PureC"
