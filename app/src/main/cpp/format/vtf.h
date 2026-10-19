// Pembaca tekstur Valve: VTF (PC, v7.0-7.5) dan VTFX (Xbox 360 / PS3).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cs {

enum class TexFmt { RGBA8, BC1, BC2, BC3 };

struct TextureMip {
    int w = 0, h = 0;
    std::vector<uint8_t> data;
};

struct TextureData {
    int width = 0, height = 0;
    TexFmt fmt = TexFmt::RGBA8;
    bool hasAlpha = false;
    bool isConsole = false;
    int srcFormat = -1;
    std::vector<TextureMip> mips; // mips[0] = terbesar
};

// mipSkip: jumlah mip besar yang dilewati (kualitas tekstur).
// allowBC: true jika GPU mendukung BCn — data DXT tetap terkompresi; jika false didekode ke RGBA8.
bool loadVtf(const std::vector<uint8_t>& file, int mipSkip, bool allowBC, TextureData& out, std::string& err);

// Dekoder DXT ke RGBA8 (juga dipakai untuk uji)
void decodeBC(TexFmt fmt, const uint8_t* src, int w, int h, uint8_t* rgbaOut);
size_t bcSize(TexFmt fmt, int w, int h);

} // namespace cs
