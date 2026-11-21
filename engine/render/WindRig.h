// AIVERSE hierarchical wind masks (2026-11-21).
//
// How strongly each part of an object sways is described by MASKS: small 1D
// curves along a part (0 = attachment, 1 = tip). Parts form a HIERARCHY (trunk
// -> branch -> twig -> leaf, or mast -> flag, pole -> cable ...). A child part
// moves rigidly with its parent at its attachment point and adds its own
// masked bend on top, so a branch sways with the trunk and whips on its own.
//
// Reuse, from cheap to free:
//   * WindMaskLibrary  - named curves, deduplicated by baked content. One GPU
//                        atlas (WindMaskAtlas) for the whole game; every rig
//                        and every object references masks by index.
//   * WindProfile      - per-hierarchy-level defaults (mask, stiffness, flex):
//                        one profile serves a whole species / object class.
//   * WindRig          - per PROTOTYPE mesh (all instances share it); nodes
//                        only store geometry + optional overrides.
// GPU cost: 16 bytes per rig node (+4 header), 4 bytes per vertex (the vertex
// colour: node index 16 bit, leaf flutter 8 bit, legacy weight 8 bit).
// Masks: 32 samples = 32 bytes each, shared by everything.
//
// This header is pure CPU (no RHI): authoring, baking, packing and the exact
// reference evaluation that the GLSL/MSL in WindRigShader.h mirrors.
// Presentation only: never feed into authoritative simulation.
#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render::wind {

inline constexpr int kMaskSamples = 32;          // per mask, uint8
inline constexpr int kMaskTexels = kMaskSamples / 4;
inline constexpr int kTextureWidth = 64;         // texels per row (atlas and rig)
inline constexpr int kMaxLevels = 4;             // hierarchy depth evaluated per vertex
inline constexpr int kMaxMasks = 256;
inline constexpr uint32_t kMaxNodes = 65534;
inline constexpr uint16_t kNoNode = 0xffff;

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float f) { return {a.x * f, a.y * f, a.z * f}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }

// ------------------------------------------------------------------ masks

using MaskSamples = std::array<uint8_t, kMaskSamples>;

// Piecewise-linear curve through (t, value) points, t and value in [0,1].
inline MaskSamples bakeMaskCurve(std::vector<std::pair<float, float>> points) {
    if (points.empty()) throw std::invalid_argument("wind mask needs at least one point");
    for (const auto& [t, v] : points)
        if (!std::isfinite(t) || !std::isfinite(v) || t < 0 || t > 1 || v < 0 || v > 1)
            throw std::invalid_argument("wind mask points must be in [0,1]");
    std::stable_sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    MaskSamples out{};
    for (int i = 0; i < kMaskSamples; ++i) {
        const float t = float(i) / float(kMaskSamples - 1);
        float v = points.front().second;
        if (t >= points.back().first) v = points.back().second;
        else for (size_t k = 1; k < points.size(); ++k)
            if (t <= points[k].first) {
                const auto& a = points[k - 1]; const auto& b = points[k];
                const float span = b.first - a.first;
                v = span > 1e-6f ? a.second + (b.second - a.second) * (t - a.first) / span : b.second;
                break;
            }
        out[size_t(i)] = uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
    }
    return out;
}

inline float sampleMask(const MaskSamples& m, float t) {
    const float f = std::clamp(t, 0.f, 1.f) * float(kMaskSamples - 1);
    const int i0 = int(std::floor(f));
    const int i1 = std::min(i0 + 1, kMaskSamples - 1);
    const float a = float(m[size_t(i0)]) / 255.f, b = float(m[size_t(i1)]) / 255.f;
    return a + (b - a) * (f - float(i0));
}

// Named, content-deduplicated masks. Index 0 is always "rigid" (all zero).
class WindMaskLibrary {
public:
    WindMaskLibrary() { addBuiltins(); }

