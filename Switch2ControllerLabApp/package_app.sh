#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_BUNDLE="$SCRIPT_DIR/dist/Switch2 Controller Lab.app"
PKG_OUTPUT="$SCRIPT_DIR/dist/Switch2ControllerLab-0.1.0.pkg"

if [[ ! -d "$APP_BUNDLE" ]]; then
  "$SCRIPT_DIR/build_app.sh"
fi

pkgbuild \
  --component "$APP_BUNDLE" \
  --install-location "/Applications" \
  --identifier "app.switch2controllerlab.pkg" \
  --version "0.1.0" \
  "$PKG_OUTPUT"

echo "Built: $PKG_OUTPUT"
