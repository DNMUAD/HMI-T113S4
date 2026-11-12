#!/usr/bin/env bash
# 把 etc/init.d/S41wifi 装进 buildroot 的 rootfs overlay，
# 这样重编镜像后板子开机就会自动装载 WiFi 驱动、拉起 wlan0 和 wifi_daemon。
#
# 用法：
#   SDK_ROOT=/path/to/tina-sdk bash notes/hmi-t113s4/install-wifi-init.sh
#
# 也可显式指定 overlay 目录（当 SDK 布局不同时）：
#   bash notes/hmi-t113s4/install-wifi-init.sh /path/to/buildroot/config/buildroot/overlay
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$REPO/etc/init.d/S41wifi"

if [[ $# -ge 1 ]]; then
    OVERLAY="$1"
elif [[ -n "${SDK_ROOT:-}" ]]; then
    OVERLAY="$SDK_ROOT/buildroot/config/buildroot/overlay"
else
    echo "请设置 SDK_ROOT，或显式传入 overlay 目录" >&2
    echo "  SDK_ROOT=/path/to/sdk bash notes/hmi-t113s4/install-wifi-init.sh" >&2
    exit 1
fi

[[ -f "$SRC" ]] || { echo "缺少源文件：$SRC" >&2; exit 1; }
[[ -d "$OVERLAY" ]] || { echo "overlay 目录不存在：$OVERLAY" >&2; exit 1; }

DST="$OVERLAY/etc/init.d"
mkdir -p "$DST"
cp "$SRC" "$DST/S41wifi"
chmod 755 "$DST/S41wifi"

echo "已安装：$DST/S41wifi"
echo
echo "下一步（在 SDK 根目录执行）："
echo "  bash build.sh buildroot_rootfs"
echo "  bash build.sh pack"
echo
echo "刷机后验证："
echo "  ls -l /etc/init.d/S41wifi"
echo "  ls /sys/class/net/wlan0"
echo "  pidof wifi_daemon && ifconfig wlan0"
