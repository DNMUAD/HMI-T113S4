#include "qrcode.h"

#include <cstdlib>
#include <cstring>

namespace {

const int kMaxVersion = 10;

// 纠错等级 L 的版本参数：总码字 / 每块纠错码字 / 块数 / 数据码字总数
struct VersionSpec
{
    int total;
    int eccPerBlock;
    int blocks;
    int data;
};

const VersionSpec kSpec[kMaxVersion] = {
    {  26,  7, 1,  19 },   // V1
    {  44, 10, 1,  34 },   // V2
    {  70, 15, 1,  55 },   // V3
    { 100, 20, 1,  80 },   // V4
    { 134, 26, 1, 108 },   // V5
    { 172, 18, 2, 136 },   // V6
    { 196, 20, 2, 156 },   // V7
    { 242, 24, 2, 194 },   // V8
    { 292, 30, 2, 232 },   // V9
    { 346, 18, 4, 274 }    // V10
};

// 校正图形中心坐标（0 结尾）
const int kAlign[kMaxVersion][7] = {
    { 0,  0,  0,  0,  0,  0, 0 },
    { 6, 18,  0,  0,  0,  0, 0 },
    { 6, 22,  0,  0,  0,  0, 0 },
    { 6, 26,  0,  0,  0,  0, 0 },
    { 6, 30,  0,  0,  0,  0, 0 },
    { 6, 34,  0,  0,  0,  0, 0 },
    { 6, 22, 38,  0,  0,  0, 0 },
    { 6, 24, 42,  0,  0,  0, 0 },
    { 6, 26, 46,  0,  0,  0, 0 },
    { 6, 28, 50,  0,  0,  0, 0 }
};

unsigned char gfMul(unsigned char x, unsigned char y)
{
    // GF(256) 乘法，本原多项式 0x11D
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<unsigned char>(z);
}

void reedSolomon(const std::vector<unsigned char> &data, int degree,
                 std::vector<unsigned char> &ecc)
{
    // 生成多项式（最高次在 [0]）
    std::vector<unsigned char> gen(degree, 0);
    if (degree > 0) {
        gen[degree - 1] = 1;
        unsigned char root = 1;
        for (int i = 0; i < degree; ++i) {
            for (int j = 0; j < degree; ++j) {
                gen[j] = gfMul(gen[j], root);
                if (j + 1 < degree)
                    gen[j] ^= gen[j + 1];
            }
            root = gfMul(root, 0x02);
        }
    }

    std::vector<unsigned char> rem(degree, 0);
    for (size_t k = 0; k < data.size(); ++k) {
        const unsigned char factor = data[k] ^ rem[0];
        for (int j = 0; j + 1 < degree; ++j)
            rem[j] = rem[j + 1];
        if (degree > 0)
            rem[degree - 1] = 0;
        for (int j = 0; j < degree; ++j)
            rem[j] ^= gfMul(gen[j], factor);
    }
    ecc = rem;
}

int bitAt(const std::vector<unsigned char> &bytes, int index)
{
    return (bytes[index >> 3] >> (7 - (index & 7))) & 1;
}

} // namespace

QrCode::QrCode()
    : m_version(0)
    , m_size(0)
    , m_mask(-1)
    , m_valid(false)
{
}

bool QrCode::isValid() const
{
    return m_valid;
}

int QrCode::version() const
{
    return m_version;
}

int QrCode::size() const
{
    return m_size;
}

int QrCode::mask() const
{
    return m_mask;
}

bool QrCode::module(int x, int y) const
{
    if (!m_valid || x < 0 || y < 0 || x >= m_size || y >= m_size)
        return false;
    return m_modules[static_cast<size_t>(y) * m_size + x] != 0;
}

std::vector<unsigned char> QrCode::codewords() const
{
    return m_codewords;
}

