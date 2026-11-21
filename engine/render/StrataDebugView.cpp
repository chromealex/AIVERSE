#include "StrataDebugView.h"

#include <algorithm>
#include <cmath>

namespace aiverse::render::strata {
namespace {

float clamp01(float v) { return std::isfinite(v) ? std::min(1.f, std::max(0.f, v)) : 0.f; }

// HSV (s=0.75, v=0.95) to RGB, hue in [0,1).
uint32_t hueColor(float hue, float alpha) {
    hue = hue - std::floor(hue);
    const float s = .75f, v = .95f;
    const float h = hue * 6.f;
    const int sector = int(h) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
    float r = v, g = t, b = p;
    switch (sector) {
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    default: break;
    }
    return debugPackRgba(r, g, b, alpha);
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Coarse-to-fine ramp: red (root) -> yellow -> green -> cyan -> blue (leaf).
uint32_t rampColor(float t, float alpha) { return hueColor(clamp01(t) * (2.f / 3.f), alpha); }

bool takeLine(const DebugViewOptions& o, DebugDrawStats& s) {
    if (s.lines >= o.maxLines) { ++s.truncated; return false; }
    ++s.lines;
    return true;
}
bool takeTriangle(const DebugViewOptions& o, DebugDrawStats& s) {
    if (s.triangles >= o.maxTriangles) { ++s.truncated; return false; }
    ++s.triangles;
    return true;
}

struct Hierarchy {
    std::vector<uint32_t> depth;
    uint32_t maxDepth = 0;
    float maxError = 0.f;
};

bool buildHierarchy(const Asset& g, Hierarchy& h) {
    h.depth.assign(g.nodes.size(), invalidNode);
    std::vector<uint32_t> pending(g.roots.begin(), g.roots.end());
    for (const uint32_t root : g.roots) {
        if (root >= g.nodes.size()) return false;
        h.depth[root] = 0;
    }
    // Bounded walk: every node is visited at most once (malformed DAGs stop).
    while (!pending.empty()) {
        const uint32_t n = pending.back();
        pending.pop_back();
        const auto& node = g.nodes[n];
        if (node.childCount && (node.firstChild >= g.nodes.size() ||
                                node.childCount > g.nodes.size() - node.firstChild))
            return false;
        for (uint32_t c = 0; c < node.childCount; ++c) {
            const uint32_t child = node.firstChild + c;
            if (h.depth[child] != invalidNode) continue;
            h.depth[child] = h.depth[n] + 1;
            h.maxDepth = std::max(h.maxDepth, h.depth[child]);
            pending.push_back(child);
        }
    }
    for (const auto& node : g.nodes)
        if (std::isfinite(node.geometricError))
            h.maxError = std::max(h.maxError, node.geometricError);
    return true;
}

} // namespace

bool validDebugViewOptions(const DebugViewOptions& o) {
    return std::isfinite(o.fillAlpha) && o.fillAlpha >= 0.f && o.fillAlpha <= 1.f &&
           o.maxLines <= 4u * 1024u * 1024u && o.maxTriangles <= 2u * 1024u * 1024u &&
           uint8_t(o.color) <= uint8_t(DebugColorMode::Material);
}

bool captureDebugGeometry(const Model& source, DebugGeometry& out) {
    if (source.vertices.empty()) return false; // already uploaded/released
    DebugGeometry captured;
    captured.positions.reserve(source.vertices.size());
    for (const auto& v : source.vertices) captured.positions.push_back(v.pos);
    out = std::move(captured);
    return true;
}

uint32_t debugPackRgba(float r, float g, float b, float a) {
    const auto q = [](float v) { return uint32_t(std::lround(clamp01(v) * 255.f)); };
    return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

uint32_t debugRouteColor(DebugRoute route) {
    switch (route) {
    case DebugRoute::StrataPortable: return debugPackRgba(.20f, .95f, .35f);
    case DebugRoute::StrataVulkanGpu: return debugPackRgba(.15f, .80f, 1.f);
    case DebugRoute::LegacyFallback: return debugPackRgba(1.f, .55f, .10f);
    case DebugRoute::Legacy: break;
    }
    return debugPackRgba(.55f, .55f, .60f, .8f);
}

uint32_t debugClusterColor(DebugColorMode mode, uint32_t primitive, uint32_t node,
                           uint32_t depth, uint32_t maxDepth, float error,
                           float maxError, float alpha) {
    switch (mode) {
    case DebugColorMode::LodDepth:
        return rampColor(maxDepth ? float(depth) / float(maxDepth) : 1.f, alpha);
    case DebugColorMode::Error: {
        // Low error (fine) = blue, high error (coarse) = red, log scale.
        const float t = maxError > 0.f && error > 0.f
            ? std::log1p(error * 1000.f / maxError) / std::log1p(1000.f) : 0.f;
        return rampColor(1.f - t, alpha);
    }
    case DebugColorMode::Material:
        return hueColor(float(hash32(primitive * 0x9e3779b9u + 1u) & 0xffffu) / 65536.f, alpha);
    case DebugColorMode::Cluster: break;
    }
    return hueColor(float(hash32(node * 0x85ebca6bu ^ (primitive + 0x27d4eb2fu)) & 0xffffu) / 65536.f,
                    alpha);
}

void emitDebugBox(DebugSink& sink, const mat4& world, float3 mn, float3 mx, uint32_t rgba,
                  const DebugViewOptions& options, DebugDrawStats& stats) {
    float3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = world.transformPoint({(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y,
                                     (i & 4) ? mx.z : mn.z});
    static constexpr int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                         {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges)
        if (takeLine(options, stats)) sink.line(c[e[0]], c[e[1]], rgba);
}

void emitSelectedClusters(DebugSink& sink, const ModelAsset& asset, const Model& source,
                          const DebugGeometry* geometry, const mat4& instanceWorld,
                          const std::vector<SelectedCluster>& selection,
                          const DebugViewOptions& options, DebugDrawStats& stats) {
    if (!options.enabled || selection.empty()) return;
    std::vector<Hierarchy> hierarchies(asset.primitives.size());
    std::vector<uint8_t> hierarchyReady(asset.primitives.size(), 0);
    const bool needTriangles = options.clusterWireframe || options.clusterFill;
    for (const auto& pick : selection) {
        if (pick.primitive >= asset.primitives.size()) continue;
        const auto& entry = asset.primitives[pick.primitive];
        const auto& g = entry.geometry;
        if (pick.node >= g.nodes.size() || pick.node >= g.bounds.size()) continue;
        auto& h = hierarchies[pick.primitive];
        if (!hierarchyReady[pick.primitive]) {
            if (!buildHierarchy(g, h)) h = {};
            hierarchyReady[pick.primitive] = 1;
        }
        const auto& node = g.nodes[pick.node];
        const uint32_t depth = pick.node < h.depth.size() && h.depth[pick.node] != invalidNode
            ? h.depth[pick.node] : 0;
        const uint32_t lineColor = debugClusterColor(options.color, pick.primitive, pick.node,
                                                     depth, h.maxDepth, node.geometricError,
                                                     h.maxError, 1.f);
        const uint32_t fillColor = debugClusterColor(options.color, pick.primitive, pick.node,
                                                     depth, h.maxDepth, node.geometricError,
                                                     h.maxError, options.fillAlpha);
        const bool haveTriangles = geometry && !geometry->positions.empty() &&
            !g.indices.empty() && uint64_t(node.firstIndex) + node.indexCount <= g.indices.size();
        if (needTriangles && !haveTriangles) ++stats.wireframeUnavailable;
        ++stats.clusters;
        const auto& b = g.bounds[pick.node];
        for (size_t n = 0; n < source.nodes.size() && n < source.bindWorld.size(); ++n) {
            if (source.nodes[n].mesh != int(entry.mesh)) continue;
            const mat4 world = instanceWorld * source.bindWorld[n];
            if (options.clusterBounds)
                emitDebugBox(sink, world, {b.min[0], b.min[1], b.min[2]},
                             {b.max[0], b.max[1], b.max[2]}, lineColor, options, stats);
            if (!needTriangles || !haveTriangles) continue;
            const auto& positions = geometry->positions;
            for (uint32_t t = 0; t + 2 < node.indexCount; t += 3) {
                const uint32_t i0 = g.indices[node.firstIndex + t];
                const uint32_t i1 = g.indices[node.firstIndex + t + 1];
                const uint32_t i2 = g.indices[node.firstIndex + t + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    continue;
                const float3 a = world.transformPoint(positions[i0]);
                const float3 c = world.transformPoint(positions[i1]);
                const float3 d = world.transformPoint(positions[i2]);
                if (options.clusterFill && takeTriangle(options, stats))
                    sink.triangle(a, c, d, fillColor);
                if (options.clusterWireframe) {
                    if (takeLine(options, stats)) sink.line(a, c, lineColor);
                    if (takeLine(options, stats)) sink.line(c, d, lineColor);
                    if (takeLine(options, stats)) sink.line(d, a, lineColor);
                }
            }
        }
    }
}

} // namespace aiverse::render::strata
