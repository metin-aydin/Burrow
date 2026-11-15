// C++ port of zDesk's Adaptive (0x3f3) decoder (MIT, Copyright (c) 2026 Andrew Chen;
// frontend/wasm/apple-adaptive/src/decoder.rs). The structure and the rules follow it line for line,
// since the Mac codes each update against what its viewer made of the last one.
#include "vnc/AppleAdaptive.h"

#include <algorithm>

namespace {

constexpr uint8_t kFull = 0, kPartial = 1, kTables = 2;
constexpr int kTablesLen = 129;
// How far past a body's end its streams may be read before the body is refused.
constexpr size_t kOverread = 500;
constexpr uint32_t kMark = 0x6d;
constexpr uint32_t kPartialMarks[3] = {0x6d, 0x76, 0x73};
constexpr uint16_t kCacheReadMax = 65000, kCacheWriteMax = 64999;
constexpr int kCachedY = 64, kCachedCr = 15, kCachedCb = 20;

// Pixels as QImage::Format_RGB32 keeps them: 0xffRRGGBB.
constexpr uint32_t kOpaque = 0xff000000u, kWhite = 0xffffffffu, kBlack = kOpaque;
// What a two-colour tile that reuses its colours draws before any were read: white and (254, 213, 181).
constexpr uint32_t kFirstPairA = 0xffffffffu, kFirstPairB = 0xfffed5b5u;

// JPEG's zigzag order, with sixteen entries past the end for a run that reads beyond it.
constexpr size_t kNatural[80] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20,
    13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59,
    52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
    63, 63, 63, 63, 63, 63,
};

constexpr uint8_t kAcLuminanceBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr uint8_t kAcLuminanceValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};

// JPEG's standard Huffman table for luminance AC coefficients (a refinement's chrominance uses it).
struct Huffman
{
    int32_t max[17];
    int32_t offset[17];
    Huffman()
    {
        std::fill(std::begin(max), std::end(max), -1);
        std::fill(std::begin(offset), std::end(offset), 0);
        int32_t code = 0, first = 0;
        for (int i = 0; i < 16; ++i) {
            const int len = i + 1;
            const int32_t count = kAcLuminanceBits[i];
            if (count > 0) {
                offset[len] = first - code;
                code += count;
                max[len] = code - 1;
                first += count;
            }
            code <<= 1;
        }
    }
};
const Huffman &huffman()
{
    static const Huffman h;
    return h;
}

// JPEG's YCbCr to RGB with the reference library's fixed-point constants.
uint32_t ycc(uint8_t yv, uint8_t cbv, uint8_t crv)
{
    constexpr int32_t half = 1 << 15;
    const int32_t cb = int32_t(cbv) - 128, cr = int32_t(crv) - 128, y = yv;
    const int32_t r = y + ((91881 * cr + half) >> 16);
    const int32_t g = y + ((-46802 * cr + -22554 * cb + half) >> 16);
    const int32_t b = y + ((116130 * cb + half) >> 16);
    auto clamp = [](int32_t v) { return uint32_t(std::clamp(v, 0, 255)); };
    return kOpaque | clamp(r) << 16 | clamp(g) << 8 | clamp(b);
}

