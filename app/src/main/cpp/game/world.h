// Membangun mesh dunia siap-render dari BSP: face brush, displacement, lightmap atlas,
// pengelompokan per material, skybox, titik spawn.
#pragma once
#include "../format/bsp.h"
#include "../format/vmt.h"
#include <string>
#include <vector>

namespace cs {

class Vfs;

struct WorldVertex {
    float pos[3];
    float uv[2];
    float lm[2];
    float alpha;
};
static_assert(sizeof(WorldVertex) == 32, "WorldVertex");

enum class MatKind { Opaque, AlphaTest, Translucent, Additive, Water };

struct WorldMaterial {
    std::string name;
    std::string baseTex, baseTex2;
    MatKind kind = MatKind::Opaque;
    bool blend2 = false;   // WorldVertexTransition: campur 2 tekstur via alpha vertex
    bool unlit = false;    // UnlitGeneric (tanpa lightmap)
    float alphaRef = 0.5f;
    float tint[4] = {1, 1, 1, 1};
    bool found = false;
};

struct DrawBatch {
    int material = 0;
    uint32_t firstIndex = 0, indexCount = 0;
    float mins[3], maxs[3];
};

struct SpawnPoint {
    Vec3 origin;
    float yaw = 0;
    std::string team;
};

struct WorldMesh {
    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawBatch> batches;           // diurutkan: opaque, alphatest, lalu transparan
    std::vector<WorldMaterial> materials;
    int lmWidth = 0, lmHeight = 0;
    std::vector<uint16_t> lightmapRGBA16F;    // atlas lightmap (half float)
    std::string skyName;
    std::vector<SpawnPoint> spawns;
    Vec3 mins, maxs;
    // segitiga displacement untuk kolisi (sudah terdisplacement)
    std::vector<Vec3> dispTriangles;
    size_t faceCount = 0, dispCount = 0, missingMaterials = 0;
};

struct WorldBuildOptions {
    bool lightmaps = true;
    float lightmapScale = 1.0f;
};

bool buildWorld(const Bsp& bsp, const Vfs& vfs, const WorldBuildOptions& opt, WorldMesh& out, std::string& err);

Vec3 parseVec3(const std::string& s);

} // namespace cs
