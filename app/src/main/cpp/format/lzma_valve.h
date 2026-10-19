// Dekompresi blok LZMA ber-header Valve ("LZMA" + actualSize + lzmaSize + props[5]),
// dipakai pada lump BSP, resource VTFX, dan file di dalam ZIP Xbox 360.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace cs {

// true jika data diawali header LZMA Valve yang valid
bool isValveLzma(const uint8_t* data, size_t size);
// ukuran hasil dekompresi menurut header (0 jika bukan)
uint32_t valveLzmaActualSize(const uint8_t* data, size_t size);
// dekompres; false jika gagal
bool decompressValveLzma(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
// LZMA mentah (props 5 byte + stream) dengan ukuran keluaran diketahui (ZIP metode 14)
bool decompressRawLzma(const uint8_t* props, const uint8_t* stream, size_t streamSize, size_t outSize, std::vector<uint8_t>& out);

} // namespace cs