    // Returns the index of an identical existing mask when one exists, so
    // "trunk" and a project's "pine_trunk" with the same curve share a row.
    uint8_t add(const std::string& name, const MaskSamples& samples) {
        if (name.empty()) throw std::invalid_argument("wind mask name");
        if (auto it = byName_.find(name); it != byName_.end()) {
            if (masks_[it->second] != samples)
                throw std::invalid_argument("wind mask '" + name + "' redefined with a different curve");
            return it->second;
        }
        for (size_t i = 0; i < masks_.size(); ++i)
            if (masks_[i] == samples) { byName_[name] = uint8_t(i); return uint8_t(i); }
        if (masks_.size() >= size_t(kMaxMasks)) throw std::length_error("wind mask library full (256)");
        masks_.push_back(samples);
        byName_[name] = uint8_t(masks_.size() - 1);
        ++revision_;
        return uint8_t(masks_.size() - 1);
    }
    uint8_t addCurve(const std::string& name, std::vector<std::pair<float, float>> points) {
        return add(name, bakeMaskCurve(std::move(points)));
    }
    std::optional<uint8_t> find(const std::string& name) const {
        auto it = byName_.find(name);
        if (it == byName_.end()) return std::nullopt;
        return it->second;
    }
    uint8_t require(const std::string& name) const {
        if (auto m = find(name)) return *m;
        throw std::invalid_argument("unknown wind mask '" + name + "'");
    }
    const MaskSamples& mask(uint8_t index) const {
        if (index >= masks_.size()) throw std::out_of_range("wind mask index");
        return masks_[index];
    }
    size_t size() const { return masks_.size(); }
    size_t names() const { return byName_.size(); }
    std::vector<std::pair<std::string, uint8_t>> namedMasks() const {
        return {byName_.begin(), byName_.end()};
    }
    uint64_t revision() const { return revision_; }

    // {"masks": {"name": [[t, v], ...], ...}}  (project file wind/masks.json)
    void loadJson(const Json& j) {
        const Json& masks = j.isObject() && j.has("masks") ? j["masks"] : j;
        if (!masks.isObject()) throw std::invalid_argument("wind masks must be an object of curves");
        for (const auto& [name, curve] : masks.members()) {
            if (!curve.isArray() || curve.items().empty())
                throw std::invalid_argument("wind mask '" + name + "' must be [[t,v],...]");
            std::vector<std::pair<float, float>> pts;
            for (const auto& p : curve.items()) {
                if (!p.isArray() || p.items().size() != 2)
                    throw std::invalid_argument("wind mask '" + name + "' point must be [t,v]");
                pts.emplace_back(float(p[size_t(0)].toDouble(-1)), float(p[size_t(1)].toDouble(-1)));
            }
            addCurve(name, std::move(pts));
        }
    }

    // Atlas bytes: RGBA8, width 64 texels, 8 texels (32 samples) per mask.
    void packAtlas(std::vector<uint8_t>& rgba, int& width, int& height) const {
        width = kTextureWidth;
        const size_t texels = masks_.size() * size_t(kMaskTexels);
        height = int(std::max<size_t>(1, (texels + kTextureWidth - 1) / kTextureWidth));
        rgba.assign(size_t(width) * size_t(height) * 4u, 0);
        for (size_t m = 0; m < masks_.size(); ++m)
            for (int s = 0; s < kMaskSamples; ++s)
                rgba[(m * size_t(kMaskTexels)) * 4u + size_t(s)] = masks_[m][size_t(s)];
    }

private:
    void addBuiltins() {
        addCurve("rigid", {{0, 0}, {1, 0}});
        addCurve("linear", {{0, 0}, {1, 1}});
        // Trunk: stiff base, bends mostly in the upper part.
        addCurve("trunk", {{0, 0}, {0.25f, 0.03f}, {0.6f, 0.3f}, {1, 1}});
        // Branch: a little at the collar, most at the outer half.
        addCurve("branch", {{0, 0}, {0.15f, 0.05f}, {0.5f, 0.35f}, {1, 1}});
        // Twig: flexible almost from the start.
        addCurve("twig", {{0, 0}, {0.1f, 0.25f}, {0.5f, 0.7f}, {1, 1}});
        // Leaf / card / flag: hinge at the stem, free edge.
        addCurve("leaf", {{0, 0}, {0.05f, 0.5f}, {1, 1}});
        addCurve("tip", {{0, 0}, {0.7f, 0}, {1, 1}});
        addCurve("uniform", {{0, 1}, {1, 1}});
    }
    std::vector<MaskSamples> masks_;
    std::map<std::string, uint8_t> byName_;
    uint64_t revision_ = 0;
};

// --------------------------------------------------------------- profiles

