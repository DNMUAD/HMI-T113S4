#!/usr/bin/env bash
# Qt Creator 自定义 Deploy 步骤可直接调用本脚本：
#   bash deploy/adb-deploy.sh build/HMI-T113S4
# 与约定等价的单行命令：
#   adb push build/HMI-T113S4 /tmp/ && adb shell "cp /tmp/HMI-T113S4 /usr/bin/QtApplication && sync && /etc/init.d/S70launcher stop; /etc/init.d/S70launcher start"
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$REPO/build/HMI-T113S4}"

[[ -f "$BIN" ]] || { echo "找不到二进制: $BIN" >&2; exit 1; }
command -v adb >/dev/null || { echo "adb 不在 PATH" >&2; exit 1; }

adb wait-for-device
adb push "$BIN" /tmp/HMI-T113S4
adb shell "cp /tmp/HMI-T113S4 /usr/bin/QtApplication && chmod 755 /usr/bin/QtApplication && sync && /etc/init.d/S70launcher stop; /etc/init.d/S70launcher start"
echo "部署完成：/usr/bin/QtApplication 已更新并重启 launcher"