// The JPEG reference library's integer inverse DCT (jidctint.c), with its output range limit.
std::array<uint8_t, 64> idct(const std::array<int16_t, 64> &coef, const std::array<int32_t, 64> &quant)
{
    constexpr int CONST_BITS = 13, PASS1_BITS = 2;
    constexpr int64_t F0_298 = 2446, F0_390 = 3196, F0_541 = 4433, F0_765 = 6270, F0_899 = 7373,
                      F1_175 = 9633, F1_501 = 12299, F1_847 = 15137, F1_961 = 16069, F2_053 = 16819,
                      F2_562 = 20995, F3_072 = 25172;
    auto descale = [](int64_t x, int n) { return (x + (int64_t(1) << (n - 1))) >> n; };
    auto butterfly = [&](const int64_t v[8], int64_t out[8]) {
        const int64_t z2 = v[2], z3 = v[6];
        const int64_t z1 = (z2 + z3) * F0_541;
        const int64_t tmp2 = z1 + z3 * -F1_847, tmp3 = z1 + z2 * F0_765;
        const int64_t tmp0 = (v[0] + v[4]) * (int64_t(1) << CONST_BITS);
        const int64_t tmp1 = (v[0] - v[4]) * (int64_t(1) << CONST_BITS);
        const int64_t tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3, tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
        int64_t t0 = v[7], t1 = v[5], t2 = v[3], t3 = v[1];
        int64_t a1 = t0 + t3, a2 = t1 + t2, a3 = t0 + t2, a4 = t1 + t3;
        const int64_t z5 = (a3 + a4) * F1_175;
        t0 *= F0_298;
        t1 *= F2_053;
        t2 *= F3_072;
        t3 *= F1_501;
        a1 *= -F0_899;
        a2 *= -F2_562;
        a3 = a3 * -F1_961 + z5;
        a4 = a4 * -F0_390 + z5;
        t0 += a1 + a3;
        t1 += a2 + a4;
        t2 += a2 + a3;
        t3 += a1 + a4;
        out[0] = tmp10 + t3; out[1] = tmp11 + t2; out[2] = tmp12 + t1; out[3] = tmp13 + t0;
        out[4] = tmp13 - t0; out[5] = tmp12 - t1; out[6] = tmp11 - t2; out[7] = tmp10 - t3;
    };
    auto limit = [](int64_t x) -> uint8_t {
        const uint32_t i = uint32_t(int32_t(x) & 1023);
        if (i < 128) return uint8_t(i + 128);
        if (i < 512) return 255;
        if (i < 896) return 0;
        return uint8_t(i - 896);
    };
    int32_t ws[64];
    for (int col = 0; col < 8; ++col) {
        auto at = [&](int row) { return int64_t(coef[row * 8 + col]) * int64_t(quant[row * 8 + col]); };
        bool acZero = true;
        for (int row = 1; row < 8 && acZero; ++row)
            acZero = coef[row * 8 + col] == 0;
        if (acZero) {
            const int32_t dc = int32_t(at(0) * (int64_t(1) << PASS1_BITS));
            for (int row = 0; row < 8; ++row)
                ws[row * 8 + col] = dc;
            continue;
        }
        int64_t v[8], out[8];
        for (int i = 0; i < 8; ++i)
            v[i] = at(i);
        butterfly(v, out);
        for (int row = 0; row < 8; ++row)
            ws[row * 8 + col] = int32_t(descale(out[row], CONST_BITS - PASS1_BITS));
    }
    std::array<uint8_t, 64> samples{};
    for (int row = 0; row < 8; ++row) {
        const int32_t *line = ws + row * 8;
        bool acZero = true;
        for (int i = 1; i < 8 && acZero; ++i)
            acZero = line[i] == 0;
        if (acZero) {
            std::fill_n(samples.begin() + row * 8, 8, limit(descale(line[0], PASS1_BITS + 3)));
            continue;
        }
        int64_t v[8], out[8];
        for (int i = 0; i < 8; ++i)
            v[i] = line[i];
        butterfly(v, out);
        for (int i = 0; i < 8; ++i)
            samples[row * 8 + i] = limit(descale(out[i], CONST_BITS + PASS1_BITS + 3));
    }
    return samples;
}

} // namespace

// A stream read most significant bit first; past its end it reads zeros.
class AppleAdaptive::Bits
{
public:
    Bits(const QByteArray &data, size_t at) : m_data(reinterpret_cast<const uint8_t *>(data.constData())), m_len(size_t(data.size())), m_next(at) {}
    uint32_t get(uint32_t n)   // at most 24
    {
        while (m_held < n) {
            const uint8_t byte = m_next < m_len ? m_data[m_next] : 0;
            ++m_next;
            m_window = ((m_window << 8) | byte) & 0xffffffffffffull;
            m_held += 8;
        }
        m_held -= n;
        return uint32_t((m_window >> m_held) & ((uint64_t(1) << n) - 1));
    }
    bool bit() { return get(1) == 1; }
    bool past() const { return m_next > m_len + kOverread; }

private:
    const uint8_t *m_data;
    size_t m_len, m_next;
    uint64_t m_window = 0;
    uint32_t m_held = 0;
};

