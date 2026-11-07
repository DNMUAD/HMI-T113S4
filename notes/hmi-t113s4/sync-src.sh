#!/usr/bin/env bash
# 把本仓库源码同步到 SDK 的 buildroot local SITE 目录。
# 用法：
#   SDK_ROOT=/path/to/tina-sdk bash notes/hmi-t113s4/sync-src.sh
#   bash notes/hmi-t113s4/sync-src.sh <源目录> <目标目录>
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${1:-$REPO}"
DST="${2:-${SDK_ROOT:-}/buildroot/package/allwinner/auto/qt-demo/HMI-T113S4}"

if [[ -z "${SDK_ROOT:-}" && $# -lt 2 ]]; then
    echo "请设置 SDK_ROOT 或显式传入源目录与目标目录" >&2
    echo "  SDK_ROOT=/path/to/sdk bash notes/hmi-t113s4/sync-src.sh" >&2
    exit 1
fi
if [[ "$DST" == "/buildroot/"* ]]; then
    echo "SDK_ROOT 未设置，拒绝写入 $DST" >&2
    exit 1
fi
command -v rsync >/dev/null || { echo "需要 rsync" >&2; exit 1; }

mkdir -p "$DST"
rsync -a --delete \
    --exclude='.git/' \
    --exclude='build*/' \
    --exclude='*.user' \
    --exclude='*.pro.user*' \
    --exclude='Makefile*' \
    --exclude='.qmake.stash' \
    --exclude='moc_*' \
    --exclude='qrc_*' \
    --exclude='*.o' \
    --exclude='*.qm' \
    --exclude='/HMI-T113S4' \
    --exclude='/HMI-T113S4.exe' \
    "$SRC/" "$DST/"

echo "源码已同步: $SRC  ->  $DST"
