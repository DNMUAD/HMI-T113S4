#ifndef HMI_CORE_QRCODE_H
#define HMI_CORE_QRCODE_H

#include <string>
#include <vector>

// 二维码编码器（纯 C++，不依赖 Qt / 不依赖网络）
//
// 为什么自己写：设备端 rootfs 里没有二维码库，也不能指望手机侧 CDN，
// 而「扫码打开网页」必须在设备屏幕上就地画出二维码。
//
// 实现范围：字节模式、纠错等级 L、版本 1–10（最长 271 字节）。
// 只选 L 是因为内容永远是 http://<ip>:<port>/ 这类短串（≤40 字节，落在 V2–V3），
// 表越少越不容易错；容量不足时 encode() 返回 false，由调用方降级为纯文本地址。
//
// 与 segno 逐模块对拍过（tools/qr_selftest.cpp + tools/qr_compare.py）。
class QrCode
{
public:
    QrCode();

    // forceVersion: 0 = 自动选择最小可行版本；forceMask: -1 = 自动评测掩码
    bool encode(const std::string &text, int forceVersion = 0, int forceMask = -1);

    bool isValid() const;

    int  version() const;               // 1..10
    int  size() const;                  // 模块数 = 17 + 4*version
    int  mask() const;                  // 实际使用的掩码 0..7
    bool module(int x, int y) const;    // true = 黑模块（y 为行，x 为列）

    // 仅调试用：交织后的完整码字序列（数据 + 纠错）
    std::vector<unsigned char> codewords() const;

private:
    int  m_version;
    int  m_size;
    int  m_mask;
    bool m_valid;
    std::vector<unsigned char> m_modules;   // size*size，1 = 黑
    std::vector<unsigned char> m_codewords; // 数据 + 纠错（调试/对拍用）
};

#endif // HMI_CORE_QRCODE_H
