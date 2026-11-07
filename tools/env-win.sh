#!/usr/bin/env bash
# 把工作区自带的 Qt 5.15.2 + MinGW 8.1 前置到「当前进程」的 PATH。
# 不写系统环境变量、不碰注册表，退出这个 shell 就失效。
#
# 为什么必须经 ASCII 目录联接访问：
#   qmake 读 bin/qt.conf 里的相对前缀（Prefix=..）时按可执行文件所在目录解析。
#   该目录一旦含非 ASCII 字符，Qt 会解析失败并退化成 "/.."，
#   于是 QT_INSTALL_HEADERS / QT_INSTALL_LIBS 全部错位，编译期直接找不到头文件。
#   同理构建目录也必须落在 ASCII 路径，否则 Makefile 里的源文件路径会被写坏。
#
# 用法：  source tools/env-win.sh

HMI_HOME="${HMI_HOME:-$HOME}"

# ASCII 联接：$HMI_SDK_LINK -> 工作区 .toolchain，$HMI_SRC_LINK -> 工作区 HMI-T113S4
export HMI_SDK_LINK="${HMI_SDK_LINK:-$HMI_HOME/.hmi-sdk}"
export HMI_SRC_LINK="${HMI_SRC_LINK:-$HMI_HOME/.hmi-src}"
export HMI_BUILD_DIR="${HMI_BUILD_DIR:-$HMI_HOME/.hmi-build}"

export HMI_QT_BIN="$HMI_SDK_LINK/5.15.2/mingw81_64/bin"
export HMI_MINGW_BIN="$HMI_SDK_LINK/Tools/mingw810_64/bin"

# 必须前置：系统里可能已经有别的 MinGW（例如 Qt 4.8.7 自带的 32 位 4.8.2），
# 被它抢先会拿到 32 位 libstdc++，运行时报 0xc000007b。
export PATH="$HMI_QT_BIN:$HMI_MINGW_BIN:$PATH"

hmi_check_toolchain()
{
    local ok=0
    if [ ! -x "$HMI_QT_BIN/qmake.exe" ]; then
        echo "缺少 qmake：$HMI_QT_BIN/qmake.exe" >&2; ok=1
    fi
    if [ ! -x "$HMI_MINGW_BIN/g++.exe" ]; then
        echo "缺少 g++：$HMI_MINGW_BIN/g++.exe" >&2; ok=1
    fi

    local qtver
    qtver=$("$HMI_QT_BIN/qmake.exe" -query QT_VERSION 2>/dev/null)
    if [ "$qtver" != "5.15.2" ]; then
        echo "qmake 版本异常：期望 5.15.2，实际 '$qtver'" >&2
        echo "（若是 \"\" 或 /.. ，说明路径含非 ASCII 字符，请经 $HMI_SDK_LINK 访问）" >&2
        ok=1
    fi

    local gccver
    gccver=$("$HMI_MINGW_BIN/g++.exe" -dumpfullversion 2>/dev/null)
    case "$gccver" in
        8.1.0*) ;;
        *) echo "g++ 版本异常：期望 8.1.0，实际 '$gccver'" >&2; ok=1 ;;
    esac
    return $ok
}
