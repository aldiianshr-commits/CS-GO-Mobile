// Uji host untuk parser format CS:GO Xbox 360 (data sintetis, big-endian + LZMA)
#include "../app/src/main/cpp/core/common.h"
#include "../app/src/main/cpp/fs/vfs.h"
#include "../app/src/main/cpp/format/bsp.h"
#include "../app/src/main/cpp/format/vtf.h"
#include "../app/src/main/cpp/format/vmt.h"
#include "../app/src/main/cpp/format/lzma_valve.h"
#include "../app/src/main/cpp/game/world.h"
#include "../app/src/main/cpp/game/collision.h"
#include "../app/src/main/cpp/game/player.h"
extern "C" {
#include "LzmaEnc.h"
}
#include <cstdio>
#include <cstdlib>
#include <array>

using namespace cs;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "GAGAL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

struct W {
    std::vector<uint8_t> d;
    bool be;
    explicit W(bool bigEndian) : be(bigEndian) {}
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { if (be) { u8(v >> 8); u8(v & 255); } else { u8(v & 255); u8(v >> 8); } }
    void u32(uint32_t v) { if (be) { u16(v >> 16); u16(v & 0xffff); } else { u16(v & 0xffff); u16(v >> 16); } }
    void i32(int32_t v) { u32((uint32_t)v); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void f32(float f) { uint32_t u; memcpy(&u, &f, 4); u32(u); }
    void bytes(const void* p, size_t n) { d.insert(d.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    void str(const char* s) { bytes(s, strlen(s) + 1); }
};

static void* SzAlloc(ISzAllocPtr, size_t s) { return malloc(s); }
static void SzFree(ISzAllocPtr, void* p) { free(p); }
static const ISzAlloc g_al = {SzAlloc, SzFree};

// kompres ke format header LZMA Valve
static std::vector<uint8_t> valveLzma(const std::vector<uint8_t>& src) {
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.dictSize = 1 << 16;
    std::vector<uint8_t> out(src.size() + src.size() / 2 + 1024);
    SizeT outLen = out.size() - 17, propsSize = 5;
    uint8_t pr[5];
    SRes r = LzmaEncode(out.data() + 17, &outLen, src.data(), src.size(), &props, pr, &propsSize, 0, nullptr, &g_al, &g_al);
    if (r != SZ_OK) { fprintf(stderr, "LzmaEncode gagal\n"); exit(2); }
    memcpy(out.data(), "LZMA", 4);
    uint32_t a = (uint32_t)src.size(), l = (uint32_t)outLen;
    memcpy(out.data() + 4, &a, 4);
    memcpy(out.data() + 8, &l, 4);
    memcpy(out.data() + 12, pr, 5);
    out.resize(17 + outLen);
    return out;
}

// ------------------------------------------------------------------ BSP sintetis
static std::vector<uint8_t> makeBsp(bool be) {
    std::vector<std::vector<uint8_t>> lumps(64);
    std::vector<int> fourcc(64, 0), ver(64, 0);
    auto plane = [&](W& w, float x, float y, float z, float dist, int type) { w.f32(x); w.f32(y); w.f32(z); w.f32(dist); w.i32(type); };
    { W w(be); plane(w, 0, 0, 1, 0, 2); plane(w, 1, 0, 0, 512, 0); plane(w, -1, 0, 0, 512, 0);
      plane(w, 0, 1, 0, 512, 1); plane(w, 0, -1, 0, 512, 1); plane(w, 0, 0, -1, 64, 2);
      lumps[LUMP_PLANES] = w.d; }
    { W w(be); float v[4][3] = {{-512, -512, 0}, {512, -512, 0}, {512, 512, 0}, {-512, 512, 0}};
      for (auto& p : v) { w.f32(p[0]); w.f32(p[1]); w.f32(p[2]); } lumps[LUMP_VERTEXES] = w.d; }
    { W w(be); uint16_t e[5][2] = {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}}; for (auto& x : e) { w.u16(x[0]); w.u16(x[1]); } lumps[LUMP_EDGES] = w.d; }
    { W w(be); for (int i = 1; i <= 4; ++i) w.i32(i); lumps[LUMP_SURFEDGES] = w.d; }
    { W w(be); w.f32(0.5f); w.f32(0.5f); w.f32(0.5f); w.i32(0); w.i32(256); w.i32(256); w.i32(256); w.i32(256); lumps[LUMP_TEXDATA] = w.d; }
    { W w(false); w.str("DEV/FLOOR"); lumps[LUMP_TEXDATA_STRING_DATA] = w.d; }
    { W w(be); w.i32(0); lumps[LUMP_TEXDATA_STRING_TABLE] = w.d; }
    { W w(be); float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 1.f / 16, 0, 0, 32, 0, 1.f / 16, 0, 32};
      for (float f : t) w.f32(f); w.i32(0); w.i32(0); lumps[LUMP_TEXINFO] = w.d; }
    { W w(be); w.u16(0); w.u8(0); w.u8(0); w.i32(0); w.i16(4); w.i16(0); w.i16(-1); w.i16(-1);
      w.u8(0); w.u8(255); w.u8(255); w.u8(255); w.i32(0); w.f32(1024 * 1024);
      w.i32(0); w.i32(0); w.i32(64); w.i32(64); w.i32(0); w.u16(0); w.u16(0); w.u32(0);
      lumps[LUMP_FACES] = w.d; }
    { W w(false); for (int i = 0; i < 65 * 65; ++i) { w.u8(128); w.u8(64); w.u8(32); w.u8(0); } lumps[LUMP_LIGHTING] = w.d; }
    { W w(be); for (float f : {-512.f, -512.f, -64.f, 512.f, 512.f, 0.f, 0.f, 0.f, 0.f}) w.f32(f); w.i32(0); w.i32(0); w.i32(1); lumps[LUMP_MODELS] = w.d; }
    { W w(be); w.i32(0); w.i32(-1); w.i32(-2); for (int i = 0; i < 6; ++i) w.i16(0); w.u16(0); w.u16(1); w.i16(0); w.i16(0); lumps[LUMP_NODES] = w.d; }
    { W w(be);
      auto leaf = [&](int contents, int fb, int nb) { w.i32(contents); w.i16(0); w.i16(0); for (int i = 0; i < 6; ++i) w.i16(0);
                                                       w.u16(0); w.u16(0); w.u16((uint16_t)fb); w.u16((uint16_t)nb); w.i16(-1); w.i16(0); };
      leaf(0, 0, 0); leaf(1, 0, 1); lumps[LUMP_LEAFS] = w.d; ver[LUMP_LEAFS] = 1; }
    { W w(be); w.u16(0); lumps[LUMP_LEAFBRUSHES] = w.d; }
    { W w(be); w.i32(0); w.i32(6); w.i32(1); lumps[LUMP_BRUSHES] = w.d; }
    { W w(be); for (int p : {1, 2, 3, 4, 0, 5}) { w.u16((uint16_t)p); w.i16(0); w.i16(-1); w.u8(0); w.u8(0); } lumps[LUMP_BRUSHSIDES] = w.d; }
    { std::string e = "{\n\"classname\" \"worldspawn\"\n\"skyname\" \"sky_test\"\n}\n{\n\"classname\" \"info_player_terrorist\"\n\"origin\" \"0 0 100\"\n\"angles\" \"0 90 0\"\n}\n";
      lumps[LUMP_ENTITIES].assign(e.begin(), e.end()); lumps[LUMP_ENTITIES].push_back(0); }

    // lump plane & face dikompres seperti di Xbox 360
    for (int li : {LUMP_PLANES, LUMP_FACES}) {
        fourcc[li] = (int)lumps[li].size();
        lumps[li] = valveLzma(lumps[li]);
    }
    W h(be);
    h.bytes(be ? "PSBV" : "VBSP", 4);
    h.i32(21);
    size_t ofs = 1036;
    std::vector<uint8_t> body;
    std::vector<std::array<int, 4>> table(64);
    for (int i = 0; i < 64; ++i) {
        while ((ofs + body.size()) % 4) body.push_back(0);
        table[i] = {lumps[i].empty() ? 0 : (int)(ofs + body.size()), (int)lumps[i].size(), ver[i], fourcc[i]};
        body.insert(body.end(), lumps[i].begin(), lumps[i].end());
    }
    for (auto& t : table) for (int v : t) h.i32(v);
    h.i32(1);
    h.d.insert(h.d.end(), body.begin(), body.end());
    return h.d;
}

// ------------------------------------------------------------------ VTFX sintetis (DXT1 8x8 merah)
static std::vector<uint8_t> makeVtfx(bool lzma) {
    // blok DXT1 LE: c0 = merah (0xF800), c1 = biru (0x001F), indeks 0
    uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    std::vector<uint8_t> data;
    // mip kecil -> besar: 1x1, 2x2, 4x4 (masing2 1 blok), 8x8 (4 blok)
    for (int i = 0; i < 3 + 4; ++i) data.insert(data.end(), blk, blk + 8);
    for (size_t i = 0; i + 1 < data.size(); i += 2) std::swap(data[i], data[i + 1]); // big-endian 360
    if (lzma) data = valveLzma(data);
    W w(true);
    w.bytes("VTFX", 4); w.i32(0x360); w.i32(8); w.i32(68); w.u32(0);
    w.u16(8); w.u16(8); w.u16(1); w.u16(1); w.u16(0); w.u8(0); w.u8(1);
    w.f32(1); w.f32(1); w.f32(1); w.f32(1); w.i32(13); w.u32(0); w.u32(lzma ? (uint32_t)data.size() : 0);
    CHECK(w.d.size() == 60);
    w.u8(0x30); w.u8(0); w.u8(0); w.u8(0); w.u32(68);
    w.bytes(data.data(), data.size());
    return w.d;
}

// ------------------------------------------------------------------ ZIP store
static std::vector<uint8_t> makeZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    W w(false), cd(false);
    for (auto& f : files) {
        uint32_t lho = (uint32_t)w.d.size();
        w.u32(0x04034b50); w.u16(10); w.u16(0); w.u16(0); w.u16(0); w.u16(0); w.u32(0);
        w.u32((uint32_t)f.second.size()); w.u32((uint32_t)f.second.size()); w.u16((uint16_t)f.first.size()); w.u16(0);
        w.bytes(f.first.data(), f.first.size());
        w.bytes(f.second.data(), f.second.size());
        cd.u32(0x02014b50); cd.u16(20); cd.u16(10); cd.u16(0); cd.u16(0); cd.u16(0); cd.u16(0); cd.u32(0);
        cd.u32((uint32_t)f.second.size()); cd.u32((uint32_t)f.second.size()); cd.u16((uint16_t)f.first.size());
        cd.u16(0); cd.u16(0); cd.u16(0); cd.u16(0); cd.u32(0); cd.u32(lho);
        cd.bytes(f.first.data(), f.first.size());
    }
    uint32_t cdo = (uint32_t)w.d.size();
    w.bytes(cd.d.data(), cd.d.size());
    w.u32(0x06054b50); w.u16(0); w.u16(0); w.u16((uint16_t)files.size()); w.u16((uint16_t)files.size());
    w.u32((uint32_t)cd.d.size()); w.u32(cdo); w.u16(0);
    return w.d;
}

