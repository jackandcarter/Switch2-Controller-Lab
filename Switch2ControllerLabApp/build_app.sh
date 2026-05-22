#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

APP_DISPLAY_NAME="Switch2 Controller Lab"
APP_EXECUTABLE="Switch2ControllerLab"
APP_BUNDLE="$SCRIPT_DIR/dist/${APP_DISPLAY_NAME}.app"
APP_CONTENTS="$APP_BUNDLE/Contents"
APP_MACOS="$APP_CONTENTS/MacOS"
APP_FRAMEWORKS="$APP_CONTENTS/Frameworks"
APP_RESOURCES="$APP_CONTENTS/Resources"

HELPER_SOURCE="$REPO_ROOT/Switch2ControllerLab/switch2_mac_bridge.c"
HELPER_OUTPUT="$APP_MACOS/switch2_mac_bridge"
INFO_PLIST="$SCRIPT_DIR/Resources/Info.plist"
SWIFT_SOURCE="$SCRIPT_DIR/Sources/Switch2ControllerLabApp.swift"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to locate libusb." >&2
  exit 1
fi

LIBUSB_PREFIX="${LIBUSB_PREFIX:-$(brew --prefix libusb)}"
LIBUSB_INCLUDE="$LIBUSB_PREFIX/include/libusb-1.0"
LIBUSB_LIB_DIR="$LIBUSB_PREFIX/lib"
LIBUSB_DYLIB="$LIBUSB_LIB_DIR/libusb-1.0.0.dylib"

if [[ ! -f "$LIBUSB_DYLIB" ]]; then
  echo "libusb dylib not found at $LIBUSB_DYLIB" >&2
  echo "Install it with: brew install libusb" >&2
  exit 1
fi

HELPER_ARCH="${HELPER_ARCH:-}"
if [[ -z "$HELPER_ARCH" ]]; then
  if file "$LIBUSB_DYLIB" | grep -q "arm64"; then
    HELPER_ARCH="arm64"
  else
    HELPER_ARCH="x86_64"
  fi
fi

APP_ARCH="${APP_ARCH:-$(uname -m)}"

rm -rf "$APP_BUNDLE"
mkdir -p "$APP_MACOS" "$APP_FRAMEWORKS" "$APP_RESOURCES"

cp "$INFO_PLIST" "$APP_CONTENTS/Info.plist"

echo "Building SwiftUI app ($APP_ARCH)..."
xcrun --sdk macosx swiftc \
  -O \
  -parse-as-library \
  -target "${APP_ARCH}-apple-macos13.0" \
  "$SWIFT_SOURCE" \
  -framework SwiftUI \
  -framework AppKit \
  -framework Network \
  -framework ApplicationServices \
  -o "$APP_MACOS/$APP_EXECUTABLE"

echo "Building native helper ($HELPER_ARCH)..."
clang \
  -arch "$HELPER_ARCH" \
  -Wall -Wextra -Wpedantic -std=c11 \
  "$HELPER_SOURCE" \
  -I"$LIBUSB_INCLUDE" \
  -L"$LIBUSB_LIB_DIR" -lusb-1.0 \
  -framework IOKit \
  -framework CoreFoundation \
  -o "$HELPER_OUTPUT"

echo "Bundling libusb..."
cp "$LIBUSB_DYLIB" "$APP_FRAMEWORKS/libusb-1.0.0.dylib"

LIBUSB_INSTALL_NAME="$(otool -L "$HELPER_OUTPUT" | awk '/libusb-1.0.0.dylib/ {print $1; exit}')"
if [[ -n "$LIBUSB_INSTALL_NAME" ]]; then
  install_name_tool \
    -change "$LIBUSB_INSTALL_NAME" \
    "@executable_path/../Frameworks/libusb-1.0.0.dylib" \
    "$HELPER_OUTPUT"
fi

chmod +x "$APP_MACOS/$APP_EXECUTABLE" "$HELPER_OUTPUT"

if command -v codesign >/dev/null 2>&1; then
  echo "Ad-hoc signing app bundle..."
  codesign --force --deep --sign - "$APP_BUNDLE" >/dev/null
fi

echo "Built: $APP_BUNDLE"