// The tiles of a rectangle, in rows, left to right and top to bottom.
struct AppleAdaptive::Scan
{
    Rect rect;
    size_t dx = 0, dy = 0, index = 0, row = 0, across = 0;
    Scan(const Rect &r, size_t acr) : rect(r), across(acr)
    {
        index = (r.y >> 3) * acr + (r.x >> 3);
        row = index;
    }
    size_t x() const { return rect.x + dx; }
    size_t y() const { return rect.y + dy; }
    size_t at(size_t width) const { return y() * width + x(); }
    std::pair<size_t, size_t> clipped() const { return {std::min<size_t>(rect.w - dx, 8), std::min<size_t>(rect.h - dy, 8)}; }
    bool advance()   // true when the tile just done was the last
    {
        dx += 8;
        if (dx < rect.w) {
            ++index;
            return false;
        }
        dy += 8;
        if (dy >= rect.h)
            return true;
        dx = 0;
        row += across;
        index = row;
        return false;
    }
};

namespace {

// A repeat count: how many tiles a command covers.
template <typename B>
uint32_t tilesCovered(B &bits)
{
    if (!bits.bit())
        return 1;
    const uint32_t shortCount = bits.get(4);
    if (shortCount != 15)
        return shortCount + 2;
    const uint32_t first = bits.get(8);
    if (!(first & 0x80))
        return first + 17;
    const uint32_t second = bits.get(8);
    if (!(second & 0x80))
        return ((second << 7) | (first & 0x7f)) + 17;
    const uint32_t third = bits.get(8);
    return ((third << 14) | ((second & 0x7f) << 7) | (first & 0x7f)) + 17;
}

// A signed number: k one bits and a zero, then the magnitude's bits and a sign.
template <typename B>
bool signedNumber(B &bits, int16_t &out)
{
    uint32_t ones = 0;
    while (bits.bit()) {
        if (++ones == 39)
            return false;
    }
    uint32_t magnitude = 0;
    bool negative = false;
    switch (ones) {
    case 0:
        if (!bits.bit()) {
            out = 0;
            return true;
        }
        magnitude = 1;
        negative = bits.bit();
        break;
    case 1: magnitude = 2 + bits.get(1); negative = bits.bit(); break;
    case 2: magnitude = 4 + bits.get(2); negative = bits.bit(); break;
    default: magnitude = 8 * (ones - 2) + bits.get(3); negative = bits.bit(); break;
    }
    out = negative ? int16_t(-int16_t(magnitude)) : int16_t(magnitude);
    return true;
}

template <typename B>
uint8_t huffmanDecode(B &bits)
{
    const Huffman &h = huffman();
    int32_t code = 0;
    for (int len = 1; len <= 16; ++len) {
        code = (code << 1) | int32_t(bits.get(1));
        if (code <= h.max[len])
            return kAcLuminanceValues[code + h.offset[len]];
    }
    bits.get(1);
    return 0;
}

template <typename B>
void masked(B &data, std::array<uint32_t, 64> &tile, uint32_t set, uint32_t clear)
{
    const uint32_t rows = data.get(8);
    for (int row = 0; row < 8; ++row) {
        if (rows & (0x80u >> row)) {
            std::fill_n(tile.begin() + row * 8, 8, set);
            continue;
        }
        const uint32_t pixels = data.get(8);
        for (int col = 0; col < 8; ++col)
            tile[row * 8 + col] = (pixels & (0x80u >> col)) ? set : clear;
    }
}

template <typename B>
uint32_t colour(B &data)
{
    const uint8_t y = uint8_t(data.get(8));
    const uint8_t cb = uint8_t(data.get(6) << 2);
    const uint8_t cr = uint8_t(data.get(6) << 2);
    return ycc(y, cb, cr);
}

} // namespace

AppleAdaptive::AppleAdaptive() = default;

