#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/bin}"
SRC="${QT_SRC_DIR:-$ROOT/qt-src}/qtbase"
BUILD="${QT_BUILD_DIR:-$BIN_DIR/qt-build}"
PREFIX="${QT_PREFIX:-$BIN_DIR/sysroot}"
sh "$ROOT/mk/qt/patch.sh"
if [ "${QT_GUI:-ON}" = "ON" ]; then
  QT_GUI_FLAGS="-DFEATURE_gui=ON -DFEATURE_widgets=ON -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF -DFEATURE_fontconfig=OFF -DFEATURE_xkbcommon=OFF -DFEATURE_vulkan=OFF -DFEATURE_printsupport=OFF -DFEATURE_sessionmanager=OFF -DFEATURE_accessibility=OFF -DFEATURE_linuxfb=OFF -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF -DFEATURE_tslib=OFF -DFEATURE_kms=OFF -DFEATURE_eglfs=OFF -DFEATURE_vnc=OFF -DFEATURE_xcb=OFF -DFEATURE_cups=OFF -DFEATURE_dbus=OFF -DQT_QPA_PLATFORMS=minimal;offscreen;purec"
else
  QT_GUI_FLAGS="-DFEATURE_gui=OFF -DFEATURE_widgets=OFF"
fi
rm -rf "$BUILD"
cmake -G Ninja -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/mk/sysroot/toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_HOST_PATH="${QT_HOST_PATH:-/usr}" \
  -DQT_HOST_PATH_CMAKE_DIR="${QT_HOST_PATH_CMAKE_DIR:-/usr/lib/cmake}" \
  -DBUILD_SHARED_LIBS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  $QT_GUI_FLAGS -DFEATURE_network=OFF -DFEATURE_sql=OFF -DFEATURE_dbus=OFF \
  -DFEATURE_testlib=OFF -DFEATURE_xml=OFF -DFEATURE_icu=OFF -DFEATURE_opengl=OFF -DINPUT_opengl=no \
  -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF -DFEATURE_system_doubleconversion=OFF \
  -DFEATURE_intelcet=OFF -DFEATURE_stack_protector=OFF -DFEATURE_trivial_auto_var_init_pattern=OFF \
  -DFEATURE_libcpp_hardening=OFF -DFEATURE_relro_now_linker=OFF -DFEATURE_reduce_relocations=OFF \
  -DFEATURE_process=OFF -DFEATURE_library=OFF -DFEATURE_storageinfo=OFF \
  "$@"