static std::vector<uint8_t> S(const std::string& s) { return {s.begin(), s.end()}; }

// ------------------------------------------------------------------ STFS sintetis (female)
static std::vector<uint8_t> makeStfs() {
    std::vector<uint8_t> f(0x8000, 0);
    memcpy(&f[0], "LIVE", 4);
    auto be32 = [&](size_t o, uint32_t v) { f[o] = v >> 24; f[o + 1] = v >> 16; f[o + 2] = v >> 8; f[o + 3] = v; };
    be32(0x340, 0x971);
    be32(0x344, 0xD0000);
    f[0x379] = 0x24; f[0x37B] = 1; f[0x37C] = 1; f[0x37D] = 0; f[0x37E] = 0; f[0x37F] = 0; f[0x380] = 0;
    be32(0x395, 8);
    auto blockAddr = [](uint32_t b) { return 0x1000 + (((b + 0xAA) / 0xAA) + b) * 0x1000; };
    auto entry = [&](int i, const char* name, uint8_t flags, uint32_t blocks, uint32_t start, uint16_t path, uint32_t size) {
        uint8_t* e = &f[blockAddr(0) + i * 0x40];
        size_t nl = strlen(name);
        memcpy(e, name, nl);
        e[0x28] = flags | (uint8_t)nl;
        e[0x29] = blocks & 255; e[0x2A] = (blocks >> 8) & 255; e[0x2B] = blocks >> 16;
        e[0x2C] = blocks & 255;
        e[0x2F] = start & 255; e[0x30] = (start >> 8) & 255; e[0x31] = start >> 16;
        e[0x32] = path >> 8; e[0x33] = path & 255;
        e[0x34] = size >> 24; e[0x35] = size >> 16; e[0x36] = size >> 8; e[0x37] = size;
    };
    entry(0, "csgo", 0x80, 0, 0, 0xFFFF, 0);
    entry(1, "test.txt", 0x40, 1, 1, 0, 5);
    entry(2, "chain.bin", 0x00, 2, 3, 0, 0x1800);
    memcpy(&f[blockAddr(1)], "hello", 5);
    memset(&f[blockAddr(3)], 'A', 0x1000);
    memset(&f[blockAddr(5)], 'B', 0x1000);
    // rantai hash: blok 3 -> blok 5
    uint8_t* h = &f[0x1000 + 3 * 0x18];
    h[0x15] = 0; h[0x16] = 0; h[0x17] = 5;
    return f;
}

