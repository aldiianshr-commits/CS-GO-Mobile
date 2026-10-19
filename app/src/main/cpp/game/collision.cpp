#include "collision.h"

namespace cs {

static constexpr float DIST_EPS = 0.03125f;

void Collision::build(const Bsp& bsp, const std::vector<Vec3>& dispTriangles) {
    bsp_ = &bsp;
    brushSolid_.assign(bsp.brushes.size(), false);
    size_t solid = 0;
    for (size_t i = 0; i < bsp.brushes.size(); ++i) {
        const BspBrush& b = bsp.brushes[i];
        if (!(b.contents & MASK_PLAYERSOLID)) continue;
        bool hasDisp = false;
        for (int s = 0; s < b.numsides; ++s) {
            int si = b.firstside + s;
            if (si >= 0 && si < (int)bsp.brushsides.size() && bsp.brushsides[si].dispinfo >= 0) { hasDisp = true; break; }
        }
        // brush pemilik displacement tidak padat — permukaan displacement yang dipakai
        if (hasDisp) continue;
        brushSolid_[i] = true;
        ++solid;
    }
    checked_.assign(bsp.brushes.size(), 0);

    dispTris_ = dispTriangles;
    grid_.clear();
    for (uint32_t t = 0; t + 2 < dispTris_.size(); t += 3) {
        const Vec3 &a = dispTris_[t], &b = dispTris_[t + 1], &c = dispTris_[t + 2];
        int x0 = (int)std::floor(std::min({a.x, b.x, c.x}) / kCell), x1 = (int)std::floor(std::max({a.x, b.x, c.x}) / kCell);
        int y0 = (int)std::floor(std::min({a.y, b.y, c.y}) / kCell), y1 = (int)std::floor(std::max({a.y, b.y, c.y}) / kCell);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) grid_[cellKey(x, y)].push_back(t);
    }
    LOGI("Kolisi: %zu brush padat, %zu segitiga displacement", solid, dispTris_.size() / 3);
}

TraceResult Collision::traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const {
    TraceResult tr;
    tr.endpos = end;
    if (!bsp_ || bsp_->models.empty() || bsp_->nodes.empty()) return tr;
    Vec3 c = (mins + maxs) * 0.5f;
    TraceWork tw;
    tw.start = start + c;
    tw.end = end + c;
    tw.ext = (maxs - mins) * 0.5f;
    tw.tr = &tr;
    tw.checked = &checked_;
    if (++stamp_ == 0) { std::fill(checked_.begin(), checked_.end(), 0); stamp_ = 1; }
    tw.stamp = stamp_;
    recurse(tw, bsp_->models[0].headnode, 0.0f, 1.0f, tw.start, tw.end);
    if (tr.fraction >= 1.0f) tr.endpos = end;
    else tr.endpos = start + (end - start) * tr.fraction;
    return tr;
}

void Collision::recurse(TraceWork& tw, int num, float p1f, float p2f, const Vec3& p1, const Vec3& p2) const {
    if (tw.tr->fraction <= p1f) return;
    if (num < 0) { traceLeaf(tw, -1 - num); return; }
    if (num >= (int)bsp_->nodes.size()) return;
    const BspNode& node = bsp_->nodes[num];
    if (node.planenum < 0 || node.planenum >= (int)bsp_->planes.size()) return;
    const BspPlane& pl = bsp_->planes[node.planenum];
    float t1 = dot(pl.normal, p1) - pl.dist;
    float t2 = dot(pl.normal, p2) - pl.dist;
    float offset = std::fabs(tw.ext.x * pl.normal.x) + std::fabs(tw.ext.y * pl.normal.y) + std::fabs(tw.ext.z * pl.normal.z);
    if (t1 >= offset + 1 && t2 >= offset + 1) { recurse(tw, node.children[0], p1f, p2f, p1, p2); return; }
    if (t1 < -offset - 1 && t2 < -offset - 1) { recurse(tw, node.children[1], p1f, p2f, p1, p2); return; }
    int side;
    float frac, frac2;
    if (t1 < t2) {
        float id = 1.0f / (t1 - t2);
        side = 1;
        frac2 = (t1 + offset + DIST_EPS) * id;
        frac = (t1 - offset + DIST_EPS) * id;
    } else if (t1 > t2) {
        float id = 1.0f / (t1 - t2);
        side = 0;
        frac2 = (t1 - offset - DIST_EPS) * id;
        frac = (t1 + offset + DIST_EPS) * id;
    } else {
        side = 0; frac = 1; frac2 = 0;
    }
    frac = std::max(0.0f, std::min(1.0f, frac));
    frac2 = std::max(0.0f, std::min(1.0f, frac2));
    float midf = p1f + (p2f - p1f) * frac;
    Vec3 mid = p1 + (p2 - p1) * frac;
    recurse(tw, node.children[side], p1f, midf, p1, mid);
    midf = p1f + (p2f - p1f) * frac2;
    mid = p1 + (p2 - p1) * frac2;
    recurse(tw, node.children[side ^ 1], midf, p2f, mid, p2);
}

