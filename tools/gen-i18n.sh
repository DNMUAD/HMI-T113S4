#!/usr/bin/env bash
# 桌面 Kit / 本地生成 resources/i18n/*.qm（buildroot 由 .mk 自动做，无需手动跑）
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
LRELEASE="${LRELEASE:-lrelease}"
command -v "$LRELEASE" >/dev/null || { echo "找不到 lrelease，请设置 LRELEASE=/path/to/lrelease" >&2; exit 1; }
shopt -s nullglob
for ts in resources/i18n/*.ts; do
    "$LRELEASE" "$ts" -qm "${ts%.ts}.qm"
done
ls -l resources/i18n/*.qm
