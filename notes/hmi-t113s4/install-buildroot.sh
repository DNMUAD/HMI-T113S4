#!/usr/bin/env bash
# 把 buildroot 包三件套放进 SDK，并幂等地把 source 行追加到 auto/Config.in。
# 用法：SDK_ROOT=/path/to/tina-sdk bash notes/hmi-t113s4/install-buildroot.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK="${SDK_ROOT:-}"
[[ -n "$SDK" ]] || { echo "请设置 SDK_ROOT" >&2; exit 1; }

AUTO_DIR="$SDK/buildroot/config/buildroot/allwinner/auto"
PKG_DIR="$AUTO_DIR/qt-demo/hmi-t113s4"
mkdir -p "$PKG_DIR"

cp "$REPO/buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/Config.in" "$PKG_DIR/"
cp "$REPO/buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/hmi-t113s4.mk" "$PKG_DIR/"

SOURCE_LINE='source "package/allwinner/auto/qt-demo/hmi-t113s4/Config.in"'
if ! grep -Fq "$SOURCE_LINE" "$AUTO_DIR/Config.in"; then
    echo "" >> "$AUTO_DIR/Config.in"
    echo "$SOURCE_LINE" >> "$AUTO_DIR/Config.in"
    echo "已追加 source 到 $AUTO_DIR/Config.in"
else
    echo "source 行已存在，跳过追加"
fi

bash "$REPO/notes/hmi-t113s4/sync-src.sh" "$REPO" \
    "$SDK/buildroot/package/allwinner/auto/qt-demo/HMI-T113S4"

echo "完成。接下来：bash ./build.sh package hmi-t113s4"
