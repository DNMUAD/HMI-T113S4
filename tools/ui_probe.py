#!/usr/bin/env python
"""UI 回归探针：对运行中的 HMI-T113S4 窗口发真实鼠标点击，并在每步后截图。

用途：验证「总览 ↔ 功能页」反复跳转、以及导航高亮是否唯一。纯 ctypes 调 Win32，
不依赖第三方库。截图用 PrintWindow，窗口被遮挡也能拿到内容。

    python tools/ui_probe.py dist/win/HMI-T113S4.exe docs/shots/probe

坐标按 UiMetrics 计算（导航栏左侧内边距 8，总览按钮高 42，间距 7，列表项高 42）：
    总览按钮中心 ≈ (84, 29)
    第 N 个导航项 ≈ (84, 78 + 44*N)
"""

import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0201, 0x0202
MK_LBUTTON = 0x0001
SRCCOPY = 0x00CC0020
BI_RGB = 0
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
        ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def click(hwnd, x, y):
    lp = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)
    time.sleep(0.45)


def shot(hwnd, path):
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom

    hdc = user32.GetDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(memdc, bmp)

    # PrintWindow 拿真实渲染结果，不依赖窗口是否在最前
    ok = user32.PrintWindow(hwnd, memdc, 0)
    if not ok:
        gdi32.BitBlt(memdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY)

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h          # 负值 = 自上而下
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = BI_RGB

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), DIB_RGB_COLORS)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)

    rows = []
    for y in range(h):
        row = bytearray(buf.raw[y * w * 4:(y + 1) * w * 4])
        for x in range(w):
            i = x * 4
            row[i], row[i + 2] = row[i + 2], row[i]     # BGRA -> RGBA
            row[i + 3] = 255
        rows.append(bytes(row))

    # 写 24 位 BMP，免去第三方图像库
    stride = (w * 3 + 3) & ~3
    pixels = bytearray()
    for row in rows:
        line = bytearray(stride)
        for x in range(w):
            line[x * 3:x * 3 + 3] = bytes((row[x * 4 + 2], row[x * 4 + 1], row[x * 4]))
        pixels += line

    header_size = 14 + 40
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write((header_size + len(pixels)).to_bytes(4, "little"))
        f.write((0).to_bytes(4, "little"))
        f.write(header_size.to_bytes(4, "little"))
        f.write((40).to_bytes(4, "little"))
        f.write(w.to_bytes(4, "little"))
        # 负高度 = 自上而下，与上面 GetDIBits 的取法一致；写正数会导致图像上下颠倒
        f.write((-h).to_bytes(4, "little", signed=True))
        f.write((1).to_bytes(2, "little"))
        f.write((24).to_bytes(2, "little"))
        f.write((0).to_bytes(4, "little"))
        f.write(len(pixels).to_bytes(4, "little"))
        for _ in range(4):
            f.write((0).to_bytes(4, "little"))
        f.write(pixels)


def main():
    # 必须在取窗口尺寸前声明 DPI 感知，否则 125%/150% 缩放下
    # GetClientRect 给的是缩放尺寸、PrintWindow 按物理像素画，截图会被裁切。
    try:
        user32.SetProcessDPIAware()
    except Exception:
        pass

    exe = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "dist/win/HMI-T113S4.exe")
    outdir = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "docs/shots/probe")
    os.makedirs(outdir, exist_ok=True)

    proc = subprocess.Popen([exe, "--windowed"])
    try:
        hwnd = None
        for _ in range(50):
            time.sleep(0.2)
            hwnd = find_window(proc.pid)
            if hwnd:
                break
        if not hwnd:
            print("未找到窗口")
            return 1

        time.sleep(1.2)
        home = (84, 29)
        item1 = (84, 78)          # 第一个导航项（WiFi）

        steps = [
            ("00-start", None),
            ("01-click-wifi", item1),
            ("02-click-home", home),
            ("03-click-wifi-again", item1),
            ("04-click-home-again", home),
            ("05-click-wifi-third", item1),
        ]
        for name, pt in steps:
            if pt:
                click(hwnd, *pt)
            path = os.path.join(outdir, name + ".bmp")
            shot(hwnd, path)
            print("%-22s -> %s" % (name, os.path.basename(path)))
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
