#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>
#include <array>
#include <cstdint>
#include <optional>

// Decoder for a Mac's Adaptive picture (RFB encoding 0x3f3): 8x8 tiles drawn as white, copies,
// one or two colours, JPEG-style DCT blocks, cached tiles, and partial updates that refine the
// last full update's coefficients. A C++ port of zDesk's decoder (MIT; see THIRD_PARTY_NOTICES.md,
// frontend/wasm/apple-adaptive/src/decoder.rs), which follows Apple's own viewer.
//
// It paints onto a QImage::Format_RGB32 framebuffer the caller owns (0xffRRGGBB). Each tile's
// coefficients outlive the rectangle that drew it, so the decoder must see every 0x3f3 rectangle.
class AppleAdaptive
{
public:
    static constexpr int Encoding = 0x3f3;

    AppleAdaptive();
    // A framebuffer of a new size: no tile coded. The tables and the cache outlive it, as the Mac's do.
    void resize(int width, int height);
    // Decodes one rectangle's body (the bytes after its u32 length) onto `frame`, which must have the
    // size given to resize(). Returns false with `error` set for a body that doesn't decode; what it
    // drew before failing stays. `drew` says whether pixels changed (the tables don't draw).
    bool decode(QImage &frame, int x, int y, int w, int h, const QByteArray &body, bool *drew, QString *error);

private:
    struct Block { std::array<int16_t, 64> y{}, cb{}, cr{}; };
    struct Coded {
        uint8_t precision = 0;
        std::array<int8_t, 64> y{};
        std::optional<int8_t> cr, cb;
    };
    struct Tile {
        Coded coded;
        uint32_t update = 0;
        bool hasCopy = false;
        size_t copyFrom = 0, copyAt = 0;
    };
    struct Tables { std::array<int32_t, 64> luminance{}, chrominance{}; };
    struct Rect { size_t x = 0, y = 0, w = 0, h = 0; };
    struct Refined {
        std::array<int8_t, 64> y{};
        std::array<int8_t, 15> cr{};
        std::array<int8_t, 20> cb{};
        std::array<int16_t, 64> crBlock{}, cbBlock{};
    };
    class Bits;
    struct Scan;

    bool full(const Rect &rect, const QByteArray &body, QString *error);
    bool partial(const Rect &rect, const QByteArray &body, QString *error);
    void readTile(const Scan &scan, std::array<uint32_t, 64> &tile) const;
    void writeTile(const Scan &scan, const std::array<uint32_t, 64> &tile);
    void copyInto(std::array<uint32_t, 64> &tile, ptrdiff_t at) const;
    void drawCached(uint16_t entry, std::array<uint32_t, 64> &tile);
    void cacheEntry(const Refined &entry);
    static void inverse(const Block &block, const Tables &tables, std::array<uint32_t, 64> &tile);

    size_t m_width = 0, m_height = 0, m_tilesAcross = 0;
    uint32_t *m_pixels = nullptr;   // the frame being decoded onto (valid during decode)
    size_t m_pixelCount = 0;
    std::vector<Tile> m_tiles;
    std::optional<Tables> m_tables;
    uint32_t m_updates = 0;
    std::vector<std::array<int8_t, 99>> m_cache;   // 64 Y + 15 Cr + 20 Cb, by index
    uint32_t m_cached = 0;
    uint16_t m_written = 0, m_drawn = 0;
};
