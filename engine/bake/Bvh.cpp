#include "Bvh.h"
#include <algorithm>
#include <cmath>

namespace aiverse::bake {
namespace {
struct Box {
    float3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    void add(float3 p) { mn = render::min3(mn, p); mx = render::max3(mx, p); }
    void add(const Box& b) { mn = render::min3(mn, b.mn); mx = render::max3(mx, b.mx); }
    float area() const {
        if (mx.x < mn.x) return 0;
        const float3 d = mx - mn;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
};
float axis(float3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
struct Ref { Box box; float3 c; uint32_t tri; };
constexpr int kBins = 12, kLeaf = 4;
} // namespace

void Bvh::build(std::vector<BvhTri> tris) {
    tris_ = std::move(tris);
    nodes_.clear();
    if (tris_.empty()) return;
    std::vector<Ref> refs(tris_.size());
    for (size_t i = 0; i < tris_.size(); ++i) {
        const auto& t = tris_[i];
        Ref& r = refs[i];
        r.box.add(t.v0); r.box.add(t.v0 + t.e1); r.box.add(t.v0 + t.e2);
        r.c = (r.box.mn + r.box.mx) * 0.5f;
        r.tri = uint32_t(i);
    }
    nodes_.reserve(tris_.size() * 2);
    struct Task { uint32_t node, begin, end; };
    std::vector<Task> stack;
    nodes_.push_back({});
    stack.push_back({0, 0, uint32_t(refs.size())});
    while (!stack.empty()) {
        const Task task = stack.back(); stack.pop_back();
        Box box, cbox;
        for (uint32_t i = task.begin; i < task.end; ++i) { box.add(refs[i].box); cbox.add(refs[i].c); }
        Node& node = nodes_[task.node];
        node.bmin[0] = box.mn.x; node.bmin[1] = box.mn.y; node.bmin[2] = box.mn.z;
        node.bmax[0] = box.mx.x; node.bmax[1] = box.mx.y; node.bmax[2] = box.mx.z;
        const uint32_t n = task.end - task.begin;
        int bestAxis = -1, bestSplit = 0;
        float bestCost = float(n);   // leaf cost (intersection = 1, traversal = 1 relative to area)
        if (n > uint32_t(kLeaf)) {
            for (int a = 0; a < 3; ++a) {
                const float lo = axis(cbox.mn, a), hi = axis(cbox.mx, a);
                if (hi - lo < 1e-9f) continue;
                Box bins[kBins]; uint32_t cnt[kBins] = {};
                const float k = float(kBins) / (hi - lo);
                for (uint32_t i = task.begin; i < task.end; ++i) {
                    const int b = std::min(kBins - 1, int((axis(refs[i].c, a) - lo) * k));
                    bins[b].add(refs[i].box); ++cnt[b];
                }
                float rightArea[kBins]; uint32_t rightCnt[kBins];
                Box acc; uint32_t c = 0;
                for (int b = kBins - 1; b > 0; --b) { acc.add(bins[b]); c += cnt[b]; rightArea[b] = acc.area(); rightCnt[b] = c; }
                Box left; uint32_t lc = 0;
                const float inv = 1.0f / std::max(box.area(), 1e-20f);
                for (int b = 0; b < kBins - 1; ++b) {
                    left.add(bins[b]); lc += cnt[b];
                    if (!lc || !rightCnt[b + 1]) continue;
                    const float cost = 0.5f + (left.area() * float(lc) + rightArea[b + 1] * float(rightCnt[b + 1])) * inv;
                    if (cost < bestCost) { bestCost = cost; bestAxis = a; bestSplit = b; }
                }
            }
        }
        uint32_t mid = 0;
        if (bestAxis >= 0) {
            const float lo = axis(cbox.mn, bestAxis), k = float(kBins) / (axis(cbox.mx, bestAxis) - lo);
            auto it = std::partition(refs.begin() + task.begin, refs.begin() + task.end, [&](const Ref& r) {
                return std::min(kBins - 1, int((axis(r.c, bestAxis) - lo) * k)) <= bestSplit;
            });
            mid = uint32_t(it - refs.begin());
        } else if (n > 64) {   // degenerate centroids, too many for one leaf: median split
            mid = task.begin + n / 2;
        }
        if (mid <= task.begin || mid >= task.end) {
            node.first = task.begin; node.count = n;
            continue;
        }
        const uint32_t left = uint32_t(nodes_.size());
        nodes_.push_back({}); nodes_.push_back({});
        nodes_[task.node].first = left; nodes_[task.node].count = 0;
        stack.push_back({left + 1, mid, task.end});
        stack.push_back({left, task.begin, mid});
    }
    // leaves index tris_ directly: reorder the triangles by reference order
    std::vector<BvhTri> ordered(tris_.size());
    for (size_t i = 0; i < refs.size(); ++i) ordered[i] = tris_[refs[i].tri];
    tris_ = std::move(ordered);
}

template <bool Any> bool Bvh::trace(float3 org, float3 dir, float tmax, BvhHit* hit) const {
    if (nodes_.empty()) return false;
    const float inv[3] = {1.0f / (std::fabs(dir.x) > 1e-12f ? dir.x : 1e-12f), 1.0f / (std::fabs(dir.y) > 1e-12f ? dir.y : 1e-12f),
                          1.0f / (std::fabs(dir.z) > 1e-12f ? dir.z : 1e-12f)};
    const float o[3] = {org.x, org.y, org.z};
    uint32_t stack[64]; int sp = 0;
    stack[sp++] = 0;
    bool found = false;
    float best = tmax;
    auto slab = [&](const Node& n, float& tn) {
        float t0 = 0, t1 = best;
        for (int a = 0; a < 3; ++a) {
            float ta = (n.bmin[a] - o[a]) * inv[a], tb = (n.bmax[a] - o[a]) * inv[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb * 1.0000004f);
        }
        tn = t0;
        return t0 <= t1;
    };
    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];
        if (n.count) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const BvhTri& t = tris_[i];
                const float3 p = render::cross(dir, t.e2);
                const float det = render::dot(t.e1, p);
                if (std::fabs(det) < 1e-12f) continue;
                const float id = 1.0f / det;
                const float3 s = org - t.v0;
                const float u = render::dot(s, p) * id;
                if (u < 0 || u > 1) continue;
                const float3 q = render::cross(s, t.e1);
                const float v = render::dot(dir, q) * id;
                if (v < 0 || u + v > 1) continue;
                const float tt = render::dot(t.e2, q) * id;
                if (tt <= 0 || tt >= best) continue;
                if (!masked_.empty() && t.id < masked_.size() && masked_[t.id] && alphaTest_ && !alphaTest_(t.id, u, v)) continue;
                if constexpr (Any) return true;
                best = tt; found = true;
                hit->t = tt; hit->u = u; hit->v = v; hit->tri = t.id; hit->backface = det < 0;
            }
            continue;
        }
        float ta, tb;
        const bool ha = slab(nodes_[n.first], ta), hb = slab(nodes_[n.first + 1], tb);
        if (ha && hb) {
            if (sp > 62) continue;   // pathological depth: skip (never with SAH on real scenes)
            if (ta < tb) { stack[sp++] = n.first + 1; stack[sp++] = n.first; }
            else { stack[sp++] = n.first; stack[sp++] = n.first + 1; }
        } else if (ha) { if (sp < 64) stack[sp++] = n.first; }
        else if (hb) { if (sp < 64) stack[sp++] = n.first + 1; }
    }
    return found;
}

bool Bvh::intersect(float3 org, float3 dir, float tmax, BvhHit& hit) const { return trace<false>(org, dir, tmax, &hit); }
bool Bvh::occluded(float3 org, float3 dir, float tmax) const { return trace<true>(org, dir, tmax, nullptr); }

} // namespace aiverse::bake