void AppleAdaptive::resize(int width, int height)
{
    m_width = size_t(qMax(0, width));
    m_height = size_t(qMax(0, height));
    m_tilesAcross = (m_width + 7) / 8;
    m_tiles.assign(m_tilesAcross * ((m_height + 7) / 8), Tile{});
}

bool AppleAdaptive::decode(QImage &frame, int x, int y, int w, int h, const QByteArray &body, bool *drew, QString *error)
{
    *drew = false;
    if (body.isEmpty()) {
        *error = QStringLiteral("an empty body");
        return false;
    }
    const uint8_t kind = uint8_t(body.at(0));
    if (kind == kTables) {
        if (body.size() != kTablesLen) {
            *error = QStringLiteral("quantization tables in %1 bytes").arg(body.size());
            return false;
        }
        Tables t;
        for (int i = 0; i < 64; ++i) {
            t.luminance[size_t(i)] = uint8_t(body.at(1 + i));
            t.chrominance[size_t(i)] = uint8_t(body.at(65 + i));
        }
        m_tables = t;
        return true;
    }
    if (kind != kFull && kind != kPartial) {
        *error = QStringLiteral("a body of kind %1").arg(kind);
        return false;
    }
    if (frame.format() != QImage::Format_RGB32 || size_t(frame.width()) != m_width || size_t(frame.height()) != m_height
        || size_t(frame.bytesPerLine()) != m_width * 4) {
        *error = QStringLiteral("a framebuffer of another size");
        return false;
    }
    const size_t x0 = size_t(x), y0 = size_t(y), w0 = size_t(w), h0 = size_t(h);
    if (x < 0 || y < 0 || w < 0 || h < 0 || !(x0 < m_width && x0 + w0 <= m_width && y0 < m_height && y0 + h0 <= m_height)) {
        *error = QStringLiteral("a %1x%2 rectangle at %3,%4 outside the framebuffer").arg(w).arg(h).arg(x).arg(y);
        return false;
    }
    m_pixels = reinterpret_cast<uint32_t *>(frame.bits());
    m_pixelCount = m_width * m_height;
    const Rect rect{x0, y0, w0, h0};
    const bool ok = kind == kFull ? full(rect, body, error) : partial(rect, body, error);
    m_pixels = nullptr;
    *drew = true;
    return ok;
}

void AppleAdaptive::readTile(const Scan &scan, std::array<uint32_t, 64> &tile) const
{
    tile.fill(kBlack);
    const auto [w, h] = scan.clipped();
    for (size_t row = 0; row < h; ++row)
        std::copy_n(m_pixels + (scan.y() + row) * m_width + scan.x(), w, tile.begin() + row * 8);
}

void AppleAdaptive::writeTile(const Scan &scan, const std::array<uint32_t, 64> &tile)
{
    const auto [w, h] = scan.clipped();
    for (size_t row = 0; row < h; ++row)
        std::copy_n(tile.begin() + row * 8, w, m_pixels + (scan.y() + row) * m_width + scan.x());
}

// Eight rows of eight pixels from `at`, the framebuffer read as one run of rows.
void AppleAdaptive::copyInto(std::array<uint32_t, 64> &tile, ptrdiff_t at) const
{
    for (int row = 0; row < 8; ++row) {
        const ptrdiff_t start = at + ptrdiff_t(row) * ptrdiff_t(m_width);
        if (start < 0)
            continue;
        for (int col = 0; col < 8; ++col) {
            const size_t i = size_t(start) + size_t(col);
            if (i < m_pixelCount)
                tile[size_t(row * 8 + col)] = m_pixels[i];
        }
    }
}

// A block with only its DC is one sample throughout; its value through the reference transform.
static bool flatSample(const std::array<int16_t, 64> &coef, int32_t q0, uint8_t &out)
{
    for (size_t i = 1; i < 64; ++i)
        if (coef[i] != 0)
            return false;
    std::array<int16_t, 64> dcOnly{};
    dcOnly[0] = coef[0];
    std::array<int32_t, 64> quant{};
    quant[0] = q0;
    out = idct(dcOnly, quant)[0];
    return true;
}

