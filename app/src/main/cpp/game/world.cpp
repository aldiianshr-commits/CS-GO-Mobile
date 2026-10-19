#include "world.h"
#include "../fs/vfs.h"
#include <map>
#include <unordered_map>

namespace cs {

Vec3 parseVec3(const std::string& s) {
    Vec3 v;
    sscanf(s.c_str(), "%f %f %f", &v.x, &v.y, &v.z);
    return v;
}

namespace {

struct LmRect { int w, h, x = 0, y = 0, id; };

bool packRects(std::vector<LmRect>& rects, int size) {
    int x = 1, y = 1, rowH = 0;
    for (auto& r : rects) {
        if (r.w + 2 > size) return false;
        if (x + r.w + 1 > size) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + r.h + 1 > size) return false;
        r.x = x; r.y = y;
        x += r.w + 1;
        rowH = std::max(rowH, r.h);
    }
    return true;
}

float expScale(int8_t e) { return std::ldexp(1.0f, e); }

bool isToolTexture(const std::string& n) {
    return startsWith(n, "tools/") || n.find("toolsnodraw") != std::string::npos ||
           n.find("toolsskybox") != std::string::npos || n.find("toolstrigger") != std::string::npos ||
           n.find("toolsclip") != std::string::npos || n.find("toolsinvisible") != std::string::npos;
}

std::string texPathFromParam(const std::string& v) {
    std::string p = normPath(v);
    if (endsWith(p, ".vtf")) p = p.substr(0, p.size() - 4);
    return p;
}

WorldMaterial resolveMaterial(const Vfs& vfs, const std::string& name) {
    WorldMaterial wm;
    wm.name = name;
    Material m = loadMaterial(vfs, name);
    wm.found = m.found;
    if (!m.found) return wm;
    wm.baseTex = texPathFromParam(m.get("$basetexture"));
    wm.baseTex2 = texPathFromParam(m.get("$basetexture2"));
    const std::string& sh = m.shader;
    if (sh == "worldvertextransition" || sh == "lightmapped_4wayblend" || sh == "worldtwotextureblend")
        wm.blend2 = !wm.baseTex2.empty();
    if (sh == "unlitgeneric" || sh == "vertexlitgeneric" || sh == "unlittwotexture") wm.unlit = true;
    if (sh == "water" || sh.find("water") != std::string::npos) {
        wm.kind = MatKind::Water;
        float c[3];
        if (m.getColor("$fogcolor", c)) { wm.tint[0] = c[0]; wm.tint[1] = c[1]; wm.tint[2] = c[2]; }
        else { wm.tint[0] = 0.2f; wm.tint[1] = 0.3f; wm.tint[2] = 0.35f; }
        wm.tint[3] = 0.6f;
        if (wm.baseTex.empty()) wm.unlit = true;
        return wm;
    }
    if (m.flag("$additive")) wm.kind = MatKind::Additive;
    else if (m.flag("$translucent")) wm.kind = MatKind::Translucent;
    else if (m.flag("$alphatest")) {
        wm.kind = MatKind::AlphaTest;
        wm.alphaRef = m.getFloat("$alphatestreference", 0.5f);
    }
    float c[3];
    if (m.getColor("$color", c)) { wm.tint[0] = c[0]; wm.tint[1] = c[1]; wm.tint[2] = c[2]; }
    if (m.getColor("$layertint1", c)) { wm.tint[0] *= c[0]; wm.tint[1] *= c[1]; wm.tint[2] *= c[2]; }
    wm.tint[3] = m.getFloat("$alpha", 1.0f);
    return wm;
}

int kindOrder(MatKind k) {
    switch (k) {
        case MatKind::Opaque: return 0;
        case MatKind::AlphaTest: return 1;
        case MatKind::Water: return 2;
        case MatKind::Translucent: return 3;
        default: return 4;
    }
}

} // namespace

