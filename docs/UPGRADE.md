# U 盘升级

通过插入 U 盘、读取升级包、覆盖主程序 / 翻译 / 配置并重启完成升级。

## 设计来源

Qt 官方有 `qt/qtotaupdate`（Embedded Linux OTA Update），但它**不是可复用的 C++ 升级库**：
它的部署与回滚能力来自 **ostree**（`qt-ostree/` 是 Python/shell 调 `ostree admin deploy`），
外加 `dracut` 的 initramfs 钩子和 systemd 服务。要照搬，板端得先有一套 ostree 仓库镜像。

目标板是直接铺文件（`/usr/bin/QtApplication` + `/usr/share/hmi/i18n` + `/etc/hmi.ini`），
没有 ostree，因此**借用它的流程而不是代码**：

```
版本清单 → 完整性校验 → 落入 staging → 原子替换 → 失败回滚 → 重启生效
```

和 ostree 的区别只在"原子替换"的实现：ostree 靠只读快照 + 符号链接切换，
这里靠同目录 `.new` 文件 + `rename()`（POSIX 下 rename 原子且可替换正在运行的可执行文件）。

## 升级包格式

放在 U 盘根目录（目录名可用 `[upgrade] packageDir` 改）：

```
<U盘>/hmi-upgrade/
├── manifest.json
├── QtApplication
├── i18n/HMI-T113S4_zh_CN.qm
└── hmi.ini
```

`manifest.json`：

```json
{
  "product": "HMI-T113S4",
  "version": "1.1.0",
  "notes": "修复界面比例与导航跳转",
  "files": [
    { "target": "bin",    "path": "QtApplication",            "sha256": "…", "size": 665088, "mode": "0755" },
    { "target": "i18n",   "path": "i18n/HMI-T113S4_zh_CN.qm", "sha256": "…", "size": 156 },
    { "target": "config", "path": "hmi.ini",                  "sha256": "…", "size": 1504 }
  ]
}
```

| 字段 | 说明 |
|---|---|
| `product` | 必须等于 `HMI-T113S4`（留空则不校验），防止刷错产品的包 |
| `version` | 显示用；不强制单调递增，现场允许重刷同版本 |
| `files[].target` | 只认 `bin` / `i18n` / `config` 三类 |
| `files[].path` | 包内相对路径，**禁止** `..`、绝对路径、盘符 |
| `files[].sha256` | 留空则只校验存在性与大小 |
| `files[].mode` | 含 `755` 时给可执行权限（主程序必须） |

`target` 决定落盘位置，包无法指定任意路径：

| target | 板端 | 桌面调试（Windows） |
|---|---|---|
| `bin` | `/usr/bin/QtApplication` | `<exe目录>/_upgrade_target/QtApplication.exe` |
| `i18n` | `/usr/share/hmi/i18n/` | `<exe目录>/_upgrade_target/i18n/` |
| `config` | `/etc/hmi.ini` | `<exe目录>/_upgrade_target/hmi.ini` |

桌面调试之所以换到沙箱目录：Windows 无法替换正在运行的 exe，换路径后
复制 / 备份 / 回滚的完整逻辑仍能跑通并被验证。

## 安装流程

1. **扫描**：读 `/proc/mounts`（Linux）或 `QStorageInfo` 可移动卷（Windows），
   在 `<挂载点>/hmi-upgrade/` 找 `manifest.json`
2. **校验**：逐文件比对大小与 SHA-256，任一不符即中止，**不会**动目标文件
3. **安装**（逐文件，每步一个事件循环 tick，界面不卡）：
   - 复制到 `<目标>.new`（同目录，保证 rename 不跨设备）
   - 已有目标 → 先 `rename` 成 `<目标>.bak`
   - `.new` → 目标（POSIX `rename`，原子且可替换运行中的二进制；直接 `cp` 覆盖会 `ETXTBSY`）
   - 任一步失败 → 按 `.bak` 逆序回滚
4. **记录**：写 `upgrade-state.json`（板端 `/etc/`，桌面在 exe 同目录），
   页面据此显示"待生效版本"
5. **重启**：`sync; sleep 1; reboot`（板端）；桌面只提示不重启

## 制作升级包

```bash
python tools/make-upgrade-package.py D:/hmi-usb 1.1.0 \
    --bin dist/win/HMI-T113S4.exe \
    --qm  /c/Users/<user>/.hmi-build/.qm/HMI-T113S4_zh_CN.qm \
    --ini etc/hmi.ini \
    --notes "本次改动说明"
```

脚本会算 SHA-256 并写好 manifest，把 `D:/hmi-usb` 整盘拷到 U 盘即用。

## 无 U 盘时怎么测

在 exe 同目录放 `hmi.ini`，把任意目录当"U 盘"：

```ini
[upgrade]
roots=C:/Users/HP_Sinrobot/hmi-usb
```

然后跑无人值守自检（扫描 → 校验 → 安装 → 回滚，退出码 0 表示全链路通过）：

```bash
dist/win/HMI-T113S4.exe --upgrade-selftest
```

自检装完会立即回滚，不留下改动，适合反复跑。

## 已验证的边界

| 用例 | 期望 | 实测 |
|---|---|---|
| 正常包 | 扫描→校验→安装→回滚全通 | 退出码 0，回滚后文件恢复为旧内容，无 `.bak`/`.new` 残留 |
| 包内文件被改 1 字节 | 校验失败不安装 | `大小不符（期望 1504，实际 1505）`，停在 VerifyFailed |
| 清单 `path` 写成 `../../evil.bin` | 拒绝 | `清单路径非法`，停在 NoPackage |
| `product` 改成别的产品名 | 拒绝 | `升级包产品不匹配`，停在 NoPackage |
| 回滚之后 | 待生效标记清除 | `upgrade-state.json` 被删除，页面不再提示"待生效" |

## 板端注意事项

- 程序以 root 运行（init.d 拉起），覆盖 `/usr/bin` 与 `/etc` 需要该权限
- 升级后 `S70launcher` 重启会自动拉起新版本，无需额外操作
- 若 `/etc/hmi.ini` 被覆盖，重启后会按新配置运行；建议包里保留 `web/roots` 等现场配置
- 备份文件 `<目标>.bak` 会一直留在原地，确认新版本稳定后可手动清理
- U 盘挂载点因 udev 规则而异（`/media/sda1`、`/run/media/sda1`、`/mnt/udisk`），
  自动扫描覆盖这些情况；固定路径可在 `[upgrade] roots` 里写死