void AppleAdaptive::inverse(const Block &block, const Tables &tables, std::array<uint32_t, 64> &tile)
{
    const auto y = idct(block.y, tables.luminance);
    uint8_t cbFlat = 0, crFlat = 0;
    // A full update sends chrominance as DC alone: then the colour terms are the same for the whole
    // tile and only Y varies (same arithmetic as ycc(), hoisted).
    if (flatSample(block.cb, tables.chrominance[0], cbFlat) && flatSample(block.cr, tables.chrominance[0], crFlat)) {
        constexpr int32_t half = 1 << 15;
        const int32_t cb = int32_t(cbFlat) - 128, cr = int32_t(crFlat) - 128;
        const int32_t dr = (91881 * cr + half) >> 16;
        const int32_t dg = (-46802 * cr + -22554 * cb + half) >> 16;
        const int32_t db = (116130 * cb + half) >> 16;
        for (size_t i = 0; i < 64; ++i) {
            const int32_t yv = y[i];
            tile[i] = kOpaque | uint32_t(std::clamp(yv + dr, 0, 255)) << 16 | uint32_t(std::clamp(yv + dg, 0, 255)) << 8
                | uint32_t(std::clamp(yv + db, 0, 255));
        }
        return;
    }
    const auto cb = idct(block.cb, tables.chrominance);
    const auto cr = idct(block.cr, tables.chrominance);
    for (size_t i = 0; i < 64; ++i)
        tile[i] = ycc(y[i], cb[i], cr[i]);
}

void AppleAdaptive::drawCached(uint16_t entry, std::array<uint32_t, 64> &tile)
{
    if (entry == 0 || entry > kCacheReadMax || uint32_t(entry) > m_cached || m_cache.size() <= entry || !m_tables)
        return;
    const auto &cached = m_cache[entry];
    Block block;
    for (int k = 0; k < kCachedY; ++k)
        block.y[kNatural[k]] = cached[size_t(k)];
    for (int k = 0; k < kCachedCr; ++k)
        block.cr[kNatural[k]] = cached[size_t(kCachedY + k)];
    for (int k = 0; k < kCachedCb; ++k)
        block.cb[kNatural[k]] = cached[size_t(kCachedY + kCachedCr + k)];
    inverse(block, *m_tables, tile);
    m_drawn = entry;
}

void AppleAdaptive::cacheEntry(const Refined &entry)
{
    if (m_cache.empty())
        m_cache.assign(size_t(kCacheReadMax) + 1, {});
    ++m_cached;
    m_written = m_written == kCacheWriteMax ? 1 : uint16_t(m_written + 1);
    auto &slot = m_cache[m_written];
    std::copy(entry.y.begin(), entry.y.end(), slot.begin());
    std::copy(entry.cr.begin(), entry.cr.end(), slot.begin() + kCachedY);
    std::copy(entry.cb.begin(), entry.cb.end(), slot.begin() + kCachedY + kCachedCr);
}