struct WindLevel {
    std::string mask = "linear";
    float stiffness = 0.5f;   // 0 = slow, soft;   1 = fast, stiff (frequency)
    float flex = 1.0f;        // amplitude multiplier 0..2
};

// Per hierarchy level defaults (level 0 = root part). Shared by every rig of
// a species / class; nodes override only what differs.
struct WindProfile {
    std::vector<WindLevel> levels{{"trunk", 0.85f, 0.35f}, {"branch", 0.55f, 0.75f},
                                  {"twig", 0.35f, 1.1f}, {"leaf", 0.2f, 1.3f}};
    float leafFlutter = 1.0f;  // multiplier of per-vertex flutter weight
    const WindLevel& level(int depth) const {
        if (levels.empty()) throw std::invalid_argument("wind profile has no levels");
        return levels[size_t(std::clamp(depth, 0, int(levels.size()) - 1))];
    }
    // {"levels":[{"mask":"trunk","stiffness":0.85,"flex":0.35},...],"leafFlutter":1}
    static WindProfile fromJson(const Json& j) {
        WindProfile p;
        if (!j.isObject()) throw std::invalid_argument("wind profile must be an object");
        if (j.has("levels")) {
            const Json& lv = j["levels"];
            if (!lv.isArray() || lv.items().empty() || lv.items().size() > size_t(kMaxLevels))
                throw std::invalid_argument("wind profile levels: 1..4 entries");
            p.levels.clear();
            for (const auto& l : lv.items()) {
                WindLevel w;
                w.mask = l.getStr("mask", w.mask);
                w.stiffness = float(std::clamp(l["stiffness"].toDouble(w.stiffness), 0.0, 1.0));
                w.flex = float(std::clamp(l["flex"].toDouble(w.flex), 0.0, 2.0));
                p.levels.push_back(w);
            }
        }
        p.leafFlutter = float(std::clamp(j["leafFlutter"].toDouble(p.leafFlutter), 0.0, 4.0));
        return p;
    }
};

// ------------------------------------------------------------------- rigs

struct WindNodeDesc {
    int parent = -1;                  // index into the builder's nodes
    V3 pivot, tip;                    // object space metres: attachment and tip
    std::string mask;                 // empty = profile level default
    std::optional<float> stiffness, flex;
};

struct WindNode {
    V3 pivot, axis{0, 1, 0};
    float length = 1;
    uint16_t parent = kNoNode;
    uint8_t mask = 0, depth = 0;
    float stiffness = 0.5f, flex = 1, phase = 0;
};

struct WindRig {
    std::vector<WindNode> nodes;
    int unitExponent = 10;            // pivot unit = 2^(unitExponent-20) m
    float unitMetres() const { return std::ldexp(1.f, unitExponent - 20); }
};

inline uint64_t windHash(uint64_t v) {
    v += 0x9e3779b97f4a7c15ull;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ull;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebull;
    return v ^ (v >> 31);
}

// Resolve descriptors against a profile and library. Nodes must be listed
// parents first. Depth >= kMaxLevels is clamped: deeper parts inherit the
// motion of their 4-level chain (bounded GPU loop).
inline WindRig buildWindRig(const std::vector<WindNodeDesc>& desc, const WindProfile& profile,
                            const WindMaskLibrary& masks, uint64_t seed = 1) {
    if (desc.empty()) throw std::invalid_argument("wind rig needs nodes");
    if (desc.size() > kMaxNodes) throw std::length_error("wind rig node limit 65534");
    WindRig rig;
    rig.nodes.resize(desc.size());
    float extent = 0;
    for (size_t i = 0; i < desc.size(); ++i) {
        const auto& d = desc[i];
        if (d.parent >= int(i)) throw std::invalid_argument("wind rig: parents must come first");
        for (float v : {d.pivot.x, d.pivot.y, d.pivot.z, d.tip.x, d.tip.y, d.tip.z})
            if (!std::isfinite(v)) throw std::invalid_argument("wind rig: non-finite node");
        auto& n = rig.nodes[i];
        n.parent = d.parent < 0 ? kNoNode : uint16_t(d.parent);
        n.depth = d.parent < 0 ? 0 : uint8_t(std::min(int(rig.nodes[size_t(d.parent)].depth) + 1, 255));
        const auto& lvl = profile.level(n.depth);
        n.mask = masks.require(d.mask.empty() ? lvl.mask : d.mask);
        n.stiffness = std::clamp(d.stiffness.value_or(lvl.stiffness), 0.f, 1.f);
        n.flex = std::clamp(d.flex.value_or(lvl.flex), 0.f, 2.f);
        n.pivot = d.pivot;
        const V3 delta = d.tip - d.pivot;
        n.length = std::max(length(delta), 0.01f);
        n.axis = length(delta) > 1e-5f ? delta * (1.f / length(delta)) : V3{0, 1, 0};
        n.phase = float(windHash(seed ^ (uint64_t(i) * 0x9e3779b97f4a7c15ull)) % 1000u) / 1000.f;
        extent = std::max({extent, std::fabs(d.pivot.x), std::fabs(d.pivot.y), std::fabs(d.pivot.z),
                           n.length});
    }
    // Smallest unit so that every pivot fits int16 and every length uint16.
    rig.unitExponent = 0;
    while (rig.unitExponent < 40 && extent / rig.unitMetres() > 32000.f) ++rig.unitExponent;
    if (extent / rig.unitMetres() > 32000.f) throw std::out_of_range("wind rig extent");
    return rig;
}

