#include "vtf.h"
#include "lzma_valve.h"
#include "../core/common.h"

namespace cs {

// Nilai enum ImageFormat Source
enum {
    F_RGBA8888 = 0, F_ABGR8888 = 1, F_RGB888 = 2, F_BGR888 = 3, F_RGB565 = 4, F_I8 = 5, F_IA88 = 6,
    F_P8 = 7, F_A8 = 8, F_RGB888_BS = 9, F_BGR888_BS = 10, F_ARGB8888 = 11, F_BGRA8888 = 12,
    F_DXT1 = 13, F_DXT3 = 14, F_DXT5 = 15, F_BGRX8888 = 16, F_BGR565 = 17, F_BGRX5551 = 18,
    F_BGRA4444 = 19, F_DXT1_1BA = 20, F_BGRA5551 = 21, F_UV88 = 22, F_UVWQ8888 = 23,
    F_RGBA16F = 24, F_RGBA16 = 25, F_UVLX8888 = 26,
    F_LIN_BGRX8888 = 42, F_LIN_RGBA8888 = 43, F_LIN_ABGR8888 = 44, F_LIN_ARGB8888 = 45,
    F_LIN_BGRA8888 = 46, F_LIN_RGB888 = 47, F_LIN_BGR888 = 48, F_LIN_BGRX5551 = 49, F_LIN_I8 = 50,
    F_LIN_RGBA16 = 51, F_LE_BGRX8888 = 52, F_LE_BGRA8888 = 53
};

static int canonical(int f) {
    switch (f) {
        case F_LIN_BGRX8888: case F_LE_BGRX8888: return F_BGRX8888;
        case F_LIN_RGBA8888: return F_RGBA8888;
        case F_LIN_ABGR8888: return F_ABGR8888;
        case F_LIN_ARGB8888: return F_ARGB8888;
        case F_LIN_BGRA8888: case F_LE_BGRA8888: return F_BGRA8888;
        case F_LIN_RGB888: return F_RGB888;
        case F_LIN_BGR888: return F_BGR888;
        case F_LIN_BGRX5551: return F_BGRX5551;
        case F_LIN_I8: return F_I8;
        case F_LIN_RGBA16: return F_RGBA16;
        case F_RGB888_BS: return F_RGB888;
        case F_BGR888_BS: return F_BGR888;
        default: return f;
    }
}

static int bytesPerPixel(int f) {
    switch (f) {
        case F_RGBA8888: case F_ABGR8888: case F_ARGB8888: case F_BGRA8888: case F_BGRX8888:
        case F_UVWQ8888: case F_UVLX8888: return 4;
        case F_RGB888: case F_BGR888: return 3;
        case F_RGB565: case F_BGR565: case F_IA88: case F_BGRX5551: case F_BGRA4444: case F_BGRA5551: case F_UV88: return 2;
        case F_I8: case F_A8: case F_P8: return 1;
        case F_RGBA16F: case F_RGBA16: return 8;
        default: return 0;
    }
}

static bool isBC(int f, TexFmt& k) {
    if (f == F_DXT1 || f == F_DXT1_1BA) { k = TexFmt::BC1; return true; }
    if (f == F_DXT3) { k = TexFmt::BC2; return true; }
    if (f == F_DXT5) { k = TexFmt::BC3; return true; }
    return false;
}

size_t bcSize(TexFmt fmt, int w, int h) {
    size_t bw = (size_t)std::max(1, (w + 3) / 4), bh = (size_t)std::max(1, (h + 3) / 4);
    return bw * bh * (fmt == TexFmt::BC1 ? 8 : 16);
}

static size_t mipBytes(int f, int w, int h) {
    TexFmt k;
    if (isBC(f, k)) return bcSize(k, w, h);
    return (size_t)w * h * bytesPerPixel(f);
}

// ---------------------------------------------------------------- dekoder BC
static void color565(uint16_t c, uint8_t* o) {
    o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    o[2] = (uint8_t)((c & 31) * 255 / 31);
    o[3] = 255;
}

static void decodeColorBlock(const uint8_t* b, uint8_t out[16][4], bool allow3) {
    uint16_t c0 = rd16le(b), c1 = rd16le(b + 2);
    uint8_t pal[4][4];
    color565(c0, pal[0]);
    color565(c1, pal[1]);
    if (c0 > c1 || !allow3) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) { pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2); pal[3][i] = 0; }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    uint32_t idx = rd32le(b + 4);
    for (int p = 0; p < 16; ++p) memcpy(out[p], pal[(idx >> (2 * p)) & 3], 4);
}

