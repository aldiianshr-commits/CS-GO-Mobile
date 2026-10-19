#include "lzma_valve.h"
#include "../core/common.h"
extern "C" {
#include "LzmaDec.h"
}
#include <cstdlib>

namespace cs {

static void* lzAlloc(ISzAllocPtr, size_t s) { return malloc(s); }
static void lzFree(ISzAllocPtr, void* p) { free(p); }
static const ISzAlloc g_alloc = {lzAlloc, lzFree};

bool isValveLzma(const uint8_t* d, size_t n) {
    if (n < 17) return false;
    // id "LZMA" (urutan byte bisa terbalik di konsol)
    bool magic = (d[0] == 'L' && d[1] == 'Z' && d[2] == 'M' && d[3] == 'A') ||
                 (d[0] == 'A' && d[1] == 'M' && d[2] == 'Z' && d[3] == 'L');
    if (!magic) return false;
    uint32_t actual = rd32le(d + 4), lz = rd32le(d + 8);
    return actual > 0 && actual < 0x40000000u && lz > 0 && lz <= n - 17 + 64;
}

uint32_t valveLzmaActualSize(const uint8_t* d, size_t n) {
    return isValveLzma(d, n) ? rd32le(d + 4) : 0;
}

bool decompressRawLzma(const uint8_t* props, const uint8_t* stream, size_t streamSize, size_t outSize, std::vector<uint8_t>& out) {
    out.resize(outSize);
    SizeT destLen = outSize, srcLen = streamSize;
    ELzmaStatus status;
    SRes r = LzmaDecode(out.data(), &destLen, stream, &srcLen, props, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status, &g_alloc);
    if (r != SZ_OK || destLen != outSize) {
        LOGW("LZMA gagal: res=%d out=%zu/%zu", (int)r, (size_t)destLen, outSize);
        if (r != SZ_OK) return false;
        out.resize(destLen);
    }
    return true;
}

bool decompressValveLzma(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    if (!isValveLzma(d, n)) return false;
    uint32_t actual = rd32le(d + 4), lz = rd32le(d + 8);
    size_t avail = n - 17;
    if (lz > avail) lz = (uint32_t)avail;
    return decompressRawLzma(d + 12, d + 17, lz, actual, out);
}

} // namespace cs