bool AppleAdaptive::full(const Rect &rect, const QByteArray &body, QString *error)
{
    if (body.size() < 6) {
        *error = QStringLiteral("a full update under its header");
        return false;
    }
    if (!m_tables) {
        *error = QStringLiteral("a picture before the quantization tables");
        return false;
    }
    const Tables tables = *m_tables;
    const uint8_t precisions[2] = {uint8_t(body.at(1)), uint8_t(body.at(2))};
    const size_t offset = (size_t(uint8_t(body.at(3))) << 16) | (size_t(uint8_t(body.at(4))) << 8) | size_t(uint8_t(body.at(5)));
    if (offset > size_t(body.size())) {
        *error = QStringLiteral("a data stream past the body");
        return false;
    }
    Bits commands(body, 6), data(body, offset);
    commands.get(1);
    ++m_updates;

    Scan scan(rect, m_tilesAcross);
    uint32_t single = kBlack;
    uint32_t pairA = kFirstPairA, pairB = kFirstPairB;
    Block previous;
    std::optional<size_t> lastCoded;
    size_t before = scan.at(m_width);
    std::array<uint32_t, 64> drawn;
    for (;;) {
        if (commands.past()) {
            *error = QStringLiteral("an update that ran past its body");
            return false;
        }
        const uint32_t command = commands.get(3);
        const uint32_t covered = tilesCovered(commands);
        for (uint32_t n = 0; n < covered; ++n) {
            const size_t index = scan.index;
            const size_t here = scan.at(m_width);
            if (index >= m_tiles.size()) {
                *error = QStringLiteral("a tile outside the framebuffer");
                return false;
            }
            m_tiles[index].update = m_updates;
            m_tiles[index].hasCopy = false;
            // Copies and cache draws can leave pixels as they were; the others draw all 64.
            if (command == 1 || command == 2 || command >= 6)
                readTile(scan, drawn);
            switch (command) {
            case 0:
                drawn.fill(kWhite);
                break;
            case 1:
                if (index >= 1) {
                    m_tiles[index].hasCopy = true;
                    m_tiles[index].copyFrom = index - 1;
                    m_tiles[index].copyAt = before;
                }
                copyInto(drawn, ptrdiff_t(before));
                break;
            case 2: {
                const ptrdiff_t above = ptrdiff_t(here) - 8 * ptrdiff_t(m_width);
                if (index >= m_tilesAcross) {
                    m_tiles[index].hasCopy = true;
                    m_tiles[index].copyFrom = index - m_tilesAcross;
                    m_tiles[index].copyAt = size_t(std::max<ptrdiff_t>(above, 0));
                }
                copyInto(drawn, above);
                break;
            }
            case 3:
                masked(data, drawn, kWhite, kBlack);
                break;
            case 4: {
                const bool two = data.bit();
                const bool reuse = data.bit();
                if (two) {
                    if (!reuse) {
                        pairA = colour(data);
                        pairB = colour(data);
                    }
                    masked(data, drawn, pairA, pairB);
                } else {
                    if (!reuse)
                        single = colour(data);
                    drawn.fill(single);
                }
                break;
            }
            case 5: {
                // A DCT tile: copy bit, chroma-keep and precision bits, chroma DCs, Y DC, Y ACs.
                Block block;
                enum { Coded_, Last_, Nothing_ } saved = Nothing_;
                Coded coded;
                if (data.bit()) {
                    if (lastCoded) {
                        block.y = previous.y;
                        block.cb[0] = previous.cb[0];
                        block.cr[0] = previous.cr[0];
                        saved = Last_;
                    }
                } else {
                    const bool keep = data.bit();
                    const uint8_t precision = data.bit() ? precisions[1] : precisions[0];
                    int16_t sn = 0;
                    if (keep) {
                        block.cb[0] = previous.cb[0];
                        block.cr[0] = previous.cr[0];
                    } else {
                        if (!signedNumber(data, sn))
                            goto badCoefficients;
                        block.cb[0] = int16_t(int16_t(int16_t(previous.cb[0] / 2) - sn) * 2);
                        if (!signedNumber(data, sn))
                            goto badCoefficients;
                        block.cr[0] = int16_t(int16_t(int16_t(previous.cr[0] / 2) - sn) * 2);
                    }
                    if (!signedNumber(data, sn))
                        goto badCoefficients;
                    block.y[0] = int16_t(previous.y[0] - sn);
                    const bool fine = precision >= 15;
                    size_t k = 1;
                    bool keepRecord = true;
                    while (k < 64) {
                        const bool early = k < 6;
                        const bool below = uint8_t(k) < precision;
                        const int shift = fine ? (below ? 1 : 3) : (below ? 3 : 4);
                        if (data.bit()) {
                            uint32_t ones = 0;
                            bool stop = false;
                            while (data.bit()) {
                                ++ones;
                                if (early && ones == 33)
                                    goto badCoefficients;
                                if (!early && ones == 19) {
                                    keepRecord = false;   // the viewer keeps what it has
                                    stop = true;
                                    break;
                                }
                            }
                            if (stop)
                                break;
                            int32_t magnitude;
                            if (early)
                                magnitude = ones < 4 ? 2 + 4 * int32_t(ones) + int32_t(data.get(2))
                                                     : 18 + 8 * (int32_t(ones) - 4) + int32_t(data.get(3));
                            else
                                magnitude = ones == 0 ? 2 + int32_t(data.get(1))
                                          : ones == 1 ? 4 + int32_t(data.get(2))
                                                      : 8 * (int32_t(ones) - 1) + int32_t(data.get(3));
                            const int32_t value = data.bit() ? -magnitude : magnitude;
                            block.y[kNatural[k]] = int16_t(value * (1 << shift));
                            ++k;
                            continue;
                        }
                        const int16_t unit = int16_t(1 << shift);
                        switch (data.get(2)) {
                        case 0b00: ++k; break;
                        case 0b10: block.y[kNatural[k]] = unit; ++k; break;
                        case 0b11: block.y[kNatural[k]] = int16_t(-unit); ++k; break;
                        default: {
                            if (!data.bit()) {
                                k = 64;   // end of block
                                break;
                            }
                            const uint32_t r = data.get(2);
                            uint32_t run = r + 3;
                            if (r == 3) {
                                for (;;) {
                                    const uint32_t more = data.get(3);
                                    run += more;
                                    if (more != 7 || k + (run & 0xff) >= 64)
                                        break;
                                }
                                if (k + (run & 0xff) > 63)
                                    goto badCoefficients;
                                k += run & 0xff;
                            } else {
                                k += run;
                            }
                        }
                        }
                    }
                    if (keepRecord) {
                        coded.precision = precision;
                        for (size_t i = 0; i < 64; ++i)
                            coded.y[i] = int8_t(block.y[kNatural[i]]);
                        coded.cr = int8_t(block.cr[0]);
                        coded.cb = int8_t(block.cb[0]);
                        saved = Coded_;
                    }
                }
                if (saved == Coded_)
                    m_tiles[index].coded = coded;
                else if (saved == Last_)
                    m_tiles[index] = m_tiles[*lastCoded];
                previous = block;
                lastCoded = index;
                inverse(block, tables, drawn);
                break;
            }
            case 6:
                drawCached(uint16_t(data.get(16)), drawn);
                break;
            default:
                drawCached(uint16_t(m_drawn + 1), drawn);
                break;
            }
            writeTile(scan, drawn);
            before = here;
            if (scan.advance()) {
                if (commands.get(8) != kMark || data.get(8) != kMark) {
                    *error = QStringLiteral("a full update that didn't end on its mark");
                    return false;
                }
                return true;
            }
        }
        if (data.past()) {
            *error = QStringLiteral("an update that ran past its body");
            return false;
        }
    }
badCoefficients:
    *error = QStringLiteral("a tile's coefficients that don't read");
    return false;
}

