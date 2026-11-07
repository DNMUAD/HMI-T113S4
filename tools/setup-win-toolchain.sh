#!/usr/bin/env bash
# 一次性安装本工程专用的 Qt 5.15.2 + MinGW 8.1.0 命令行工具链。
#
# 隔离原则：所有文件只落在工作区 .toolchain/ 下，不写 C:/Qt、不改系统 PATH、
#          不碰注册表、不用需要登录的官方在线安装器。
#          卸载： rm -rf .toolchain  （外加两个目录联接，见文末输出）
#
# 用法：  bash tools/setup-win-toolchain.sh [--force]
#         缺省幂等：已装好的部分会跳过；--force 强制重下。

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE="$(cd "$HERE/.." && pwd)"          # 工作区根：HMI-T113S4 的上一层
TOOLCHAIN="$WORKSPACE/.toolchain"
QT_VER="5.15.2"
QT_ARCH="win64_mingw81"
MINGW_TOOL="qt.tools.win64_mingw810"
MIRROR="${HMI_QT_MIRROR:-https://mirrors.aliyun.com/qt/}"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

# py7zr 必须钉 1.1.0：1.1.1+ 新增符号链接校验，在 Windows 上会随机抛
# Bad7zFile: Specified path is bad（aqtinstall #995）
AQT_PKGS=("aqtinstall==3.3.0" "py7zr==1.1.0")

echo "工作区 : $WORKSPACE"
echo "工具链 : $TOOLCHAIN"
echo "镜像   : $MIRROR"

# ---------------------------------------------------------------- 1. aqtinstall
VENV="$TOOLCHAIN/aqt-venv"
PY="$VENV/Scripts/python.exe"
if [ ! -x "$PY" ] || [ "$FORCE" = "1" ]; then
    echo "==> 创建独立 venv（不污染全局 Python）"
    mkdir -p "$TOOLCHAIN"
    python -m venv "$VENV"
    "$PY" -m pip install --quiet -U pip
    "$PY" -m pip install --quiet "${AQT_PKGS[@]}"
else
    echo "==> venv 已存在，跳过"
fi

AQT=("$PY" -m aqt)
"${AQT[@]}" version | head -1

# --------------------------------------------------------------------- 2. Qt
if [ -x "$TOOLCHAIN/$QT_VER/mingw81_64/bin/qmake.exe" ] && [ "$FORCE" = "0" ]; then
    echo "==> Qt $QT_VER 已存在，跳过"
else
    echo "==> 安装 Qt $QT_VER ($QT_ARCH)"
    # 只下编译本工程必需的归档：
    #   qtbase     qmake / mkspecs / widgets / network / platforms 插件
    #   qttools    lrelease（工程开了 embed_translations）+ windeployqt
    #   qtmultimedia / qtserialport  QT += 里用到
    #   x86_64     把 libstdc++/libgcc/libwinpthread 放进 Qt 的 bin，
    #              否则 qmake.exe 自己都起不来（会被系统里 32 位同名 DLL 顶掉）
    "${AQT[@]}" install-qt windows desktop "$QT_VER" "$QT_ARCH" \
        --outputdir "$TOOLCHAIN" --base "$MIRROR" \
        --archives qtbase qttools qtmultimedia qtserialport x86_64
fi

# ------------------------------------------------------------------- 3. MinGW
if [ -x "$TOOLCHAIN/Tools/mingw810_64/bin/g++.exe" ] && [ "$FORCE" = "0" ]; then
    echo "==> MinGW 8.1.0 已存在，跳过"
else
    echo "==> 安装 MinGW 8.1.0"
    "${AQT[@]}" install-tool windows desktop tools_mingw "$MINGW_TOOL" \
        --outputdir "$TOOLCHAIN" --base "$MIRROR"
fi

# ------------------------------------------- 4. 建 ASCII 目录联接（关键一步）
# 工作区路径含中文，而 qmake 在非 ASCII 前缀下会把 qt.conf 的 Prefix=.. 解析成 "/.."，
# 导致 QT_INSTALL_HEADERS/LIBS 全错。联接让工具链与源码以 ASCII 路径出现，
# 文件本身仍留在工作区，不复制、不占额外空间。
make_junction()
{
    local link="$1" target="$2"
    if [ -d "$link" ]; then
        echo "==> 联接已存在：$link"
        return 0
    fi
    local wlink wtarget
    wlink="$(cygpath -w "$link")"
    wtarget="$(cygpath -w "$target")"
    MSYS2_ARG_CONV_EXCL='*' cmd //c "mklink /J \"$wlink\" \"$wtarget\"" >/dev/null
    echo "==> 创建联接：$link -> $target"
}

make_junction "${HMI_SDK_LINK:-$HOME/.hmi-sdk}" "$TOOLCHAIN"
make_junction "${HMI_SRC_LINK:-$HOME/.hmi-src}" "$HERE/.."

# -------------------------------------------------------------------- 5. 校验
# shellcheck disable=SC1091
source "$HERE/env-win.sh"
echo "==> 校验"
if ! hmi_check_toolchain; then
    echo "工具链校验失败。" >&2
    exit 1
fi
echo "    qmake      : $("$HMI_QT_BIN/qmake.exe" -query QT_VERSION)"
echo "    QT_INSTALL : $("$HMI_QT_BIN/qmake.exe" -query QT_INSTALL_PREFIX)"
echo "    g++        : $("$HMI_MINGW_BIN/g++.exe" -dumpfullversion)"
echo "    lrelease   : $("$HMI_QT_BIN/lrelease.exe" -version)"

cat <<EOF

完成。日常构建直接执行：
    bash tools/build-win.sh

彻底卸载（恢复原样）：
    rm -rf "$TOOLCHAIN" "$HMI_BUILD_DIR"
    rmdir "$HMI_SDK_LINK" "$HMI_SRC_LINK"
EOF
