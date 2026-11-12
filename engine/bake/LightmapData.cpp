#include "LightmapData.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../data/Json.h"
#include "../render/Model.h"
#include "Half.h"

namespace aiverse::bake {

const char* bakedModeName(BakedMode m) {
    return m == BakedMode::Indirect ? "indirect" : m == BakedMode::Subtractive ? "subtractive" : "shadowmask";
}
bool parseBakedMode(const std::string& s, BakedMode& out) {
    if (s == "indirect" || s == "baked-indirect") out = BakedMode::Indirect;
    else if (s == "shadowmask") out = BakedMode::Shadowmask;
    else if (s == "subtractive") out = BakedMode::Subtractive;
    else return false;
    return true;
}

uint64_t fnv1a(const void* data, size_t n, uint64_t h) {
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

uint64_t lightmapRegionKey(const std::string& modelPath, int node, const mat4& world) {
    uint64_t h = fnv1a(modelPath.data(), modelPath.size());
    h = fnv1a(&node, sizeof node, h);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r) {
            // 1/256 steps: the same placement recomputed (editor vs game) lands on the same key
            const int32_t q = int32_t(std::lround(double(world.m[c * 4 + r]) * 256.0));
            h = fnv1a(&q, sizeof q, h);
        }
    return h;
}

std::string hex64(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[size_t(i)] = d[v & 15];
    return s;
}
bool parseHex64(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 16) return false;
    v = 0;
    for (char c : s) {
        int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (x < 0) return false;
        v = (v << 4) | uint64_t(x);
    }
    return true;
}