int main() {
    // ---- LZMA Valve
    {
        std::vector<uint8_t> src(10000);
        for (size_t i = 0; i < src.size(); ++i) src[i] = (uint8_t)(i * 7 % 13);
        auto c = valveLzma(src);
        std::vector<uint8_t> d;
        CHECK(isValveLzma(c.data(), c.size()));
        CHECK(decompressValveLzma(c.data(), c.size(), d));
        CHECK(d == src);
    }

    // ---- BSP big-endian + LE
    Bsp bsp;
    for (bool be : {false, true}) {
        Bsp b;
        std::string err;
        auto file = makeBsp(be);
        CHECK(b.load(file, err));
        CHECK(b.bigEndian == be);
        CHECK(b.planes.size() == 6);
        CHECK(b.faces.size() == 1 && b.faces[0].numedges == 4 && b.faces[0].dispinfo == -1);
        CHECK(b.faces[0].lmSize[0] == 64);
        CHECK(b.vertices.size() == 4 && b.vertices[2].x == 512.f);
        CHECK(b.texName(0) == "dev/floor");
        CHECK(b.leafs.size() == 2 && b.leafs[1].contents == 1 && b.leafs[1].numleafbrushes == 1);
        CHECK(b.brushsides.size() == 6 && b.brushsides[5].planenum == 5);
        CHECK(b.nodes.size() == 1 && b.nodes[0].children[1] == -2);
        CHECK(b.entities.size() == 2 && b.entities[0].get("skyname") == "sky_test");
        CHECK(std::fabs(b.texinfos[0].lv[0][0] - 1.f / 16) < 1e-6f);
        if (be) bsp = b;
    }

    // ---- VTFX DXT1 (360) tanpa & dengan LZMA
    for (bool lz : {false, true}) {
        TextureData td;
        std::string err;
        auto v = makeVtfx(lz);
        CHECK(loadVtf(v, 0, false, td, err));
        CHECK(td.width == 8 && td.height == 8 && td.isConsole);
        CHECK(td.mips.size() == 4);
        if (!td.mips.empty()) {
            const uint8_t* p = td.mips[0].data.data();
            CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
        }
        TextureData tb;
        CHECK(loadVtf(v, 0, true, tb, err));
        CHECK(tb.fmt == TexFmt::BC1 && tb.mips[0].data.size() == 32 && tb.mips[0].data[1] == 0xF8);
        if (!err.empty()) fprintf(stderr, "vtf err: %s\n", err.c_str());
    }

    // ---- ZIP + VFS + VMT patch + peta
    Vfs vfs;
    {
        std::string base = "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"dev/floor_tex\"\n\t\"$surfaceprop\" \"concrete\" // komentar\n}\n";
        std::string patch = "patch\n{\n include \"materials/dev/base.vmt\"\n replace\n {\n  \"$alphatest\" 1\n }\n}\n";
        auto zip = makeZip({{"materials/dev/base.vmt", S(base)}, {"materials/dev/floor.vmt", S(patch)},
                            {"materials/dev/floor_tex.360.vtf", makeVtfx(true)}});
        vfs.addRaw("csgo/zip0.360.zip", makeMemFile(std::move(zip)));
        vfs.addRaw("csgo/maps/de_test.360.bsp", makeMemFile(makeBsp(true)));
        auto stfs = makeStfs();
        std::string tmp = "/tmp/csgo_stfs_test.bin";
        FILE* fp = fopen(tmp.c_str(), "wb");
        fwrite(stfs.data(), 1, stfs.size(), fp);
        fclose(fp);
        CHECK(vfs.mountPath(tmp));
        vfs.finalizeArchives();
        auto maps = vfs.listMaps();
        CHECK(maps.size() == 1 && maps[0] == "de_test");
        std::vector<uint8_t> d;
        CHECK(vfs.readFile("maps/de_test.bsp", d) && d.size() > 1036);
        CHECK(vfs.readFile("csgo/test.txt", d) && std::string(d.begin(), d.end()) == "hello");
        CHECK(vfs.readFile("csgo/chain.bin", d) && d.size() == 0x1800 && d[0] == 'A' && d[0x1000] == 'B' && d[0x17ff] == 'B');
        Material m = loadMaterial(vfs, "DEV/FLOOR");
        CHECK(m.found && m.shader == "lightmappedgeneric");
        CHECK(m.get("$basetexture") == "dev/floor_tex");
        CHECK(m.flag("$alphatest"));
        CHECK(vfs.readFile("materials/dev/floor_tex.vtf", d));
        TextureData td;
        std::string err;
        CHECK(loadVtf(d, 1, false, td, err) && td.width == 8); // skip dibatasi (min 16px tidak tercapai)
    }

    // ---- dunia
    WorldMesh wm;
    {
        std::string err;
        WorldBuildOptions o;
        CHECK(buildWorld(bsp, vfs, o, wm, err));
        CHECK(wm.batches.size() == 1 && wm.indices.size() == 6 && wm.vertices.size() == 4);
        CHECK(wm.materials.size() == 1 && wm.materials[0].kind == MatKind::AlphaTest && wm.materials[0].baseTex == "dev/floor_tex");
        CHECK(wm.skyName == "sky_test" && wm.spawns.size() == 1 && wm.spawns[0].yaw == 90.f);
        CHECK(wm.lmWidth >= 66);
        // lightmap sudut (0,0) -> nilai 128/255
        const WorldVertex& v0 = wm.vertices[0];
        int lx = (int)(v0.lm[0] * wm.lmWidth), ly = (int)(v0.lm[1] * wm.lmHeight);
        uint16_t hr = wm.lightmapRGBA16F[((size_t)ly * wm.lmWidth + lx) * 4];
        CHECK(hr == floatToHalf(128.f / 255.f));
    }

    // ---- kolisi + pemain jatuh ke lantai brush
    {
        Collision col;
        col.build(bsp, {});
        TraceResult tr = col.traceBox(Vec3(0, 0, 100), Vec3(0, 0, -100), Vec3(-16, -16, 0), Vec3(16, 16, 72));
        CHECK(tr.fraction < 1 && std::fabs(tr.endpos.z) < 0.1f && tr.normal.z > 0.99f);
        Player p;
        p.spawn(Vec3(0, 0, 100), 90);
        PlayerInput in;
        for (int i = 0; i < 180; ++i) p.update(1.0f / 60, in, col);
        CHECK(p.onGround && std::fabs(p.origin.z) < 0.5f);
        in.moveY = 1;
        for (int i = 0; i < 60; ++i) p.update(1.0f / 60, in, col);
        CHECK(p.origin.y > 150 && p.origin.y < 260 && std::fabs(p.origin.x) < 1);
        in.moveY = 0;
        in.jump = true;
        p.update(1.0f / 60, in, col);
        CHECK(!p.onGround && p.velocity.z > 200);

        // lantai displacement
        Collision c2;
        c2.build(bsp, {Vec3(-100, -100, 10), Vec3(100, -100, 10), Vec3(0, 100, 10)});
        float z;
        Vec3 n;
        CHECK(c2.dispFloor(0, 0, 50, -50, z, n) && std::fabs(z - 10) < 1e-3f && n.z > 0.99f);
        CHECK(!c2.dispFloor(0, 0, 5, -50, z, n));
    }

    // ---- dekoder BC3
    {
        uint8_t blk[16] = {255, 0, 0, 0, 0, 0, 0, 0, 0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0};
        uint8_t px[64];
        decodeBC(TexFmt::BC3, blk, 4, 4, px);
        CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255);
    }

    if (g_fail) { fprintf(stderr, "%d uji gagal\n", g_fail); return 1; }
    printf("Semua uji format lulus\n");
    return 0;
}