void Collision::traceLeaf(TraceWork& tw, int leaf) const {
    if (leaf < 0 || leaf >= (int)bsp_->leafs.size()) return;
    const BspLeaf& lf = bsp_->leafs[leaf];
    for (int i = 0; i < lf.numleafbrushes; ++i) {
        int li = lf.firstleafbrush + i;
        if (li >= (int)bsp_->leafbrushes.size()) break;
        int bi = bsp_->leafbrushes[li];
        if (bi >= (int)bsp_->brushes.size() || !brushSolid_[bi]) continue;
        if ((*tw.checked)[bi] == tw.stamp) continue;
        (*tw.checked)[bi] = tw.stamp;
        clipBrush(tw, bsp_->brushes[bi]);
        if (tw.tr->allSolid) return;
    }
}

void Collision::clipBrush(TraceWork& tw, const BspBrush& b) const {
    if (b.numsides <= 0) return;
    float enterFrac = -1.0f, leaveFrac = 1.0f;
    const BspPlane* clip = nullptr;
    bool getout = false, startout = false;
    for (int s = 0; s < b.numsides; ++s) {
        int si = b.firstside + s;
        if (si < 0 || si >= (int)bsp_->brushsides.size()) return;
        int pn = bsp_->brushsides[si].planenum;
        if (pn >= (int)bsp_->planes.size()) return;
        const BspPlane& pl = bsp_->planes[pn];
        float dist = pl.dist + std::fabs(pl.normal.x) * tw.ext.x + std::fabs(pl.normal.y) * tw.ext.y + std::fabs(pl.normal.z) * tw.ext.z;
        float d1 = dot(tw.start, pl.normal) - dist;
        float d2 = dot(tw.end, pl.normal) - dist;
        if (d2 > 0) getout = true;
        if (d1 > 0) startout = true;
        if (d1 > 0 && (d2 >= DIST_EPS || d2 >= d1)) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) {
            float f = (d1 - DIST_EPS) / (d1 - d2);
            if (f < 0) f = 0;
            if (f > enterFrac) { enterFrac = f; clip = &pl; }
        } else {
            float f = (d1 + DIST_EPS) / (d1 - d2);
            if (f > 1) f = 1;
            if (f < leaveFrac) leaveFrac = f;
        }
    }
    if (!startout) {
        tw.tr->startSolid = true;
        if (!getout) { tw.tr->allSolid = true; tw.tr->fraction = 0; }
        return;
    }
    if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tw.tr->fraction && clip) {
        tw.tr->fraction = std::max(0.0f, enterFrac);
        tw.tr->normal = clip->normal;
    }
}

bool Collision::dispFloor(float x, float y, float zTop, float zBottom, float& outZ, Vec3& outNormal) const {
    auto it = grid_.find(cellKey((int)std::floor(x / kCell), (int)std::floor(y / kCell)));
    if (it == grid_.end()) return false;
    bool found = false;
    float best = zBottom;
    for (uint32_t t : it->second) {
        const Vec3 &a = dispTris_[t], &b = dispTris_[t + 1], &c = dispTris_[t + 2];
        // barycentric di bidang XY
        float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::fabs(d) < 1e-6f) continue;
        float l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
        float l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
        float l3 = 1 - l1 - l2;
        if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
        float z = l1 * a.z + l2 * b.z + l3 * c.z;
        if (z > zTop || z < best) continue;
        Vec3 n = normalize(cross(b - a, c - a));
        if (n.z < 0) n = n * -1.0f;
        best = z;
        outNormal = n;
        found = true;
    }
    if (found) outZ = best;
    return found;
}

} // namespace cs