// ------------------------------------------------------------- packing

// Octahedral unit vector <-> 2 x uint8.
inline std::pair<uint8_t, uint8_t> encodeOct(V3 n) {
    const float s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    float u = n.x / s, v = n.z / s;
    if (n.y < 0) {
        const float ou = (1 - std::fabs(v)) * (u >= 0 ? 1.f : -1.f);
        const float ov = (1 - std::fabs(u)) * (v >= 0 ? 1.f : -1.f);
        u = ou; v = ov;
    }
    const auto q = [](float x) { return uint8_t(std::lround((std::clamp(x, -1.f, 1.f) * .5f + .5f) * 255.f)); };
    return {q(u), q(v)};
}
inline V3 decodeOct(float r, float g) {   // r,g = normalized 0..1 channel values
    const float u = r * 2 - 1, v = g * 2 - 1;
    V3 n{u, 1 - std::fabs(u) - std::fabs(v), v};
    if (n.y < 0) {
        const float ou = (1 - std::fabs(n.z)) * (n.x >= 0 ? 1.f : -1.f);
        const float ov = (1 - std::fabs(n.x)) * (n.z >= 0 ? 1.f : -1.f);
        n.x = ou; n.z = ov;
    }
    const float l = std::max(length(n), 1e-6f);
    return n * (1.f / l);
}

// Rig texture: RGBA8, width 64. Texel 0 header, node i at texels 1+4i..4+4i.
//   H : R,G node count (u16) | B unit exponent | A version (1)
//   N0: pivot.x (s16 R lo G hi) | pivot.y (s16 B lo A hi)
//   N1: pivot.z (s16)           | length (u16, units)
//   N2: axis oct u, v           | parent (u16, 0xffff root)
//   N3: mask | stiffness | phase | flex/2
inline void packWindRig(const WindRig& rig, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (rig.nodes.empty() || rig.nodes.size() > kMaxNodes) throw std::invalid_argument("wind rig size");
    width = kTextureWidth;
    const size_t texels = 1 + rig.nodes.size() * 4;
    height = int((texels + kTextureWidth - 1) / kTextureWidth);
    rgba.assign(size_t(width) * size_t(height) * 4u, 0);
    const float unit = rig.unitMetres();
    auto put = [&](size_t texel, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        rgba[texel * 4] = a; rgba[texel * 4 + 1] = b; rgba[texel * 4 + 2] = c; rgba[texel * 4 + 3] = d;
    };
    auto s16 = [&](float metres) {
        const long q = std::lround(metres / unit);
        return uint16_t(int16_t(std::clamp<long>(q, -32767, 32767)));
    };
    const auto n = uint16_t(rig.nodes.size());
    put(0, uint8_t(n & 255), uint8_t(n >> 8), uint8_t(rig.unitExponent), 1);
    for (size_t i = 0; i < rig.nodes.size(); ++i) {
        const auto& w = rig.nodes[i];
        const size_t t = 1 + i * 4;
        const uint16_t px = s16(w.pivot.x), py = s16(w.pivot.y), pz = s16(w.pivot.z);
        const uint16_t len = uint16_t(std::clamp<long>(std::lround(w.length / unit), 1, 65535));
        const auto [ou, ov] = encodeOct(w.axis);
        put(t, uint8_t(px & 255), uint8_t(px >> 8), uint8_t(py & 255), uint8_t(py >> 8));
        put(t + 1, uint8_t(pz & 255), uint8_t(pz >> 8), uint8_t(len & 255), uint8_t(len >> 8));
        put(t + 2, ou, ov, uint8_t(w.parent & 255), uint8_t(w.parent >> 8));
        put(t + 3, w.mask, uint8_t(std::lround(w.stiffness * 255)), uint8_t(std::lround(w.phase * 255)),
            uint8_t(std::lround(std::clamp(w.flex * 0.5f, 0.f, 1.f) * 255)));
    }
}

