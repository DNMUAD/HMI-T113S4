# 局域网网页文件管理（扫码打开）

> 需求：手机扫设备屏幕上的二维码 → 打开网页 → 浏览 / 上传 / 下载 / 重命名 / 删除设备上的文件。
> 参考实现：FongMi/TV 的「远程管理」（`app/src/main/java/com/fongmi/android/tv/server`：Nano=http 服务 + 网页资源 + 应用内显示地址）。

## 1. 与 FongMi 的对应关系

| FongMi（Android / Java） | 本项目（T113 / Qt C++） | 说明 |
|---|---|---|
| `Nano.java`（nanohttpd） | `core/httpserver.*`（`QTcpServer` 实现） | 不引第三方库，纯 Qt 网络栈；只实现需要的 HTTP 子集 |
| `Server.start()` 端口 9978–9999 顺延 | `WebShareService::start()` 从 `web/port` 顺延 10 个 | 端口被占时自动换一个，界面显示实际端口 |
| `assets/web/*` 网页资源 | `resources/web/*` → qrc `:/web/*` | 随二进制走，rootfs 不需要额外文件 |
| `Server.getPageAddress(tab)` | `WebShareService::url()` | `http://<ip>:<port>/?k=<token>` |
| 应用内展示地址 / 二维码 | `pages/websharepage` + `widgets/qrcodewidget` | 二维码由 `core/qrcode` 就地生成（设备无外网，不能依赖 CDN） |
| 媒体库操作 | 文件系统操作 | 本机没有媒体库概念，网页只做文件管理 |

## 2. 调用链

```
手机浏览器
   │  HTTP/1.1（GET 静态资源 / GET 下载 / POST JSON / POST 流式上传）
   ▼
core/httpserver        解析请求、路由分发、连接数限制、令牌校验、按 bytesToWrite 节流
   ▼
services/webshareservice   注册路由、组织 JSON、把上传流落盘
   ▼
services/fileservice       路径沙箱（白名单 + canonical）+ 目录/改名/删除/容量
   ▼
文件系统（/mnt/usb、/media、/usr/share、/tmp …）
```

页面（`pages/websharepage`）不碰 socket、不碰 QFile：它只读 `WebShareService` 的
`port()/url()/clients()/lastRequest()`，把地址喂给 `QrCodeWidget`。

## 3. HTTP 接口

| 方法 | 路径 | 参数 | 返回 |
|---|---|---|---|
| GET | `/` `/index.html` | — | 手机端网页（内嵌 qrc） |
| GET | `/app.css` `/app.js` | — | 静态资源 |
| GET | `/api/roots` | — | `{ok, roots:[{name,path}]}` |
| GET | `/api/list` | `path` | `{ok, path, parent, isRoot, items:[{name,path,dir,size,mtime}], free, total}` |
| GET | `/api/download` | `path` | 文件流（`Content-Disposition: attachment; filename*=UTF-8''…`） |
| POST | `/api/upload` | `path`(目录) + `name`(文件名)，body 为文件原始字节 | `{ok, name, size}` |
| POST | `/api/mkdir` | JSON `{path, name}` | `{ok}` |
| POST | `/api/rename` | JSON `{path, name}` | `{ok}` |
| POST | `/api/delete` | JSON `{path, recursive}` | `{ok}` |

约定：
- 所有请求都是 `Connection: close`（不做 keep-alive，状态机越简单越不容易在板端出玄学问题）。
- `Content-Length ≤ 256KB` 的请求体一次性读进内存（JSON 接口够用）；超过则要求 handler
  调用 `attachBodySink()` 流式接收 —— 上传走这条路，永远不会把整个文件读进 256MB 内存。
- 下载按 `bytesToWrite() < 256KB` 节流，64KB 一块往 socket 写。

## 4. 安全模型（进程以 root 运行，这层必须做死）

1. **路径白名单**：`web/roots` 里配置的目录，`FileService::canonical()` 对每个路径做
   `QDir::cleanPath` + `canonicalFilePath`，再用「等于根 或 以 `根/` 开头」判断，符号链接指到外面也会被拒。
2. **目标路径二次校验**：新建/上传/改名的目标用「父目录 canonical + 文件名」再验一次，避免 `../` 逃逸。
3. **根目录保护**：白名单根本身不允许删除或改名。
4. **文件名净化**：只接受单层名（不含 `/` `\`，不是 `.`/`..`，长度 ≤ 200）。
5. **令牌（可选）**：`web/token` 非空时，所有请求必须带 `?k=<token>` 或 `X-Token`；二维码里已带令牌，
   手机扫码即用。留空 = 仅靠「同一局域网」隔离。
6. **资源限制**：请求头 16KB、内联 body 256KB、单请求体 4GB 上限、最多 16 个并发连接。

> 生产环境建议：`web/roots` 只放 U 盘/媒体目录，别把 `/` 或 `/etc` 加进去；对外网桥接的场景请配 `web/token`。

## 5. 配置（`/etc/hmi.ini`）

```ini
[web]
port=8080            ; 被占用时自动尝试后面 9 个端口
token=               ; 留空=免令牌；填了就必须带 ?k=
roots=/mnt/usb,/media,/usr/share,/tmp
autostart=true       ; false 则在「网页管理」页手动启动
```

## 6. 二维码

`core/qrcode.*` 是自己写的编码器（字节模式、纠错 L、版本 1–10，最长 271 字节）：

- 设备端 rootfs 没有二维码库，手机侧也不该依赖外网 CDN，所以必须本地生成；
- URL 约 30 字节，落在 V2–V3，25–29 模块，`QrCodeWidget` 按整数倍放大 + 4 模块静区，保证边缘不糊；
- **验证方式**：`tools/qr_selftest.cpp` 生成矩阵 → OpenCV `QRCodeDetector` 解码，
  覆盖 7 种长度（V1–V10）× 9 种掩码（自动 + 0..7）共 63 组，全部还原出原文；
- 复验命令：

```bash
g++ -O2 -o build/qr_selftest.exe tools/qr_selftest.cpp core/qrcode.cpp
python tools/qr_verify.py build/qr_selftest.exe      # 需要 opencv-python-headless
```

示例产物：`docs/sample-qr.png`（内容 `http://192.168.1.23:8080/?k=7f3a1c`，由同一编码器输出，扫码可验证）。

## 7. 已知限制

| 限制 | 说明 | 后续可做 |
|---|---|---|
| 无账号体系 | 只有「同局域网 + 可选令牌」 | 加 4 位 PIN 登录页 / HTTP Basic |
| 上传不可续传 | 断线重传整份文件 | 支持 `Range` 分片 |
| 无 keep-alive | 每个请求一条 TCP 连接 | 需要时再上状态机，局域网内收益有限 |
| 大目录一次全返回 | 上千个文件时 JSON 偏大 | 加分页 / 虚拟列表 |
| 不支持预览 | 只做下载，视频由手机播放器接管 | 内嵌 `<video>` + `Range` 请求 |
