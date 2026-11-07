#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""二维码编码器端到端验证：把 core/qrcode.cpp 生成的矩阵渲染成图，再用独立解码器读回来。

    g++ -O2 -o build/qr_selftest.exe tools/qr_selftest.cpp core/qrcode.cpp
    python -m pip install opencv-python-headless
    python tools/qr_verify.py build/qr_selftest.exe

覆盖：7 种长度（V1–V10）× 9 种掩码（自动 + 0..7），每项都要求解码结果与原文完全一致。
退出码 0 = 全部通过。
"""
import os
import subprocess
import sys

try:
    import numpy as np
    import cv2
except ImportError:
    print('需要 opencv-python-headless 与 numpy：python -m pip install opencv-python-headless')
    sys.exit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'qr_selftest.exe')

SAMPLES = [
    'http://192.168.1.23:8080/',
    'http://10.0.0.8:8080/?k=7f3a1c',
    'http://192.168.100.250:9978/',
    'A',
    'http://t113-s4.local:8080/' + 'x' * 20,
    'http://192.168.1.100:8080/' + 'y' * 120,
    'http://192.168.1.100:8080/' + 'z' * 200,
]


def render(rows, scale=8, quiet=4):
    size = len(rows)
    total = size + quiet * 2
    image = np.ones((total, total), dtype=np.uint8) * 255
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == '1':
                image[y + quiet, x + quiet] = 0
    image = cv2.resize(image, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
    return cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)


def main():
    if not os.path.isfile(EXE):
        print('找不到自测程序：%s' % EXE)
        return 2

    detector = cv2.QRCodeDetector()
    passed = 0
    failed = 0

    for text in SAMPLES:
        for mask in [-1] + list(range(8)):
            out = subprocess.check_output([EXE, text, '0', str(mask)]).decode('utf-8').splitlines()
            if not out or out[0].startswith('ERROR'):
                print('[FAIL] 编码失败 len=%d mask=%d' % (len(text), mask))
                failed += 1
                continue
            version = out[0].split()[1]
            size = int(out[2].split()[1])
            rows = out[3:3 + size]
            decoded, _, _ = detector.detectAndDecode(render(rows))
            if decoded == text:
                passed += 1
            else:
                failed += 1
                print('[FAIL] 解码不一致 v=%s len=%d mask=%d got=%r'
                      % (version, len(text), mask, decoded[:40]))

    print('二维码验证：通过 %d 项，失败 %d 项' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