// Inverse of packWindRig (quantized values). For tools, debug views and
// tests; the GPU reads the same bytes.
inline WindRig decodeWindRig(const std::vector<uint8_t>& rgba, int width) {
    if (width != kTextureWidth || rgba.size() < 4) throw std::invalid_argument("wind rig texture");
    WindRig rig;
    const uint32_t count = uint32_t(rgba[0]) | (uint32_t(rgba[1]) << 8);
    if (rgba[3] != 1 || count == 0 || (1 + size_t(count) * 4) * 4 > rgba.size())
        throw std::invalid_argument("wind rig texture header");
    rig.unitExponent = rgba[2];
    const float unit = rig.unitMetres();
    const auto at = [&](size_t texel, int c) { return rgba[texel * 4 + size_t(c)]; };
    const auto s16 = [&](uint8_t lo, uint8_t hi) {
        const int v = int(lo) | (int(hi) << 8);
        return float(v >= 32768 ? v - 65536 : v);
    };
    rig.nodes.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t t = 1 + size_t(i) * 4;
        auto& n = rig.nodes[i];
        n.pivot = {s16(at(t, 0), at(t, 1)) * unit, s16(at(t, 2), at(t, 3)) * unit,
                   s16(at(t + 1, 0), at(t + 1, 1)) * unit};
        n.length = std::max(float(int(at(t + 1, 2)) | (int(at(t + 1, 3)) << 8)) * unit, 0.01f);
        n.axis = decodeOct(at(t + 2, 0) / 255.f, at(t + 2, 1) / 255.f);
        n.parent = uint16_t(int(at(t + 2, 2)) | (int(at(t + 2, 3)) << 8));
        n.mask = at(t + 3, 0);
        n.stiffness = at(t + 3, 1) / 255.f;
        n.phase = at(t + 3, 2) / 255.f;
        n.flex = at(t + 3, 3) / 255.f * 2.f;
        n.depth = n.parent < i ? uint8_t(std::min(int(rig.nodes[n.parent].depth) + 1, 255)) : 0;
    }
    return rig;
}

// Per-vertex payload, written into ModelVertex::color (RGBA8).
//   R,G node index (u16) | B leaf flutter 0..255 | A legacy sway weight
// The material must ignore vertex colours (Material::vertexColors=false).
inline uint32_t encodeWindVertex(uint16_t node, float flutter, float legacyWeight) {
    const uint32_t f = uint32_t(std::lround(std::clamp(flutter, 0.f, 1.f) * 255));
    const uint32_t a = uint32_t(std::lround(std::clamp(legacyWeight, 0.f, 1.f) * 255));
    return uint32_t(node & 255) | (uint32_t(node >> 8) << 8) | (f << 16) | (a << 24);
}

// Per-draw input, identical to the existing canopy convention (I.anim):
// object-local wind displacement (metres at full weight) x/z, phase, gust.
struct WindDrawParams { float windX = 0, windZ = 0, phase = 0, gust = 0; };

// ------------------------------------------------- reference evaluation
// Decodes the PACKED bytes exactly like the shader (WindRigShader.h).
class PackedWindRig {
public:
    PackedWindRig(const std::vector<uint8_t>& rig, int rigWidth,
                  const std::vector<uint8_t>& atlas, int atlasWidth)
        : rig_(rig), atlas_(atlas), rw_(rigWidth), aw_(atlasWidth) {}