bool QrCode::encode(const std::string &text, int forceVersion, int forceMask)
{
    m_valid = false;
    m_version = 0;
    m_size = 0;
    m_mask = -1;
    m_modules.clear();

    if (text.empty())
        return false;

    // 1) 选版本：最小的、放得下的版本
    int version = forceVersion;
    if (version <= 0) {
        for (int v = 1; v <= kMaxVersion; ++v) {
            const int countBits = (v <= 9) ? 8 : 16;
            const int needed = 4 + countBits + static_cast<int>(text.size()) * 8;
            if (needed <= kSpec[v - 1].data * 8) {
                version = v;
                break;
            }
        }
    }
    if (version <= 0 || version > kMaxVersion)
        return false;

    const VersionSpec &spec = kSpec[version - 1];
    const int countBits = (version <= 9) ? 8 : 16;
    if (static_cast<int>(text.size()) * 8 + 4 + countBits > spec.data * 8)
        return false;

    // 2) 比特流 → 数据码字
    std::vector<unsigned char> bits;
    bits.reserve(spec.data * 8 + 16);

    struct BitWriter
    {
        std::vector<unsigned char> *out;
        int count;
        void put(int value, int len)
        {
            for (int i = len - 1; i >= 0; --i) {
                if ((count & 7) == 0)
                    out->push_back(0);
                if ((value >> i) & 1)
                    (*out)[out->size() - 1] |= static_cast<unsigned char>(0x80 >> (count & 7));
                ++count;
            }
        }
    } writer;
    writer.out = &bits;
    writer.count = 0;

    writer.put(0x4, 4);                                    // 字节模式
    writer.put(static_cast<int>(text.size()), countBits);   // 字符数
    for (size_t i = 0; i < text.size(); ++i)
        writer.put(static_cast<unsigned char>(text[i]), 8);

    const int capacityBits = spec.data * 8;
    const int terminator = (capacityBits - writer.count < 4) ? (capacityBits - writer.count) : 4;
    writer.put(0, terminator);

    while ((writer.count & 7) != 0)
        writer.put(0, 1);

    for (int pad = 0xEC; writer.count < capacityBits; pad ^= 0xEC ^ 0x11)
        writer.put(pad, 8);

    // 3) 分块 + 纠错 + 交织
    const int numBlocks = spec.blocks;
    const int shortLen = spec.data / numBlocks;
    const int numLong = spec.data % numBlocks;

    std::vector<std::vector<unsigned char> > blocks(numBlocks);
    std::vector<std::vector<unsigned char> > eccBlocks(numBlocks);
    int offset = 0;
    for (int b = 0; b < numBlocks; ++b) {
        const int len = shortLen + (b >= numBlocks - numLong ? 1 : 0);
        blocks[b].assign(bits.begin() + offset, bits.begin() + offset + len);
        offset += len;
        reedSolomon(blocks[b], spec.eccPerBlock, eccBlocks[b]);
    }

    std::vector<unsigned char> codewords;
    codewords.reserve(spec.total);
    const int maxLen = shortLen + (numLong > 0 ? 1 : 0);
    for (int i = 0; i < maxLen; ++i) {
        for (int b = 0; b < numBlocks; ++b) {
            if (i < static_cast<int>(blocks[b].size()))
                codewords.push_back(blocks[b][i]);
        }
    }
    for (int i = 0; i < spec.eccPerBlock; ++i) {
        for (int b = 0; b < numBlocks; ++b)
            codewords.push_back(eccBlocks[b][i]);
    }

    // 4) 画功能图形
    const int size = 17 + 4 * version;
    std::vector<int> grid(static_cast<size_t>(size) * size, -1);
    std::vector<unsigned char> isFunction(static_cast<size_t>(size) * size, 0);

    struct Builder
    {
        std::vector<int> *g;
        std::vector<unsigned char> *fn;
        int size;

        void setFunction(int x, int y, bool dark)
        {
            if (x < 0 || y < 0 || x >= size || y >= size)
                return;
            (*g)[static_cast<size_t>(y) * size + x] = dark ? 1 : 0;
            (*fn)[static_cast<size_t>(y) * size + x] = 1;
        }

        void setFunctionSafe(int x, int y, bool dark)
        {
            setFunction(x, y, dark);
        }
    } bld;
    bld.g = &grid;
    bld.fn = &isFunction;
    bld.size = size;

    // 定时图形
    for (int i = 0; i < size; ++i) {
        bld.setFunction(6, i, (i % 2) == 0);
        bld.setFunction(i, 6, (i % 2) == 0);
    }

    // 三个定位图形（含分隔符）
    const int finder[3][2] = { { 3, 3 }, { size - 4, 3 }, { 3, size - 4 } };
    for (int f = 0; f < 3; ++f) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int dist = std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
                bld.setFunction(finder[f][0] + dx, finder[f][1] + dy,
                                dist != 2 && dist != 4);
            }
        }
    }

    // 校正图形
    const int *align = kAlign[version - 1];
    int alignCount = 0;
    while (alignCount < 7 && align[alignCount] != 0)
        ++alignCount;
    for (int i = 0; i < alignCount; ++i) {
        for (int j = 0; j < alignCount; ++j) {
            const int cx = align[i];
            const int cy = align[j];
            const bool corner = (cx == 6 && cy == 6)
                                || (cx == 6 && cy == size - 7)
                                || (cx == size - 7 && cy == 6);
            if (corner)
                continue;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int dist = std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
                    bld.setFunction(cx + dx, cy + dy, dist != 1);
                }
            }
        }
    }

    // 预留格式信息（值稍后填）
    for (int i = 0; i <= 5; ++i)
        bld.setFunction(8, i, false);
    bld.setFunction(8, 7, false);
    bld.setFunction(8, 8, false);
    bld.setFunction(7, 8, false);
    for (int i = 9; i < 15; ++i)
        bld.setFunction(14 - i, 8, false);
    for (int i = 0; i < 8; ++i)
        bld.setFunction(size - 1 - i, 8, false);
    for (int i = 8; i < 15; ++i)
        bld.setFunction(8, size - 15 + i, false);
    bld.setFunction(8, size - 8, true);      // 固定黑模块

    // 版本信息（V7 起）
    if (version >= 7) {
        int rem = version;
        for (int i = 0; i < 12; ++i)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        const int info = (version << 12) | (rem & 0xFFF);
        for (int i = 0; i < 18; ++i) {
            const bool bit = ((info >> i) & 1) != 0;
            const int a = size - 11 + i % 3;
            const int b = i / 3;
            bld.setFunction(a, b, bit);
            bld.setFunction(b, a, bit);
        }
    }

    // 5) 填数据（右下角起 Z 字形）
    int bitIndex = 0;
    const int totalBits = static_cast<int>(codewords.size()) * 8;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? size - 1 - vert : vert;
                const size_t idx = static_cast<size_t>(y) * size + x;
                if (isFunction[idx])
                    continue;
                if (bitIndex < totalBits) {
                    grid[idx] = bitAt(codewords, bitIndex) ? 1 : 0;
                    ++bitIndex;
                } else {
                    grid[idx] = 0;      // 剩余位为 0
                }
            }
        }
    }

    // 6) 掩码 + 格式信息 + 罚分，取最优
    struct Masker
    {
        static bool masked(int mask, int x, int y)
        {
            switch (mask) {
            case 0: return (x + y) % 2 == 0;
            case 1: return y % 2 == 0;
            case 2: return x % 3 == 0;
            case 3: return (x + y) % 3 == 0;
            case 4: return (x / 3 + y / 2) % 2 == 0;
            case 5: return (x * y) % 2 + (x * y) % 3 == 0;
            case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
            default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
            }
        }
    };

    int bestMask = -1;
    int bestScore = 0;
    std::vector<int> bestGrid;

    const int firstMask = (forceMask >= 0 && forceMask <= 7) ? forceMask : 0;
    const int lastMask = (forceMask >= 0 && forceMask <= 7) ? forceMask : 7;

    for (int mask = firstMask; mask <= lastMask; ++mask) {
        std::vector<int> cand = grid;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const size_t idx = static_cast<size_t>(y) * size + x;
                if (!isFunction[idx] && Masker::masked(mask, x, y))
                    cand[idx] = cand[idx] ? 0 : 1;
            }
        }

        // 格式信息：纠错等级 L = 0b01
        int data = (0x01 << 3) | mask;
        int rem = data;
        for (int i = 0; i < 10; ++i)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = ((data << 10) | (rem & 0x3FF)) ^ 0x5412;

        struct Writer2
        {
            std::vector<int> *g;
            int size;
            void set(int x, int y, bool dark)
            {
                (*g)[static_cast<size_t>(y) * size + x] = dark ? 1 : 0;
            }
        } fw;
        fw.g = &cand;
        fw.size = size;

        for (int i = 0; i <= 5; ++i)
            fw.set(8, i, ((bits >> i) & 1) != 0);
        fw.set(8, 7, ((bits >> 6) & 1) != 0);
        fw.set(8, 8, ((bits >> 7) & 1) != 0);
        fw.set(7, 8, ((bits >> 8) & 1) != 0);
        for (int i = 9; i < 15; ++i)
            fw.set(14 - i, 8, ((bits >> i) & 1) != 0);
        for (int i = 0; i < 8; ++i)
            fw.set(size - 1 - i, 8, ((bits >> i) & 1) != 0);
        for (int i = 8; i < 15; ++i)
            fw.set(8, size - 15 + i, ((bits >> i) & 1) != 0);
        fw.set(8, size - 8, true);

        // 罚分（QR 规范四条规则）
        int score = 0;
        for (int y = 0; y < size; ++y) {
            int run = 1;
            for (int x = 1; x <= size; ++x) {
                if (x < size && cand[static_cast<size_t>(y) * size + x]
                                 == cand[static_cast<size_t>(y) * size + x - 1]) {
                    ++run;
                } else {
                    if (run >= 5)
                        score += 3 + (run - 5);
                    run = 1;
                }
            }
        }
        for (int x = 0; x < size; ++x) {
            int run = 1;
            for (int y = 1; y <= size; ++y) {
                if (y < size && cand[static_cast<size_t>(y) * size + x]
                                 == cand[static_cast<size_t>(y - 1) * size + x]) {
                    ++run;
                } else {
                    if (run >= 5)
                        score += 3 + (run - 5);
                    run = 1;
                }
            }
        }
        for (int y = 0; y + 1 < size; ++y) {
            for (int x = 0; x + 1 < size; ++x) {
                const int v = cand[static_cast<size_t>(y) * size + x];
                if (cand[static_cast<size_t>(y) * size + x + 1] == v
                    && cand[static_cast<size_t>(y + 1) * size + x] == v
                    && cand[static_cast<size_t>(y + 1) * size + x + 1] == v)
                    score += 3;
            }
        }

        const int finderPatternA[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
        const int finderPatternB[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x + 11 <= size; ++x) {
                bool matchA = true;
                bool matchB = true;
                for (int k = 0; k < 11; ++k) {
                    const int v = cand[static_cast<size_t>(y) * size + x + k];
                    if (v != finderPatternA[k])
                        matchA = false;
                    if (v != finderPatternB[k])
                        matchB = false;
                    if (!matchA && !matchB)
                        break;
                }
                if (matchA)
                    score += 40;
                if (matchB)
                    score += 40;
            }
        }
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y + 11 <= size; ++y) {
                bool matchA = true;
                bool matchB = true;
                for (int k = 0; k < 11; ++k) {
                    const int v = cand[static_cast<size_t>(y + k) * size + x];
                    if (v != finderPatternA[k])
                        matchA = false;
                    if (v != finderPatternB[k])
                        matchB = false;
                    if (!matchA && !matchB)
                        break;
                }
                if (matchA)
                    score += 40;
                if (matchB)
                    score += 40;
            }
        }

        int dark = 0;
        for (size_t i = 0; i < cand.size(); ++i)
            dark += cand[i] ? 1 : 0;
        const int total = size * size;
        const int percent = (dark * 100) / total;
        const int deviation = percent > 50 ? percent - 50 : 50 - percent;
        score += (deviation / 5) * 10;

        if (bestMask < 0 || score < bestScore) {
            bestScore = score;
            bestMask = mask;
            bestGrid = cand;
        }
    }

    if (bestMask < 0)
        return false;

    m_version = version;
    m_size = size;
    m_mask = bestMask;
    m_codewords = codewords;
    m_modules.resize(static_cast<size_t>(size) * size);
    for (size_t i = 0; i < bestGrid.size(); ++i)
        m_modules[i] = bestGrid[i] ? 1 : 0;
    m_valid = true;
    return true;
}