void decodeBC(TexFmt fmt, const uint8_t* src, int w, int h, uint8_t* rgba) {
    int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    size_t bsz = fmt == TexFmt::BC1 ? 8 : 16;
    uint8_t px[16][4];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = src + ((size_t)by * bw + bx) * bsz;
            if (fmt == TexFmt::BC1) {
                decodeColorBlock(b, px, true);
            } else if (fmt == TexFmt::BC2) {
                decodeColorBlock(b + 8, px, false);
                for (int p = 0; p < 16; ++p) {
                    uint8_t a = (b[p / 2] >> ((p & 1) * 4)) & 15;
                    px[p][3] = (uint8_t)(a * 17);
                }
            } else {
                decodeColorBlock(b + 8, px, false);
                uint8_t a0 = b[0], a1 = b[1];
                uint8_t ap[8];
                ap[0] = a0; ap[1] = a1;
                if (a0 > a1) for (int i = 1; i < 7; ++i) ap[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
                else {
                    for (int i = 1; i < 5; ++i) ap[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
                    ap[6] = 0; ap[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= (uint64_t)b[2 + i] << (8 * i);
                for (int p = 0; p < 16; ++p) px[p][3] = ap[(bits >> (3 * p)) & 7];
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int X = bx * 4 + x, Y = by * 4 + y;
                    if (X < w && Y < h) memcpy(rgba + ((size_t)Y * w + X) * 4, px[y * 4 + x], 4);
                }
        }
}

static uint8_t halfToByte(uint16_t h) {
    uint32_t s = h >> 15, e = (h >> 10) & 31, m = h & 1023;
    float f;
    if (e == 0) f = m / 1024.0f / 16384.0f;
    else if (e == 31) f = 65504.f;
    else f = std::ldexp(1.0f + m / 1024.0f, (int)e - 15);
    if (s) f = 0;
    // tonemap sederhana untuk tekstur HDR (mis. skybox HDR CS:GO)
    f = f / (1.0f + f) * 2.0f;
    f = std::pow(std::min(f, 1.0f), 1.0f / 2.2f);
    return (uint8_t)std::lround(std::min(f, 1.0f) * 255.f);
}

static bool toRGBA(int f, const uint8_t* s, int w, int h, uint8_t* o, bool console) {
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i, o += 4) {
        switch (f) {
            case F_RGBA8888: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; s += 4; break;
            case F_ABGR8888: o[0] = s[3]; o[1] = s[2]; o[2] = s[1]; o[3] = s[0]; s += 4; break;
            case F_ARGB8888: o[0] = s[1]; o[1] = s[2]; o[2] = s[3]; o[3] = s[0]; s += 4; break;
            case F_BGRA8888: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; s += 4; break;
            case F_BGRX8888: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; s += 4; break;
            case F_UVWQ8888: case F_UVLX8888: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; s += 4; break;
            case F_RGB888: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; s += 3; break;
            case F_BGR888: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; s += 3; break;
            case F_RGB565: case F_BGR565: {
                uint16_t c = console ? rd16be(s) : rd16le(s);
                uint8_t a = (uint8_t)((c >> 11) * 255 / 31), b = (uint8_t)(((c >> 5) & 63) * 255 / 63), d = (uint8_t)((c & 31) * 255 / 31);
                if (f == F_RGB565) { o[0] = d; o[1] = b; o[2] = a; } else { o[0] = a; o[1] = b; o[2] = d; }
                o[3] = 255; s += 2; break;
            }
            case F_BGRX5551: case F_BGRA5551: {
                uint16_t c = console ? rd16be(s) : rd16le(s);
                o[2] = (uint8_t)((c & 31) * 255 / 31); o[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
                o[0] = (uint8_t)(((c >> 10) & 31) * 255 / 31); o[3] = (f == F_BGRA5551 && !(c >> 15)) ? 0 : 255;
                s += 2; break;
            }
            case F_BGRA4444: {
                uint16_t c = console ? rd16be(s) : rd16le(s);
                o[2] = (uint8_t)((c & 15) * 17); o[1] = (uint8_t)(((c >> 4) & 15) * 17);
                o[0] = (uint8_t)(((c >> 8) & 15) * 17); o[3] = (uint8_t)((c >> 12) * 17);
                s += 2; break;
            }
            case F_IA88: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; s += 2; break;
            case F_UV88: o[0] = s[0]; o[1] = s[1]; o[2] = 255; o[3] = 255; s += 2; break;
            case F_I8: case F_P8: o[0] = o[1] = o[2] = s[0]; o[3] = 255; s += 1; break;
            case F_A8: o[0] = o[1] = o[2] = 255; o[3] = s[0]; s += 1; break;
            case F_RGBA16F:
                for (int c = 0; c < 4; ++c) {
                    uint16_t hv = console ? rd16be(s + c * 2) : rd16le(s + c * 2);
                    o[c] = c == 3 ? 255 : halfToByte(hv);
                }
                s += 8; break;
            case F_RGBA16:
                for (int c = 0; c < 4; ++c) {
                    uint16_t v = console ? rd16be(s + c * 2) : rd16le(s + c * 2);
                    o[c] = (uint8_t)(v >> 8);
                }
                s += 8; break;
            default: return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- pemuatan
bool loadVtf(const std::vector<uint8_t>& file, int mipSkip, bool allowBC, TextureData& out, std::string& err) {
    out = TextureData();
    if (file.size() < 64) { err = "file tekstur terlalu kecil"; return false; }
    const uint8_t* d = file.data();
    int width, height, depth = 1, frames = 1, format, mipCount;
    uint32_t flags;
    size_t dataOff = 0;
    bool console = false;
    std::vector<uint8_t> lzmaBuf;
    const uint8_t* img = nullptr;
    size_t imgLen = 0;

    if (!memcmp(d, "VTFX", 4)) {
        console = true;
        uint32_t major = rd32be(d + 4);
        flags = rd32be(d + 16);
        width = rd16be(d + 20);
        height = rd16be(d + 22);
        depth = std::max(1, (int)rd16be(d + 24));
        frames = std::max(1, (int)rd16be(d + 26));
        uint8_t numRes = d[31];
        format = (int)rd32be(d + 48);
        bool ps3 = (major != 0x360);
        size_t rp = 60;
        std::vector<std::pair<uint32_t, int>> offsets; // offset, isImage
        for (int i = 0; i < numRes && rp + 8 <= file.size(); ++i, rp += 8) {
            const uint8_t* e = d + rp;
            bool noData = (e[3] & 0x02) != 0;
            uint32_t v = rd32be(e + 4);
            if (!noData) offsets.push_back({v, (e[0] == 0x30 && e[1] == 0 && e[2] == 0) ? 1 : 0});
        }
        int imgIdx = -1;
        for (size_t i = 0; i < offsets.size(); ++i) if (offsets[i].second) imgIdx = (int)i;
        if (imgIdx < 0) { err = "VTFX tanpa data gambar"; return false; }
        dataOff = offsets[imgIdx].first;
        size_t end = file.size();
        for (auto& o : offsets) if (o.first > dataOff && o.first < end) end = o.first;
        if (dataOff >= file.size()) { err = "offset VTFX rusak"; return false; }
        if (isValveLzma(d + dataOff, file.size() - dataOff)) {
            if (!decompressValveLzma(d + dataOff, file.size() - dataOff, lzmaBuf)) { err = "LZMA VTFX gagal"; return false; }
            img = lzmaBuf.data(); imgLen = lzmaBuf.size();
        } else {
            img = d + dataOff; imgLen = end - dataOff;
        }
        if (ps3) console = false; // PS3: data gambar tidak perlu dibalik (lihat dok VTFX)
    } else if (!memcmp(d, "VTF\0", 4)) {
        uint32_t minor = rd32le(d + 8), headerSize = rd32le(d + 12);
        width = rd16le(d + 16);
        height = rd16le(d + 18);
        flags = rd32le(d + 20);
        frames = std::max(1, (int)rd16le(d + 24));
        format = (int)rd32le(d + 52);
        int lowFmt = (int)rd32le(d + 57);
        int lowW = d[61], lowH = d[62];
        if (minor >= 2) depth = std::max(1, (int)rd16le(d + 63));
        if (minor >= 3) {
            uint32_t numRes = rd32le(d + 68);
            for (uint32_t i = 0; i < numRes && 80 + i * 8 + 8 <= file.size(); ++i) {
                const uint8_t* e = d + 80 + i * 8;
                if (e[0] == 0x30 && e[1] == 0 && e[2] == 0) dataOff = rd32le(e + 4);
            }
        } else {
            dataOff = headerSize + (lowFmt >= 0 ? mipBytes(lowFmt, lowW, lowH) : 0);
        }
        if (!dataOff || dataOff >= file.size()) { err = "VTF tanpa data gambar"; return false; }
        img = d + dataOff; imgLen = file.size() - dataOff;
    } else {
        err = "bukan VTF/VTFX";
        return false;
    }

    out.srcFormat = format;
    out.isConsole = console;
    format = canonical(format);
    TexFmt bck;
    bool bc = isBC(format, bck);
    if (!bc && bytesPerPixel(format) == 0) { err = "format tekstur " + std::to_string(out.srcFormat) + " tidak didukung"; return false; }
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) { err = "ukuran tekstur tidak valid"; return false; }

    int faces = (flags & 0x4000) ? 6 : 1;
    if (flags & 0x100) mipCount = 1; // NOMIP
    else { mipCount = 1; int m = std::max(width, height); while (m > 1) { m >>= 1; ++mipCount; } }
    out.hasAlpha = (flags & (0x1000 | 0x2000)) != 0;

    auto dimAt = [&](int m, int& w, int& h) { w = std::max(1, width >> m); h = std::max(1, height >> m); };
    auto sliceBytes = [&](int m) { int w, h; dimAt(m, w, h); return mipBytes(format, w, h); };
    // total semua mip; disimpan dari mip terkecil ke terbesar, tiap mip: frame x face x slice
    size_t total = 0;
    std::vector<size_t> mipOff(mipCount);
    for (int m = mipCount - 1; m >= 0; --m) {
        mipOff[m] = total;
        total += sliceBytes(m) * frames * faces * std::max(1, depth >> m);
    }
    bool anchorEnd = false;
    if (total > imgLen) {
        // data dipotong (mis. mip kecil tidak disimpan) — ambil mip besar dari ujung
        anchorEnd = true;
        LOGW("data tekstur %zu < %zu, menyesuaikan dari ujung", imgLen, total);
    }

    int skip = std::max(0, std::min(mipSkip, mipCount - 1));
    while (skip > 0) { int w, h; dimAt(skip, w, h); if (w >= 16 && h >= 16) break; --skip; }
    dimAt(skip, out.width, out.height);
    out.fmt = (bc && allowBC) ? bck : TexFmt::RGBA8;

    for (int m = skip; m < mipCount; ++m) {
        int w, h;
        dimAt(m, w, h);
        size_t sb = sliceBytes(m);
        size_t off = anchorEnd ? (imgLen - (total - mipOff[m])) : mipOff[m];
        if (anchorEnd && total - mipOff[m] > imgLen) break;
        if (off + sb > imgLen) break;
        TextureMip mip;
        mip.w = w; mip.h = h;
        std::vector<uint8_t> src(img + off, img + off + sb);
        if (bc && console) {
            for (size_t i = 0; i + 1 < src.size(); i += 2) std::swap(src[i], src[i + 1]);
        } else if (!bc && console && bytesPerPixel(format) == 4) {
            for (size_t i = 0; i + 3 < src.size(); i += 4) { std::swap(src[i], src[i + 3]); std::swap(src[i + 1], src[i + 2]); }
        }
        if (bc && allowBC) {
            mip.data.swap(src);
        } else {
            mip.data.resize((size_t)w * h * 4);
            if (bc) decodeBC(bck, src.data(), w, h, mip.data.data());
            else if (!toRGBA(format, src.data(), w, h, mip.data.data(), console)) { err = "konversi gagal"; return false; }
        }
        out.mips.push_back(std::move(mip));
    }
    if (out.mips.empty()) { err = "tidak ada mip yang terbaca"; return false; }
    return true;
}

} // namespace cs