    // Returns the object-space offset of a vertex.
    V3 offset(V3 p, V3 normal, uint32_t vertexColor, const WindDrawParams& w) const {
        uint32_t node = (vertexColor & 255u) | (((vertexColor >> 8) & 255u) << 8);
        const float flutter = float((vertexColor >> 16) & 255u) / 255.f;
        const auto header = texel(rig_, rw_, 0);
        const uint32_t count = b(header[0]) | (b(header[1]) << 8);
        const float unit = std::ldexp(1.f, int(b(header[2])) - 20);
        const V3 W{w.windX, 0, w.windZ};
        const float strength = length(W);
        V3 q = p, off{};
        for (int level = 0; level < kMaxLevels; ++level) {
            if (node >= count) break;
            const int base = 1 + int(node) * 4;
            const auto n0 = texel(rig_, rw_, base), n1 = texel(rig_, rw_, base + 1);
            const auto n2 = texel(rig_, rw_, base + 2), n3 = texel(rig_, rw_, base + 3);
            const V3 pivot{s16(n0[0], n0[1]) * unit, s16(n0[2], n0[3]) * unit, s16(n1[0], n1[1]) * unit};
            const float len = std::max(float(b(n1[2]) | (b(n1[3]) << 8)) * unit, 0.01f);
            const V3 axis = decodeOct(n2[0], n2[1]);
            const uint32_t parent = b(n2[2]) | (b(n2[3]) << 8);
            const uint32_t mask = b(n3[0]);
            const float stiff = n3[1], phase = n3[2], flex = n3[3] * 2.f;
            const float t = std::clamp(dot(q - pivot, axis) / len, 0.f, 1.f);
            const float m = maskAt(mask, t);
            const V3 perp = W - axis * dot(W, axis);
            const float ph = w.phase * (0.6f + 1.4f * stiff) + phase * 6.2831853f;
            const float wave = 0.62f + 0.38f * std::sin(ph) + w.gust * 0.25f * std::sin(ph * 2.13f + 1.7f);
            V3 d = perp * (m * flex * wave);
            const float side = 1 - std::fabs(axis.y);
            d.y += strength * m * flex * 0.25f * side * std::sin(ph * 1.31f + 0.9f);
            // Keep the part's length: swing on an arc, not a shear.
            d = d - axis * (dot(d, d) * 0.5f / len);
            off = off + d;
            q = pivot;                       // parent evaluated at the attachment
            node = parent;
        }
        const float fl = flutter * strength * 0.08f *
            std::sin(w.phase * 6.7f + dot(p, V3{3.1f, 1.7f, 2.3f}));
        return off + normal * fl;
    }

private:
    static uint32_t b(float v) { return uint32_t(std::lround(v * 255.f)); }
    static float s16(float lo, float hi) {
        const int v = int(b(lo) | (b(hi) << 8));
        return float(v >= 32768 ? v - 65536 : v);
    }
    static std::array<float, 4> texel(const std::vector<uint8_t>& px, int width, int index) {
        const size_t at = size_t(index) * 4u;
        if (width <= 0 || at + 3 >= px.size()) return {0, 0, 0, 0};
        return {px[at] / 255.f, px[at + 1] / 255.f, px[at + 2] / 255.f, px[at + 3] / 255.f};
    }
    float maskAt(uint32_t mask, float t) const {
        const float f = t * float(kMaskSamples - 1);
        const int i0 = int(std::floor(f)), i1 = std::min(i0 + 1, kMaskSamples - 1);
        const auto a = texel(atlas_, aw_, int(mask) * kMaskTexels + i0 / 4)[size_t(i0 & 3)];
        const auto c = texel(atlas_, aw_, int(mask) * kMaskTexels + i1 / 4)[size_t(i1 & 3)];
        return a + (c - a) * (f - float(i0));
    }
    const std::vector<uint8_t>& rig_;
    const std::vector<uint8_t>& atlas_;
    int rw_, aw_;
};

// Content key so identical rigs share one GPU texture.
inline uint64_t windTextureKey(const std::vector<uint8_t>& rgba) {
    uint64_t h = 0x57494e4452494721ull;
    for (size_t i = 0; i < rgba.size(); i += 8) {
        uint64_t chunk = 0;
        for (size_t k = 0; k < 8 && i + k < rgba.size(); ++k) chunk |= uint64_t(rgba[i + k]) << (8 * k);
        h = windHash(h ^ chunk);
    }
    return h;
}

} // namespace aiverse::render::wind
