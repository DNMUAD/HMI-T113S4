#!/usr/bin/env python
"""生成一个 U 盘升级包。

包结构（放在 U 盘根目录）：
    <U盘>/hmi-upgrade/
        manifest.json
        QtApplication
        i18n/HMI-T113S4_zh_CN.qm
        hmi.ini

用法：
    python tools/make-upgrade-package.py <输出目录> <版本号> \
        [--bin dist/win/HMI-T113S4.exe] \
        [--qm .qm/HMI-T113S4_zh_CN.qm] \
        [--ini etc/hmi.ini] \
        [--notes "说明文字"]

`target` 只有 bin / i18n / config 三种，落到哪个路径由 UpgradeService 写死，
包里无法指定任意目标位置。
"""

import argparse
import hashlib
import json
import os
import shutil

FILES = [
    # (target, 包内相对路径, 命令行开关, 是否可执行)
    ("bin", "QtApplication", "bin", True),
    ("i18n", "i18n/HMI-T113S4_zh_CN.qm", "qm", False),
    ("config", "hmi.ini", "ini", False),
]


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("version")
    ap.add_argument("--bin", default="dist/win/HMI-T113S4.exe")
    ap.add_argument("--qm", default=".qm/HMI-T113S4_zh_CN.qm")
    ap.add_argument("--ini", default="etc/hmi.ini")
    ap.add_argument("--notes", default="")
    ap.add_argument("--product", default="HMI-T113S4")
    args = ap.parse_args()

    pkg = os.path.join(os.path.abspath(args.outdir), "hmi-upgrade")
    os.makedirs(os.path.join(pkg, "i18n"), exist_ok=True)

    entries = []
    for target, rel, key, executable in FILES:
        src = getattr(args, key)
        if not os.path.isfile(src):
            raise SystemExit("源文件不存在: %s" % src)
        dst = os.path.join(pkg, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        entry = {
            "target": target,
            "path": rel,
            "sha256": sha256_of(dst),
            "size": os.path.getsize(dst),
        }
        if executable:
            entry["mode"] = "0755"
        entries.append(entry)
        print("  %-8s %-28s %8d B" % (target, rel, entry["size"]))

    manifest = {
        "product": args.product,
        "version": args.version,
        "notes": args.notes,
        "files": entries,
    }
    with open(os.path.join(pkg, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    print("包已生成: %s (v%s, %d 个文件)" % (pkg, args.version, len(entries)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
