// Pembaca BSP Source v19-21, termasuk BSP konsol Xbox 360 ("PSBV", big-endian,
// lump terkompresi LZMA). Struktur internal disimpan dalam little-endian.
#pragma once
#include "../core/common.h"
#include <string>
#include <vector>
#include <map>

namespace cs {

enum BspLump {
    LUMP_ENTITIES = 0, LUMP_PLANES = 1, LUMP_TEXDATA = 2, LUMP_VERTEXES = 3, LUMP_NODES = 5,
    LUMP_TEXINFO = 6, LUMP_FACES = 7, LUMP_LIGHTING = 8, LUMP_LEAFS = 10, LUMP_EDGES = 12,
    LUMP_SURFEDGES = 13, LUMP_MODELS = 14, LUMP_LEAFBRUSHES = 17, LUMP_BRUSHES = 18,
    LUMP_BRUSHSIDES = 19, LUMP_DISPINFO = 26, LUMP_DISP_VERTS = 33, LUMP_GAME_LUMP = 35,
    LUMP_PAKFILE = 40, LUMP_TEXDATA_STRING_DATA = 43, LUMP_TEXDATA_STRING_TABLE = 44,
    LUMP_DISP_TRIS = 48, LUMP_LIGHTING_HDR = 53, LUMP_FACES_HDR = 58
};

#pragma pack(push, 1)
struct BspPlane { Vec3 normal; float dist; int32_t type; };
struct BspEdge { uint16_t v[2]; };
struct BspFace {
    uint16_t planenum; uint8_t side, onNode; int32_t firstedge; int16_t numedges, texinfo, dispinfo, fogVolume;
    uint8_t styles[4]; int32_t lightofs; float area; int32_t lmMins[2], lmSize[2]; int32_t origFace;
    uint16_t numPrims, firstPrim; uint32_t smoothing;
};
struct BspTexinfo { float tv[2][4]; float lv[2][4]; int32_t flags; int32_t texdata; };
struct BspTexdata { Vec3 reflectivity; int32_t nameId; int32_t width, height, viewW, viewH; };
struct BspModel { Vec3 mins, maxs, origin; int32_t headnode, firstface, numfaces; };
struct BspNode { int32_t planenum; int32_t children[2]; int16_t mins[3], maxs[3]; uint16_t firstface, numfaces; int16_t area, pad; };
struct BspLeaf { int32_t contents; int16_t cluster, areaflags; int16_t mins[3], maxs[3];
                 uint16_t firstleafface, numleaffaces, firstleafbrush, numleafbrushes; int16_t water, pad; };
struct BspBrush { int32_t firstside, numsides, contents; };
struct BspBrushSide { uint16_t planenum; int16_t texinfo, dispinfo; uint8_t bevel, thin; };
struct BspDispVert { Vec3 vec; float dist, alpha; };
#pragma pack(pop)

static_assert(sizeof(BspFace) == 56, "dface_t");
static_assert(sizeof(BspTexinfo) == 72, "texinfo_t");
static_assert(sizeof(BspTexdata) == 32, "dtexdata_t");
static_assert(sizeof(BspModel) == 48, "dmodel_t");
static_assert(sizeof(BspNode) == 32, "dnode_t");
static_assert(sizeof(BspLeaf) == 32, "dleaf_t");
static_assert(sizeof(BspBrushSide) == 8, "dbrushside_t");

struct BspDispInfo {
    Vec3 startPos;
    int32_t vertStart = 0, triStart = 0, power = 0, minTess = 0;
    float smoothing = 0;
    int32_t contents = 0;
    uint16_t mapFace = 0;
};

// Flag texinfo
enum { SURF_LIGHT = 0x1, SURF_SKY2D = 0x2, SURF_SKY = 0x4, SURF_WARP = 0x8, SURF_TRANS = 0x10,
       SURF_TRIGGER = 0x40, SURF_NODRAW = 0x80, SURF_HINT = 0x100, SURF_SKIP = 0x200,
       SURF_NOLIGHT = 0x400, SURF_BUMPLIGHT = 0x800 };

// Isi brush
enum { CONTENTS_SOLID = 0x1, CONTENTS_WINDOW = 0x2, CONTENTS_GRATE = 0x8, CONTENTS_WATER = 0x20,
       CONTENTS_MOVEABLE = 0x4000, CONTENTS_PLAYERCLIP = 0x10000, CONTENTS_MONSTER = 0x2000000,
       MASK_PLAYERSOLID = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_MOVEABLE |
                          CONTENTS_PLAYERCLIP | CONTENTS_MONSTER };

struct BspEntity {
    std::map<std::string, std::string> kv;
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
};

class Bsp {
public:
    bool load(const std::vector<uint8_t>& file, std::string& err);

    bool bigEndian = false;
    int version = 0;
    std::vector<BspPlane> planes;
    std::vector<Vec3> vertices;
    std::vector<BspEdge> edges;
    std::vector<int32_t> surfedges;
    std::vector<BspFace> faces;
    std::vector<BspTexinfo> texinfos;
    std::vector<BspTexdata> texdatas;
    std::vector<BspModel> models;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leafs;
    std::vector<uint16_t> leafbrushes;
    std::vector<BspBrush> brushes;
    std::vector<BspBrushSide> brushsides;
    std::vector<BspDispInfo> dispinfos;
    std::vector<BspDispVert> dispverts;
    std::vector<uint8_t> lighting;     // ColorRGBExp32
    bool lightingHdr = false;
    std::vector<uint8_t> pakfile;      // ZIP berisi material khusus peta
    std::vector<std::string> texNames; // per texdata
    std::vector<BspEntity> entities;

    std::string texName(int texinfo) const;

private:
    bool readLump(const std::vector<uint8_t>& file, int idx, std::vector<uint8_t>& out, int* version = nullptr);
    template <typename T>
    bool readArray(const std::vector<uint8_t>& file, int idx, const char* layout, std::vector<T>& out);
    void parseEntities(const std::string& text);
    struct Lump { int32_t ofs, len, ver, fourCC; } lumps_[64];
};

} // namespace cs
