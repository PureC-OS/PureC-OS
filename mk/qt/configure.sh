#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/bin}"
SRC="${QT_SRC_DIR:-$ROOT/qt-src}/qtbase"
BUILD="${QT_BUILD_DIR:-$BIN_DIR/qt-build}"
PREFIX="${QT_PREFIX:-$BIN_DIR/sysroot}"
sh "$ROOT/mk/qt/patch.sh"
rm -rf "$BUILD"
cmake -G Ninja -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/mk/sysroot/toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_HOST_PATH="${QT_HOST_PATH:-/usr}" \
  -DQT_HOST_PATH_CMAKE_DIR="${QT_HOST_PATH_CMAKE_DIR:-/usr/lib/cmake}" \
  -DBUILD_SHARED_LIBS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  -DFEATURE_gui=OFF -DFEATURE_widgets=OFF -DFEATURE_network=OFF -DFEATURE_sql=OFF -DFEATURE_dbus=OFF \
  -DFEATURE_testlib=OFF -DFEATURE_xml=OFF -DFEATURE_icu=OFF -DFEATURE_opengl=OFF \
  -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_doubleconversion=OFF \
  "$@"