bool buildWorld(const Bsp& bsp, const Vfs& vfs, const WorldBuildOptions& opt, WorldMesh& out, std::string& err) {
    out = WorldMesh();

    // ---- entitas: sky, spawn, offset model brush
    std::unordered_map<int, Vec3> modelOffset;
    std::unordered_map<int, bool> modelHidden;
    for (auto& e : bsp.entities) {
        std::string cls = e.get("classname");
        if (cls == "worldspawn") out.skyName = e.get("skyname");
        if (cls == "info_player_terrorist" || cls == "info_player_counterterrorist" ||
            cls == "info_player_start" || cls == "info_deathmatch_spawn" || cls == "info_player_deathmatch") {
            SpawnPoint sp;
            sp.origin = parseVec3(e.get("origin"));
            Vec3 ang = parseVec3(e.get("angles"));
            sp.yaw = ang.y;
            sp.team = cls == "info_player_terrorist" ? "T" : cls == "info_player_counterterrorist" ? "CT" : "";
            out.spawns.push_back(sp);
        }
        std::string model = e.get("model");
        if (model.size() > 1 && model[0] == '*') {
            int mi = atoi(model.c_str() + 1);
            modelOffset[mi] = parseVec3(e.get("origin"));
            if (startsWith(cls, "trigger_") || cls == "func_buyzone" || cls == "func_bomb_target" ||
                cls == "func_hostage_rescue" || cls == "func_areaportal" || cls == "func_clip_vphysics" ||
                cls == "func_occluder" || cls == "func_precipitation" || e.get("rendermode") == "10")
                modelHidden[mi] = true;
        }
    }

    // ---- material
    std::unordered_map<std::string, int> matIndex;
    auto materialFor = [&](const std::string& name) -> int {
        auto it = matIndex.find(name);
        if (it != matIndex.end()) return it->second;
        WorldMaterial wm = resolveMaterial(vfs, name);
        if (!wm.found) out.missingMaterials++;
        int idx = (int)out.materials.size();
        out.materials.push_back(wm);
        matIndex[name] = idx;
        return idx;
    };

    // ---- kumpulkan face yang akan digambar
    struct FaceRef { int face; Vec3 offset; };
    std::vector<FaceRef> drawFaces;
    for (size_t mi = 0; mi < bsp.models.size(); ++mi) {
        if (modelHidden.count((int)mi)) continue;
        const BspModel& m = bsp.models[mi];
        Vec3 off = mi == 0 ? Vec3() : modelOffset[(int)mi];
        for (int f = m.firstface; f < m.firstface + m.numfaces && f < (int)bsp.faces.size(); ++f)
            drawFaces.push_back({f, off});
    }

    // ---- lightmap atlas
    std::vector<LmRect> rects;
    std::vector<int> faceRect(bsp.faces.size(), -1);
    const bool useLm = opt.lightmaps && !bsp.lighting.empty();
    for (auto& fr : drawFaces) {
        const BspFace& f = bsp.faces[fr.face];
        if (!useLm || f.lightofs < 0 || f.texinfo < 0 || f.texinfo >= (int)bsp.texinfos.size()) continue;
        if (bsp.texinfos[f.texinfo].flags & (SURF_NOLIGHT | SURF_SKY | SURF_SKY2D | SURF_NODRAW)) continue;
        int w = f.lmSize[0] + 1, h = f.lmSize[1] + 1;
        if (w <= 0 || h <= 0 || w > 512 || h > 512) continue;
        if ((size_t)f.lightofs + (size_t)w * h * 4 > bsp.lighting.size()) continue;
        faceRect[fr.face] = (int)rects.size();
        rects.push_back({w, h, 0, 0, fr.face});
    }
    std::vector<LmRect> sorted = rects;
    std::sort(sorted.begin(), sorted.end(), [](const LmRect& a, const LmRect& b) { return a.h > b.h || (a.h == b.h && a.w > b.w); });
    sorted.insert(sorted.begin(), LmRect{2, 2, 0, 0, -1}); // area putih untuk face tanpa lightmap
    int atlas = 256;
    while (atlas <= 4096 && !packRects(sorted, atlas)) atlas *= 2;
    if (atlas > 4096) { LOGW("lightmap atlas penuh — lightmap dimatikan"); atlas = 4; sorted.resize(1); packRects(sorted, atlas); rects.clear(); std::fill(faceRect.begin(), faceRect.end(), -1); }
    out.lmWidth = out.lmHeight = atlas;
    out.lightmapRGBA16F.assign((size_t)atlas * atlas * 4, floatToHalf(0.0f));
    LmRect whiteRect = sorted[0];
    std::unordered_map<int, LmRect> placed;
    for (auto& r : sorted) if (r.id >= 0) placed[r.id] = r;
    auto putPixel = [&](int x, int y, float r, float g, float b) {
        size_t o = ((size_t)y * atlas + x) * 4;
        out.lightmapRGBA16F[o] = floatToHalf(r);
        out.lightmapRGBA16F[o + 1] = floatToHalf(g);
        out.lightmapRGBA16F[o + 2] = floatToHalf(b);
        out.lightmapRGBA16F[o + 3] = floatToHalf(1.0f);
    };
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) putPixel(whiteRect.x + x, whiteRect.y + y, 1, 1, 1);
    for (auto& kv : placed) {
        const BspFace& f = bsp.faces[kv.first];
        const LmRect& r = kv.second;
        const uint8_t* s = &bsp.lighting[f.lightofs];
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                const uint8_t* p = s + ((size_t)y * r.w + x) * 4;
                float sc = expScale((int8_t)p[3]) / 255.0f * opt.lightmapScale;
                putPixel(r.x + x, r.y + y, p[0] * sc, p[1] * sc, p[2] * sc);
            }
        // tepi duplikat untuk filter bilinear
        for (int y = 0; y < r.h; ++y) {
            size_t a = ((size_t)(r.y + y) * atlas + r.x) * 4, b = a + (size_t)(r.w - 1) * 4;
            if (r.x > 0) memcpy(&out.lightmapRGBA16F[a - 4], &out.lightmapRGBA16F[a], 8);
            if (r.x + r.w < atlas) memcpy(&out.lightmapRGBA16F[b + 4], &out.lightmapRGBA16F[b], 8);
        }
    }
    const float invA = 1.0f / atlas;
    auto whiteUV = [&](float* lm) { lm[0] = (whiteRect.x + 1.0f) * invA; lm[1] = (whiteRect.y + 1.0f) * invA; };

    // ---- geometri per material
    std::map<int, std::vector<uint32_t>> perMat;
    out.mins = Vec3(1e9f, 1e9f, 1e9f);
    out.maxs = Vec3(-1e9f, -1e9f, -1e9f);
    auto addVertex = [&](const Vec3& p, const Vec3& base, const BspTexinfo& ti, const BspTexdata* td,
                         const BspFace& f, int faceIdx, float alpha) -> uint32_t {
        WorldVertex v{};
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        float tw = td && td->width > 0 ? (float)td->width : 256.f, th = td && td->height > 0 ? (float)td->height : 256.f;
        v.uv[0] = (base.x * ti.tv[0][0] + base.y * ti.tv[0][1] + base.z * ti.tv[0][2] + ti.tv[0][3]) / tw;
        v.uv[1] = (base.x * ti.tv[1][0] + base.y * ti.tv[1][1] + base.z * ti.tv[1][2] + ti.tv[1][3]) / th;
        auto it = placed.find(faceIdx);
        if (it != placed.end()) {
            float s = base.x * ti.lv[0][0] + base.y * ti.lv[0][1] + base.z * ti.lv[0][2] + ti.lv[0][3] - f.lmMins[0];
            float t = base.x * ti.lv[1][0] + base.y * ti.lv[1][1] + base.z * ti.lv[1][2] + ti.lv[1][3] - f.lmMins[1];
            s = std::max(0.0f, std::min(s, (float)f.lmSize[0]));
            t = std::max(0.0f, std::min(t, (float)f.lmSize[1]));
            v.lm[0] = (it->second.x + s + 0.5f) * invA;
            v.lm[1] = (it->second.y + t + 0.5f) * invA;
        } else whiteUV(v.lm);
        v.alpha = alpha;
        out.mins = Vec3(std::min(out.mins.x, p.x), std::min(out.mins.y, p.y), std::min(out.mins.z, p.z));
        out.maxs = Vec3(std::max(out.maxs.x, p.x), std::max(out.maxs.y, p.y), std::max(out.maxs.z, p.z));
        out.vertices.push_back(v);
        return (uint32_t)out.vertices.size() - 1;
    };

    for (auto& fr : drawFaces) {
        const BspFace& f = bsp.faces[fr.face];
        if (f.texinfo < 0 || f.texinfo >= (int)bsp.texinfos.size() || f.numedges < 3) continue;
        const BspTexinfo& ti = bsp.texinfos[f.texinfo];
        if (ti.flags & (SURF_NODRAW | SURF_SKY | SURF_SKY2D | SURF_TRIGGER | SURF_HINT | SURF_SKIP)) continue;
        std::string tname = bsp.texName(f.texinfo);
        if (tname.empty() || isToolTexture(tname)) continue;
        const BspTexdata* td = (ti.texdata >= 0 && ti.texdata < (int)bsp.texdatas.size()) ? &bsp.texdatas[ti.texdata] : nullptr;
        int mat = materialFor(tname);
        auto& idx = perMat[mat];

        // poligon face
        std::vector<Vec3> poly;
        for (int e = 0; e < f.numedges; ++e) {
            int si = f.firstedge + e;
            if (si < 0 || si >= (int)bsp.surfedges.size()) break;
            int se = bsp.surfedges[si];
            int ei = se >= 0 ? se : -se;
            if (ei >= (int)bsp.edges.size()) break;
            int vi = se >= 0 ? bsp.edges[ei].v[0] : bsp.edges[ei].v[1];
            if (vi >= (int)bsp.vertices.size()) break;
            poly.push_back(bsp.vertices[vi] + fr.offset);
        }
        if (poly.size() < 3) continue;

        if (f.dispinfo >= 0 && f.dispinfo < (int)bsp.dispinfos.size() && poly.size() == 4) {
            // ---- displacement
            const BspDispInfo& di = bsp.dispinfos[f.dispinfo];
            int start = 0;
            float best = 1e30f;
            for (int i = 0; i < 4; ++i) {
                float d = length(poly[i] - (di.startPos + fr.offset));
                if (d < best) { best = d; start = i; }
            }
            Vec3 c[4];
            for (int i = 0; i < 4; ++i) c[i] = poly[(start + i) % 4];
            int n = (1 << di.power) + 1;
            if (di.power < 1 || di.power > 4 || di.vertStart < 0 || di.vertStart + n * n > (int)bsp.dispverts.size()) continue;
            uint32_t base = (uint32_t)out.vertices.size();
            std::vector<Vec3> dpos((size_t)n * n);
            for (int row = 0; row < n; ++row)
                for (int col = 0; col < n; ++col) {
                    float v = (float)row / (n - 1), u = (float)col / (n - 1);
                    Vec3 e0 = lerp(c[0], c[1], v), e1 = lerp(c[3], c[2], v);
                    Vec3 flat = lerp(e0, e1, u);
                    const BspDispVert& dv = bsp.dispverts[di.vertStart + row * n + col];
                    Vec3 p = flat + dv.vec * dv.dist;
                    dpos[(size_t)row * n + col] = p;
                    addVertex(p, flat - fr.offset, ti, td, f, fr.face, std::max(0.0f, std::min(1.0f, dv.alpha / 255.0f)));
                }
            for (int row = 0; row < n - 1; ++row)
                for (int col = 0; col < n - 1; ++col) {
                    uint32_t i0 = base + row * n + col, i1 = i0 + 1, i2 = i0 + n, i3 = i2 + 1;
                    const Vec3 &p0 = dpos[(size_t)row * n + col], &p1 = dpos[(size_t)row * n + col + 1],
                               &p2 = dpos[(size_t)(row + 1) * n + col], &p3 = dpos[(size_t)(row + 1) * n + col + 1];
                    if ((row + col) & 1) {
                        idx.insert(idx.end(), {i0, i2, i1, i1, i2, i3});
                        out.dispTriangles.insert(out.dispTriangles.end(), {p0, p2, p1, p1, p2, p3});
                    } else {
                        idx.insert(idx.end(), {i0, i2, i3, i0, i3, i1});
                        out.dispTriangles.insert(out.dispTriangles.end(), {p0, p2, p3, p0, p3, p1});
                    }
                }
            out.dispCount++;
        } else {
            // ---- face brush biasa (kipas segitiga)
            uint32_t base = (uint32_t)out.vertices.size();
            for (auto& p : poly) addVertex(p, p - fr.offset, ti, td, f, fr.face, 1.0f);
            for (uint32_t i = 1; i + 1 < poly.size(); ++i) idx.insert(idx.end(), {base, base + i, base + i + 1});
        }
        out.faceCount++;
    }

    // ---- susun batch: opaque -> alphatest -> air -> transparan
    std::vector<int> order;
    for (auto& kv : perMat) if (!kv.second.empty()) order.push_back(kv.first);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return kindOrder(out.materials[a].kind) < kindOrder(out.materials[b].kind);
    });
    for (int m : order) {
        auto& idx = perMat[m];
        DrawBatch b;
        b.material = m;
        b.firstIndex = (uint32_t)out.indices.size();
        b.indexCount = (uint32_t)idx.size();
        b.mins[0] = b.mins[1] = b.mins[2] = 1e9f;
        b.maxs[0] = b.maxs[1] = b.maxs[2] = -1e9f;
        for (uint32_t i : idx) for (int k = 0; k < 3; ++k) {
            b.mins[k] = std::min(b.mins[k], out.vertices[i].pos[k]);
            b.maxs[k] = std::max(b.maxs[k], out.vertices[i].pos[k]);
        }
        out.indices.insert(out.indices.end(), idx.begin(), idx.end());
        out.batches.push_back(b);
    }
    LOGI("Dunia: %zu face, %zu displacement, %zu vertex, %zu index, %zu material (%zu hilang), lightmap %dx%d, sky '%s', %zu spawn",
         out.faceCount, out.dispCount, out.vertices.size(), out.indices.size(), out.materials.size(),
         out.missingMaterials, out.lmWidth, out.lmHeight, out.skyName.c_str(), out.spawns.size());
    if (out.indices.empty()) { err = "tidak ada geometri yang bisa digambar"; return false; }
    return true;
}

} // namespace cs