bool AppleAdaptive::partial(const Rect &rect, const QByteArray &body, QString *error)
{
    if (body.size() < 3) {
        *error = QStringLiteral("a partial update under its header");
        return false;
    }
    if (!m_tables) {
        *error = QStringLiteral("a picture before the quantization tables");
        return false;
    }
    const Tables tables = *m_tables;
    const uint8_t crCount = std::min<uint8_t>(uint8_t(body.at(1)), 64);
    const uint8_t cbCount = std::min<uint8_t>(uint8_t(body.at(2)), 64);
    Bits bits(body, 3);
    Scan scan(rect, m_tilesAcross);
    std::array<uint32_t, 64> drawn;
    for (;;) {
        const size_t index = scan.index;
        if (index >= m_tiles.size()) {
            *error = QStringLiteral("a tile outside the framebuffer");
            return false;
        }
        const Tile tile = m_tiles[index];
        switch (bits.get(2)) {
        case 0:
            break;
        case 1: {
            // The bits the full update left out of the tile's Y, and its chrominance as JPEG codes it.
            const Coded &coded = tile.coded;
            const size_t precision = coded.precision;
            const size_t count = size_t(bits.get(6)) + 1;
            Refined entry;
            entry.y[0] = coded.y[0];
            auto further = [&](int8_t c, uint32_t n) {
                const int8_t more = int8_t(bits.get(n));
                return int8_t(c + (c < 0 ? -more : more));
            };
            int16_t sn = 0;
            const size_t below = std::min(count, precision);
            for (size_t k = 1; k < below; ++k) {
                const int8_t c = coded.y[k];
                if (precision < 15) {
                    if (c == 0) {
                        if (!signedNumber(bits, sn)) { *error = QStringLiteral("a refinement that doesn't read"); return false; }
                        entry.y[k] = int8_t(sn);
                    } else {
                        entry.y[k] = further(c, 3);
                    }
                } else if (c == 0) {
                    entry.y[k] = !bits.bit() ? 0 : bits.bit() ? -1 : 1;
                } else {
                    entry.y[k] = further(c, 1);
                }
            }
            for (size_t k = std::max<size_t>(precision, 1); k < std::min<size_t>(count, 64); ++k) {
                const int8_t c = coded.y[k];
                if (c == 0) {
                    if (!signedNumber(bits, sn)) { *error = QStringLiteral("a refinement that doesn't read"); return false; }
                    entry.y[k] = int8_t(sn);
                } else {
                    entry.y[k] = further(c, precision < 15 ? 4 : 3);
                }
            }
            auto chroma = [&](std::optional<int8_t> dc, uint8_t n, std::array<int16_t, 64> &blk) {
                if (!dc)
                    return false;
                blk.fill(0);
                const bool b = bits.bit();
                if (*dc == 0)
                    blk[0] = !b ? 0 : bits.bit() ? -1 : 1;
                else
                    blk[0] = int8_t(*dc + (*dc < 0 ? -int8_t(b) : int8_t(b)));
                size_t k = 0;
                while (k < n) {
                    const uint8_t symbol = huffmanDecode(bits);
                    const size_t run = symbol >> 4;
                    const uint32_t size = symbol & 15;
                    if (size == 0) {
                        if (run != 15)
                            break;
                        k += 15;
                    } else {
                        k += run;
                        int32_t value = int32_t(bits.get(size));
                        if (value < (1 << (size - 1)))
                            value += 1 - (1 << size);
                        if (k + 1 < 80)
                            blk[kNatural[k + 1]] = int16_t(value);
                    }
                    ++k;
                }
                return true;
            };
            if (!chroma(coded.cr, crCount, entry.crBlock) || !chroma(coded.cb, cbCount, entry.cbBlock)) {
                *error = QStringLiteral("a refinement of a tile the last full update didn't code");
                return false;
            }
            for (int k = 0; k < kCachedCr; ++k)
                entry.cr[size_t(k)] = int8_t(entry.crBlock[kNatural[k]]);
            for (int k = 0; k < kCachedCb; ++k)
                entry.cb[size_t(k)] = int8_t(entry.cbBlock[kNatural[k]]);
            Block block;
            for (size_t k = 0; k < 64; ++k)
                block.y[kNatural[k]] = entry.y[k];
            block.cr = entry.crBlock;
            block.cb = entry.cbBlock;
            readTile(scan, drawn);
            inverse(block, tables, drawn);
            cacheEntry(entry);
            writeTile(scan, drawn);
            break;
        }
        case 2:
            // The copy the full update made, while the tile it copied is still that update's.
            if (tile.hasCopy && tile.copyFrom < m_tiles.size() && m_tiles[tile.copyFrom].update == tile.update) {
                readTile(scan, drawn);
                copyInto(drawn, ptrdiff_t(tile.copyAt));
                writeTile(scan, drawn);
            }
            break;
        default: {
            const uint16_t entry = bits.bit() ? uint16_t(m_drawn + 1) : uint16_t(bits.get(16));
            readTile(scan, drawn);
            drawCached(entry, drawn);
            writeTile(scan, drawn);
            break;
        }
        }
        if (scan.advance()) {
            for (uint32_t want : kPartialMarks)
                if (bits.get(8) != want) {
                    *error = QStringLiteral("a partial update that didn't end on its mark");
                    return false;
                }
            return true;
        }
        if (bits.past()) {
            *error = QStringLiteral("an update that ran past its body");
            return false;
        }
    }
}