void encodeRgbm(float3 c, float range, uint8_t out[4]) {
    const float r = std::max(0.0f, c.x) / range, g = std::max(0.0f, c.y) / range, b = std::max(0.0f, c.z) / range;
    float m = std::clamp(std::max({r, g, b, 1e-6f}), 0.0f, 1.0f);
    m = std::ceil(m * 255.0f) / 255.0f;
    auto q = [](float x) { return uint8_t(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    out[0] = q(r / m); out[1] = q(g / m); out[2] = q(b / m); out[3] = q(m);
}
float3 decodeRgbm(const uint8_t in[4], float range) {
    const float m = float(in[3]) / 255.0f * range / 255.0f;
    return {float(in[0]) * m, float(in[1]) * m, float(in[2]) * m};
}

float3 LightProbeGrid::irradiance(float3 pos, float3 n) const {
    if (empty() || nx <= 0 || ny <= 0 || nz <= 0) return {};
    float f[3] = {(pos.x - origin.x) / cell.x, (pos.y - origin.y) / cell.y, (pos.z - origin.z) / cell.z};
    const int dims[3] = {nx, ny, nz};
    int i0[3]; float t[3];
    for (int k = 0; k < 3; ++k) {
        f[k] = std::clamp(f[k], 0.0f, float(dims[k] - 1));
        i0[k] = std::min(int(f[k]), std::max(dims[k] - 2, 0));
        t[k] = dims[k] > 1 ? f[k] - float(i0[k]) : 0.0f;
    }
    float acc[12] = {};
    for (int c = 0; c < 8; ++c) {
        int ix = std::min(i0[0] + (c & 1), nx - 1), iy = std::min(i0[1] + ((c >> 1) & 1), ny - 1), iz = std::min(i0[2] + ((c >> 2) & 1), nz - 1);
        float w = ((c & 1) ? t[0] : 1 - t[0]) * (((c >> 1) & 1) ? t[1] : 1 - t[1]) * (((c >> 2) & 1) ? t[2] : 1 - t[2]);
        const float* p = &coeffs[size_t(ix + nx * (iy + ny * iz)) * 12];
        for (int k = 0; k < 12; ++k) acc[k] += p[k] * w;
    }
    auto ch = [&](int c) { return std::max(0.0f, acc[c] + acc[3 + c] * n.x + acc[6 + c] * n.y + acc[9 + c] * n.z); };
    return {ch(0), ch(1), ch(2)};
}

std::string bakeFolderFor(const std::string& mapPath) {
    std::string s = mapPath;
    if (s.size() > 5 && s.compare(s.size() - 5, 5, ".json") == 0) s.resize(s.size() - 5);
    return s + ".bake";
}

namespace {
float num(const Json& j, float def) { return j.isNumber() ? float(j.toDouble(def)) : def; }

bool parseHeader(const std::string& folder, const ReadFn& read, LightmapData& out, std::string& error, Json& j) {
    std::string text;
    if (!read(folder + "/lightmap.json", text)) { error = folder + "/lightmap.json: not found"; return false; }
    try { j = Json::parse(text); } catch (const std::exception& e) { error = folder + "/lightmap.json: " + e.what(); return false; }
    out = LightmapData{};
    out.version = int(j.getInt("version", 0));
    if (out.version != 1) { error = folder + "/lightmap.json: unsupported version " + std::to_string(out.version); return false; }
    if (!parseHex64(j.getStr("hash"), out.hash)) { error = folder + "/lightmap.json: bad 'hash'"; return false; }
    if (!parseBakedMode(j.getStr("mode", "shadowmask"), out.mode)) { error = folder + "/lightmap.json: unknown mode '" + j.getStr("mode") + "'"; return false; }
    out.rgbmRange = std::max(0.01f, num(j["rgbmRange"], 8));
    out.intensity = num(j["intensity"], 1);
    if (j["shadowColor"].size() == 3) out.shadowColor = {num(j["shadowColor"][0], 0), num(j["shadowColor"][1], 0), num(j["shadowColor"][2], 0)};
    out.settingsJson = j["settings"].dump(-1);
    out.statsJson = j["stats"].dump(-1);
    return true;
}
} // namespace

bool readLightmapHeader(const std::string& folder, const ReadFn& read, LightmapData& out, std::string& error) {
    Json j;
    return parseHeader(folder, read, out, error, j);
}

bool loadLightmapData(const std::string& folder, const ReadFn& read, LightmapData& out, std::string& error) {
    Json j;
    if (!parseHeader(folder, read, out, error, j)) return false;
    const std::string where = folder + "/lightmap.json";
    for (const Json& pj : j["pages"].items()) {
        LightmapPage pg;
        pg.width = int(pj.getInt("width")); pg.height = int(pj.getInt("height"));
        auto image = [&](const std::string& file, std::vector<uint8_t>& px) {
            std::string bytes;
            render::Image img;
            if (!read(folder + "/" + file, bytes)) { error = folder + "/" + file + ": not found"; return false; }
            if (!render::decodeImage(bytes, img) || img.width != pg.width || img.height != pg.height) { error = folder + "/" + file + ": not a " + std::to_string(pg.width) + "x" + std::to_string(pg.height) + " image"; return false; }
            px = std::move(img.rgba);
            return true;
        };
        if (pg.width <= 0 || pg.height <= 0 || pg.width > 8192 || pg.height > 8192) { error = where + ": bad page size"; return false; }
        if (!image(pj.getStr("lightmap"), pg.rgbm)) return false;
        if (pj.has("mask") && !image(pj.getStr("mask"), pg.mask)) return false;
        out.pages.push_back(std::move(pg));
    }
    for (const auto& [k, v] : j["regions"].members()) {
        uint64_t key = 0;
        if (!parseHex64(k, key) || v.size() != 5) { error = where + ": bad region '" + k + "'"; return false; }
        LightmapRegion r;
        r.page = uint16_t(v[0].integer());
        if (r.page >= out.pages.size()) { error = where + ": region '" + k + "' on a missing page"; return false; }
        for (int i = 0; i < 4; ++i) r.st[i] = num(v[size_t(i) + 1], 0);
        out.regions[key] = r;
    }
    if (const Json& t = j["terrain"]; t.isObject()) {
        out.terrain.page = int(t.getInt("page", -1));
        if (out.terrain.page >= int(out.pages.size())) { error = where + ": terrain on a missing page"; return false; }
        for (int i = 0; i < 4; ++i) out.terrain.st[i] = num(t["st"][size_t(i)], i < 2 ? 1.0f : 0.0f);
        for (int i = 0; i < 2; ++i) { out.terrain.origin[i] = num(t["origin"][size_t(i)], 0); out.terrain.extent[i] = std::max(1e-3f, num(t["extent"][size_t(i)], 1)); }
    }
    if (const Json& p = j["probes"]; p.isObject()) {
        auto& g = out.probes;
        g.origin = {num(p["origin"][0], 0), num(p["origin"][1], 0), num(p["origin"][2], 0)};
        g.cell = {num(p["cell"][0], 1), num(p["cell"][1], 1), num(p["cell"][2], 1)};
        g.nx = int(p["dims"][0].integer()); g.ny = int(p["dims"][1].integer()); g.nz = int(p["dims"][2].integer());
        const size_t count = size_t(std::max(g.nx, 0)) * size_t(std::max(g.ny, 0)) * size_t(std::max(g.nz, 0));
        std::string bytes;
        if (count == 0 || count > (1u << 20) || !read(folder + "/" + p.getStr("file", "probes.bin"), bytes) || bytes.size() != count * 24) {
            error = folder + "/probes.bin: missing or not " + std::to_string(count) + " probes"; return false;
        }
        g.coeffs.resize(count * 12);
        for (size_t i = 0; i < g.coeffs.size(); ++i) {
            uint16_t h; std::memcpy(&h, bytes.data() + i * 2, 2);
            g.coeffs[i] = halfToFloat(h);
        }
    }
    return true;
}

} // namespace aiverse::bake
