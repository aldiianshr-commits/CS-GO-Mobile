#include "bsp.h"
#include "lzma_valve.h"

namespace cs {

bool Bsp::readLump(const std::vector<uint8_t>& file, int idx, std::vector<uint8_t>& out, int* ver) {
    out.clear();
    const Lump& l = lumps_[idx];
    if (ver) *ver = l.ver;
    if (l.len <= 0) return true;
    if (l.ofs < 0 || (size_t)l.ofs + (size_t)l.len > file.size()) {
        LOGW("lump %d di luar file", idx);
        return false;
    }
    const uint8_t* d = file.data() + l.ofs;
    if (isValveLzma(d, (size_t)l.len)) {
        if (!decompressValveLzma(d, (size_t)l.len, out)) { LOGE("LZMA lump %d gagal", idx); return false; }
    } else {
        out.assign(d, d + l.len);
    }
    return true;
}

template <typename T>
bool Bsp::readArray(const std::vector<uint8_t>& file, int idx, const char* layout, std::vector<T>& out) {
    std::vector<uint8_t> raw;
    if (!readLump(file, idx, raw)) return false;
    size_t stride = layoutSize(layout);
    if (stride != sizeof(T)) { LOGE("layout lump %d salah (%zu vs %zu)", idx, stride, sizeof(T)); return false; }
    size_t count = raw.size() / stride;
    if (bigEndian) swapByLayout(raw.data(), count, layout);
    out.resize(count);
    if (count) memcpy(out.data(), raw.data(), count * sizeof(T));
    return true;
}

bool Bsp::load(const std::vector<uint8_t>& file, std::string& err) {
    if (file.size() < 1036) { err = "file BSP terlalu kecil"; return false; }
    uint32_t ident = rd32le(file.data());
    if (ident == 0x50534256) bigEndian = false;       // "VBSP"
    else if (ident == 0x56425350) bigEndian = true;   // "PSBV" (Xbox 360 / PS3)
    else { err = "bukan file BSP Source"; return false; }
    auto r32 = [&](size_t o) -> int32_t { return (int32_t)(bigEndian ? rd32be(&file[o]) : rd32le(&file[o])); };
    version = r32(4);
    for (int i = 0; i < 64; ++i) {
        size_t o = 8 + i * 16;
        lumps_[i] = {r32(o), r32(o + 4), r32(o + 8), r32(o + 12)};
    }
    LOGI("BSP v%d (%s)", version, bigEndian ? "big-endian/konsol" : "PC");
    if (version < 19 || version > 25) LOGW("versi BSP %d tidak umum", version);

    bool ok = true;
    ok &= readArray(file, LUMP_PLANES, "iiiii", planes);
    ok &= readArray(file, LUMP_VERTEXES, "iii", vertices);
    ok &= readArray(file, LUMP_EDGES, "hh", edges);
    ok &= readArray(file, LUMP_SURFEDGES, "i", surfedges);
    ok &= readArray(file, LUMP_FACES, "hbbihhhhbbbbiiiiiiihhi", faces);
    if (faces.empty()) readArray(file, LUMP_FACES_HDR, "hbbihhhhbbbbiiiiiiihhi", faces);
    ok &= readArray(file, LUMP_TEXINFO, "iiiiiiiiiiiiiiiiii", texinfos);
    ok &= readArray(file, LUMP_TEXDATA, "iiiiiiii", texdatas);
    ok &= readArray(file, LUMP_MODELS, "iiiiiiiiiiii", models);
    ok &= readArray(file, LUMP_NODES, "iiihhhhhhhhhh", nodes);
    ok &= readArray(file, LUMP_LEAFBRUSHES, "h", leafbrushes);
    ok &= readArray(file, LUMP_BRUSHES, "iii", brushes);
    ok &= readArray(file, LUMP_BRUSHSIDES, "hhhbb", brushsides);
    ok &= readArray(file, LUMP_DISP_VERTS, "iiiii", dispverts);
    if (!ok) { err = "lump BSP rusak"; return false; }

    // Leaf: versi 1 = 32 byte, versi 0 = 56 byte (dengan ambient lighting)
    {
        std::vector<uint8_t> raw;
        int ver = 0;
        readLump(file, LUMP_LEAFS, raw, &ver);
        size_t stride = (ver == 0) ? 56 : 32;
        size_t n = raw.size() / stride;
        leafs.resize(n);
        for (size_t i = 0; i < n; ++i) {
            uint8_t* p = &raw[i * stride];
            if (bigEndian) {
                swapByLayout(p, 1, "ihhhhhhhhhhhhh");
                if (stride == 56) swapByLayout(p + 54, 1, "h");
            }
            BspLeaf& lf = leafs[i];
            memcpy(&lf, p, 30);
            lf.water = 0; lf.pad = 0;
            memcpy(&lf.water, p + 28, 2);
        }
    }

    // Dispinfo 176 byte — hanya bagian awal yang dipakai
    {
        std::vector<uint8_t> raw;
        readLump(file, LUMP_DISPINFO, raw);
        size_t n = raw.size() / 176;
        dispinfos.resize(n);
        for (size_t i = 0; i < n; ++i) {
            uint8_t* p = &raw[i * 176];
            if (bigEndian) swapByLayout(p, 1, "iiiiiiiiih");
            BspDispInfo& d = dispinfos[i];
            memcpy(&d.startPos, p, 12);
            memcpy(&d.vertStart, p + 12, 4);
            memcpy(&d.triStart, p + 16, 4);
            memcpy(&d.power, p + 20, 4);
            memcpy(&d.minTess, p + 24, 4);
            memcpy(&d.smoothing, p + 28, 4);
            memcpy(&d.contents, p + 32, 4);
            memcpy(&d.mapFace, p + 36, 2);
        }
    }

    // Lightmap (byte, tidak perlu dibalik). Utamakan LDR, lalu HDR.
    readLump(file, LUMP_LIGHTING, lighting);
    if (lighting.empty()) {
        readLump(file, LUMP_LIGHTING_HDR, lighting);
        lightingHdr = !lighting.empty();
    }

    // Nama tekstur
    {
        std::vector<uint8_t> strData;
        std::vector<int32_t> table;
        readLump(file, LUMP_TEXDATA_STRING_DATA, strData);
        readArray(file, LUMP_TEXDATA_STRING_TABLE, "i", table);
        texNames.resize(texdatas.size());
        for (size_t i = 0; i < texdatas.size(); ++i) {
            int id = texdatas[i].nameId;
            if (id >= 0 && id < (int)table.size() && table[id] >= 0 && (size_t)table[id] < strData.size()) {
                const char* s = (const char*)&strData[table[id]];
                size_t maxLen = strData.size() - table[id];
                texNames[i] = normPath(std::string(s, strnlen(s, maxLen)));
            }
        }
    }

    // Pakfile (lump tidak dikompres; isinya ZIP)
    {
        const Lump& l = lumps_[LUMP_PAKFILE];
        if (l.len > 0 && (size_t)l.ofs + l.len <= file.size())
            pakfile.assign(file.begin() + l.ofs, file.begin() + l.ofs + l.len);
    }

    // Entitas (teks)
    {
        std::vector<uint8_t> raw;
        readLump(file, LUMP_ENTITIES, raw);
        parseEntities(std::string(raw.begin(), raw.end()));
    }

    LOGI("BSP: %zu face, %zu vertex, %zu texinfo, %zu disp, %zu brush, %zu entitas, lightmap %zu KB%s",
         faces.size(), vertices.size(), texinfos.size(), dispinfos.size(), brushes.size(), entities.size(),
         lighting.size() / 1024, lightingHdr ? " (HDR)" : "");
    if (faces.empty() || vertices.empty()) { err = "BSP tidak berisi geometri"; return false; }
    return true;
}

void Bsp::parseEntities(const std::string& t) {
    size_t i = 0, n = t.size();
    auto skipWs = [&]() { while (i < n && (isspace((unsigned char)t[i]) || t[i] == 0)) ++i; };
    auto readQuoted = [&](std::string& out) -> bool {
        skipWs();
        if (i >= n || t[i] != '"') return false;
        size_t e = t.find('"', i + 1);
        if (e == std::string::npos) return false;
        out = t.substr(i + 1, e - i - 1);
        i = e + 1;
        return true;
    };
    while (true) {
        skipWs();
        if (i >= n || t[i] != '{') break;
        ++i;
        BspEntity ent;
        while (true) {
            skipWs();
            if (i >= n) break;
            if (t[i] == '}') { ++i; break; }
            std::string k, v;
            if (!readQuoted(k) || !readQuoted(v)) { i = n; break; }
            ent.kv[toLower(k)] = v;
        }
        entities.push_back(std::move(ent));
    }
}

std::string Bsp::texName(int ti) const {
    if (ti < 0 || ti >= (int)texinfos.size()) return "";
    int td = texinfos[ti].texdata;
    if (td < 0 || td >= (int)texNames.size()) return "";
    return texNames[td];
}

} // namespace cs
