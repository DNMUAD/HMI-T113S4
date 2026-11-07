#!/usr/bin/env bash
# 本地生成 resources/i18n/*.qm（.pro 里已用 lrelease 自动嵌入，此脚本用于单独产出 .qm）
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
LRELEASE="${LRELEASE:-lrelease}"
command -v "$LRELEASE" >/dev/null || { echo "找不到 lrelease，请设置 LRELEASE=/path/to/lrelease" >&2; exit 1; }
shopt -s nullglob
for ts in resources/i18n/*.ts; do
    "$LRELEASE" "$ts" -qm "${ts%.ts}.qm"
done
ls -l resources/i18n/*.qm
