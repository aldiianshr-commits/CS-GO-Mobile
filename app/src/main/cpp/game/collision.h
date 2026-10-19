// Kolisi dunia: trace kotak terhadap brush BSP (gaya CM Quake/Source) +
// lantai displacement (raycast vertikal ke segitiga displacement).
#pragma once
#include "../format/bsp.h"
#include <unordered_map>
#include <vector>

namespace cs {

struct TraceResult {
    float fraction = 1.0f;
    Vec3 endpos;
    Vec3 normal;
    bool startSolid = false, allSolid = false;
};

class Collision {
public:
    void build(const Bsp& bsp, const std::vector<Vec3>& dispTriangles);
    TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const;
    // Tinggi lantai displacement tertinggi di (x,y) antara zBottom..zTop
    bool dispFloor(float x, float y, float zTop, float zBottom, float& outZ, Vec3& outNormal) const;
    bool ready() const { return bsp_ != nullptr; }

private:
    struct TraceWork {
        Vec3 start, end, ext;
        TraceResult* tr;
        std::vector<uint32_t>* checked;
        uint32_t stamp;
    };
    void recurse(TraceWork& tw, int num, float p1f, float p2f, const Vec3& p1, const Vec3& p2) const;
    void traceLeaf(TraceWork& tw, int leaf) const;
    void clipBrush(TraceWork& tw, const BspBrush& b) const;

    const Bsp* bsp_ = nullptr;
    std::vector<bool> brushSolid_;
    mutable std::vector<uint32_t> checked_;
    mutable uint32_t stamp_ = 0;

    std::vector<Vec3> dispTris_;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    static constexpr float kCell = 64.0f;
    static int64_t cellKey(int cx, int cy) { return ((int64_t)cx << 32) ^ (uint32_t)cy; }
};

} // namespace cs
