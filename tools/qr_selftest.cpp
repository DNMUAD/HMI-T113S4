// 二维码编码器离线自测（不参与 qmake 构建）
//
//   g++ -O2 -o qr_selftest tools/qr_selftest.cpp core/qrcode.cpp
//   ./qr_selftest "http://192.168.1.23:8080/" 0 -1
//
// 输出：VERSION / MASK / SIZE 三行，随后是 size 行 0/1 矩阵（1 = 黑）。
// 与 segno 生成的矩阵逐位对拍请用 tools/qr_compare.py。

#include "../core/qrcode.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    const char *text = (argc > 1) ? argv[1] : "http://192.168.1.23:8080/";
    const int version = (argc > 2) ? std::atoi(argv[2]) : 0;
    const int mask = (argc > 3) ? std::atoi(argv[3]) : -1;

    QrCode qr;
    if (!qr.encode(text, version, mask)) {
        std::printf("ERROR\n");
        return 1;
    }

    if (argc > 4 && std::strcmp(argv[4], "codewords") == 0) {
        const std::vector<unsigned char> cw = qr.codewords();
        std::printf("CW");
        for (size_t i = 0; i < cw.size(); ++i)
            std::printf(" %02X", cw[i]);
        std::printf("\n");
        return 0;
    }

    std::printf("VERSION %d\n", qr.version());
    std::printf("MASK %d\n", qr.mask());
    std::printf("SIZE %d\n", qr.size());

    for (int y = 0; y < qr.size(); ++y) {
        std::string row;
        for (int x = 0; x < qr.size(); ++x)
            row += qr.module(x, y) ? '1' : '0';
        std::printf("%s\n", row.c_str());
    }
    return 0;
}
