#include "RoadKit.h"
#include "RoadView.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace aiverse::roads::roadkit {

using namespace render;

namespace {

uint32_t hash(uint32_t x, uint32_t y, uint32_t s) {
    uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, uint32_t s) { return float(hash(uint32_t(x), uint32_t(y), s) & 0xffffu) / 65535.0f; }
// seamless value noise on a `period` lattice
float noise(float x, float y, int period, uint32_t seed) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    auto at = [&](int a, int b) { return rnd(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * u;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * u;
    return a + (b - a) * v;
}
float fbm(float x, float y, int size, int base, uint32_t seed) {   // x, y in pixels of a `size` tile
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0, p = base; o < 5 && p <= size; ++o, p *= 2) {
        sum += noise(x * float(p) / float(size), y * float(p) / float(size), p, seed + uint32_t(o) * 17u) * amp;
        norm += amp; amp *= 0.5f;
    }
    return sum / norm;
}
uint8_t q(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

struct Rgb { float r, g, b; };
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

} // namespace

Image texture(int mat, int size) {
    Image img;
    using V = RoadView;
    if (mat != V::kAsphalt && mat != V::kConcrete && mat != V::kCobble && mat != V::kPaving && mat != V::kGravel && mat != V::kDirt &&
        mat != V::kGrass && mat != V::kCurb && mat != V::kBridge && mat != V::kSoil && mat != V::kAsphaltWorn) return img;
    img.name = "#road-kit-" + std::to_string(mat);
    img.width = img.height = size;
    img.rgba.resize(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float fx = float(x), fy = float(y);
            Rgb c{0.5f, 0.5f, 0.5f};
            switch (mat) {
            case V::kAsphalt: {
                const float n = fbm(fx, fy, size, 8, 11), g = rnd(x, y, 3);
                const float v = 0.085f + (n - 0.5f) * 0.05f + (g > 0.93f ? 0.05f : g < 0.05f ? -0.03f : 0.0f);
                c = {v, v, v * 1.02f};
                break;
            }
            case V::kAsphaltWorn: {   // broken asphalt: patches of dirt and gravel show through, cracks
                const float n = fbm(fx, fy, size, 4, 61), g = rnd(x, y, 23), crack = std::fabs(fbm(fx, fy, size, 16, 67) - 0.5f);
                const float asph = 0.085f + (fbm(fx, fy, size, 8, 11) - 0.5f) * 0.05f;
                Rgb a{asph, asph, asph * 1.02f}, dirt{0.27f * 1.05f, 0.27f * 0.84f, 0.27f * 0.62f};
                dirt = mix(dirt, Rgb{0.3f, 0.29f, 0.26f}, g * 0.5f);
                c = n > 0.56f ? dirt : a;
                if (n > 0.52f && n <= 0.56f) c = mix(a, dirt, (n - 0.52f) / 0.04f);
                if (crack < 0.012f && n <= 0.52f) c = Rgb{0.05f, 0.045f, 0.04f};
                break;
            }
            case V::kConcrete: case V::kBridge: case V::kCurb: {
                const float n = fbm(fx, fy, size, 4, 21), g = rnd(x, y, 5);
                float v = (mat == V::kCurb ? 0.42f : 0.36f) + (n - 0.5f) * 0.10f + (g - 0.5f) * 0.04f;
                if (mat == V::kConcrete && (x % (size / 2) < 1 || y % (size / 2) < 1)) v -= 0.12f;   // expansion joints
                c = {v, v * 0.99f, v * 0.96f};
                break;
            }
            case V::kCobble: {   // rounded setts, 8 x 8 per tile, rows shifted
                const float cell = float(size) / 8.0f;
                const int row = int(fy / cell);
                const float ox = (row & 1) ? cell * 0.5f : 0.0f;
                const float u = std::fmod(fx + ox, cell) / cell - 0.5f, v = std::fmod(fy, cell) / cell - 0.5f;
                const float d = std::max(std::fabs(u), std::fabs(v));
                const int id = int((fx + ox) / cell) * 31 + row;
                const float tone = 0.16f + rnd(id, row, 7) * 0.10f;
                const float n = fbm(fx, fy, size, 16, 8);
                const float gap = std::clamp((d - 0.40f) * 12.0f, 0.0f, 1.0f);
                c = mix(Rgb{tone, tone * 0.97f, tone * 0.93f}, Rgb{0.16f, 0.15f, 0.14f}, gap);
                c = mix(c, Rgb{0.2f, 0.2f, 0.2f}, (n - 0.5f) * 0.3f);
                break;
            }
            case V::kPaving: {   // square slabs 4 x 4 per tile
                const float cell = float(size) / 4.0f;
                const float u = std::fmod(fx, cell) / cell, v = std::fmod(fy, cell) / cell;
                const bool seam = u < 0.025f || v < 0.025f;
                const int id = int(fx / cell) * 7 + int(fy / cell);
                const float tone = 0.30f + rnd(id, 1, 9) * 0.06f + (fbm(fx, fy, size, 8, 4) - 0.5f) * 0.06f;
                c = seam ? Rgb{0.16f, 0.15f, 0.14f} : Rgb{tone, tone * 0.97f, tone * 0.92f};
                break;
            }
            case V::kGravel: {
                const float g = rnd(x, y, 13), n = fbm(fx, fy, size, 32, 31);
                // a touch darker than the gravel road's own texture base: its ridges and grit lift that one, so the
                // junction boxes match the roads running into them
                const float v = (0.30f + (n - 0.5f) * 0.2f + (g - 0.5f) * 0.14f) * 0.86f;
                c = {v * 1.02f, v * 0.97f, v * 0.88f};
                break;
            }
            case V::kDirt: case V::kSoil: {
                const float n = fbm(fx, fy, size, 4, 41), g = rnd(x, y, 17);
                // dirt a touch darker than the dirt road's texture base (see gravel): the boxes match the roads
                const float v = ((mat == V::kSoil ? 0.12f : 0.26f) + (n - 0.5f) * 0.16f + (g - 0.5f) * 0.06f) * (mat == V::kSoil ? 1.0f : 0.86f);
                c = {v * 1.05f, v * 0.84f, v * 0.62f};
                break;
            }
            case V::kGrass: {
                const float n = fbm(fx, fy, size, 8, 51), g = rnd(x, y, 19);
                const float v = 0.22f + (n - 0.5f) * 0.12f + (g - 0.5f) * 0.08f;
                c = {v * 0.62f, v * 1.05f, v * 0.42f};
                break;
            }
            default: break;
            }
            uint8_t* p = &img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4];
            // sRGB-encoded like any PNG the game would ship
            p[0] = q(std::pow(c.r, 1 / 2.2f)); p[1] = q(std::pow(c.g, 1 / 2.2f)); p[2] = q(std::pow(c.b, 1 / 2.2f)); p[3] = 255;
        }
    img.repeat = true;
    return img;
}

namespace {
struct Poly { std::vector<float2> p; };
bool inside(const std::vector<float2>& p, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}
std::vector<Poly> arrowShape(int mark) {
    std::vector<Poly> out;
    auto rect = [&](float u0, float v0, float u1, float v1) { out.push_back({{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}}); };
    auto tri = [&](float2 a, float2 b, float2 c) { out.push_back({{a, b, c}}); };
    const bool straight = mark == kMarkArrowS || mark == kMarkArrowSL || mark == kMarkArrowSR;
    const bool left = mark == kMarkArrowL || mark == kMarkArrowSL || mark == kMarkArrowLR;
    const bool right = mark == kMarkArrowR || mark == kMarkArrowSR || mark == kMarkArrowLR;
    if (straight) { rect(0.43f, 0.30f, 0.57f, 0.98f); tri({0.5f, 0.02f}, {0.70f, 0.32f}, {0.30f, 0.32f}); }
    else rect(0.43f, 0.50f, 0.57f, 0.98f);
    if (left) { rect(0.22f, 0.44f, 0.57f, 0.56f); tri({0.04f, 0.50f}, {0.24f, 0.33f}, {0.24f, 0.67f}); }
    if (right) { rect(0.43f, 0.44f, 0.78f, 0.56f); tri({0.96f, 0.50f}, {0.76f, 0.33f}, {0.76f, 0.67f}); }
    return out;
}
const char* glyph(char c) {   // 5 x 7, rows top to bottom
    switch (c) {
    case 'S': return "01111" "10000" "10000" "01110" "00001" "00001" "11110";
    case 'T': return "11111" "00100" "00100" "00100" "00100" "00100" "00100";
    case 'O': return "01110" "10001" "10001" "10001" "10001" "10001" "01110";
    case 'P': return "11110" "10001" "10001" "11110" "10000" "10000" "10000";
    case 'B': return "11110" "10001" "10001" "11110" "10001" "10001" "11110";
    case 'U': return "10001" "10001" "10001" "10001" "10001" "10001" "01110";
    default: return "00000" "00000" "00000" "00000" "00000" "00000" "00000";
    }
}
float textCover(const char* text, float u, float v) {   // letters side by side across, stretched along v
    const int n = int(std::strlen(text));
    const float lw = 1.0f / float(n);
    const int li = std::clamp(int(u / lw), 0, n - 1);
    const float lu = (u - float(li) * lw) / lw, lv = v;
    const float gx = (lu - 0.1f) / 0.8f * 5.0f, gy = (lv - 0.04f) / 0.92f * 7.0f;
    if (gx < 0 || gy < 0 || gx >= 5 || gy >= 7) return 0;
    return glyph(text[li])[int(gy) * 5 + int(gx)] == '1' ? 1.0f : 0.0f;
}
} // namespace

std::vector<uint32_t> markingAtlas(int cell) {
    const int W = cell * 4;
    std::vector<uint32_t> px(size_t(W) * size_t(W), 0x00ffffffu);
    for (int mark = 0; mark < kMarkCount; ++mark) {
        const int ox = (mark % 4) * cell, oy = (mark / 4) * cell;
        const std::vector<Poly> shape = arrowShape(mark);
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x) {
                float cover = 0;
                for (int sy = 0; sy < 3; ++sy)   // 3 x 3 supersampling: soft edges
                    for (int sx = 0; sx < 3; ++sx) {
                        const float u = (float(x) + (float(sx) + 0.5f) / 3.0f) / float(cell), v = (float(y) + (float(sy) + 0.5f) / 3.0f) / float(cell);
                        float c = 0;
                        if (mark == kMarkLine) c = (u > 0.06f && u < 0.94f) ? 1.0f : 0.0f;
                        else if (mark == kMarkStop) c = textCover("STOP", u, v);
                        else if (mark == kMarkBus) c = textCover("BUS", u, v);
                        else for (auto& pl : shape) if (inside(pl.p, u, v)) { c = 1; break; }
                        cover += c;
                    }
                cover /= 9.0f;
                if (cover <= 0) continue;
                // worn paint: a little noise in the alpha, a few chips
                const float wear = 0.82f + 0.18f * fbm(float(x + ox), float(y + oy), W, 16, 77);
                const float chip = rnd(x + ox, y + oy, 91) > 0.985f ? 0.4f : 1.0f;
                const float a = std::clamp(cover * wear * chip, 0.0f, 1.0f);
                px[size_t(oy + y) * size_t(W) + size_t(ox + x)] = 0x00ffffffu | (uint32_t(a * 255.0f + 0.5f) << 24);
            }
    }
    return px;
}

std::vector<uint32_t> vergeAtlas(int cell) {
    const int W = cell * kVergeSlots, H = cell;
    std::vector<uint32_t> px(size_t(W) * size_t(H), 0);
    auto enc = [](float v) { return uint32_t(q(std::pow(std::clamp(v, 0.0f, 1.0f), 1 / 2.2f))); };
    auto put = [&](int slot, int x, int y, Rgb c, float a) {
        px[size_t(y) * size_t(W) + size_t(slot * cell + x)] = enc(c.r) | enc(c.g) << 8 | enc(c.b) << 16 | uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24;
    };
    for (int slot = 0; slot < 3; ++slot)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < cell; ++x) {
                const float fx = float(x), fy = float(y), u = (fx + 0.5f) / float(cell);
                Rgb c;
                const float g = rnd(x, y, 131 + slot), n = fbm(fx, fy, cell, 8, 141 + slot);
                if (slot == 0) { const float v = 0.30f + (n - 0.5f) * 0.18f + (g - 0.5f) * 0.16f; c = {v * 1.02f, v * 0.96f, v * 0.86f}; }
                else if (slot == 1) { const float v = 0.22f + (n - 0.5f) * 0.12f + (g - 0.5f) * 0.08f; c = {v * 0.62f, v * 1.05f, v * 0.42f}; }
                else { const float v = 0.26f + (n - 0.5f) * 0.14f + (g - 0.5f) * 0.06f; c = {v * 1.05f, v * 0.84f, v * 0.62f}; }
                // the inner edge eats a little into the asphalt (ragged, right at its edge: no ground shows between
                // the two), the outer edge thins out into the ground
                const float rag = fbm(fy * 0.5f, 3.0f, cell, 4, 151 + slot), crumb = fbm(fx, fy, cell, 16, 161 + slot);
                const float innerEdge = 0.01f + 0.035f * rag;
                // the outer edge: torn, bites of grass into it and clumps of it out in the grass (large and small
                // wobbles along the road plus fine noise across)
                const float outer = 0.42f + 0.30f * fbm(fy * 0.5f, 9.0f, cell, 4, 171 + slot) + 0.18f * (fbm(fx * 0.5f, fy, cell, 12, 175 + slot) - 0.5f)
                                    + 0.06f * (fbm(fx, fy, cell, 24, 177 + slot) - 0.5f);
                float a = std::clamp((u - innerEdge) / 0.025f + (crumb - 0.5f) * 1.2f, 0.0f, 1.0f);
                float out = std::clamp(1.0f - (u - outer) / 0.08f, 0.0f, 1.0f);
                const float clump = fbm(fx, fy, cell, 20, 179 + slot);
                if (u > outer && u < outer + 0.25f && clump > 0.66f) out = std::max(out, std::clamp((clump - 0.66f) / 0.05f, 0.0f, 1.0f) * (1.0f - (u - outer) / 0.25f));
                a *= out;
                put(slot, x, y, c, a);
            }
    // slot 3: asphalt breaking up into dirt, v 0 (still whole) .. 1 (the dirt road's own surface), u across the
    // carriageway (the edges go first); not tiled along v
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y), u = (fx + 0.5f) / float(cell), v = (fy + 0.5f) / float(H);
            const float n = fbm(fx, fy, cell, 6, 401), fine = fbm(fx, fy, cell, 24, 403), g = rnd(x, y, 405);
            const float edge = std::pow(std::fabs(u - 0.5f) * 2.0f, 3.0f) * 0.35f;   // the edges crumble first
            const float vv = v * v * (3 - 2 * v);
            const float level = -0.12f + 0.55f * v + 0.75f * vv + edge;                           // how much has broken away here
            const float k = level - (n * 0.75f + fine * 0.25f);
            float a = std::clamp(k / 0.03f, 0.0f, 1.0f);
            // the dirt road's colour (decals are lit a little darker than meshes: brighter to match)
            const float dv = (0.26f + (fbm(fx, fy, cell, 4, 41) - 0.5f) * 0.16f + (g - 0.5f) * 0.06f) * 1.45f;
            Rgb c{dv * 1.05f, dv * 0.84f, dv * 0.62f};
            if (g > 0.993f) c = Rgb{0.40f, 0.38f, 0.34f};                              // grit
            // the broken edge of the asphalt: a dark rim where it cracked off, crumbs of it left on the dirt
            if (k > -0.035f && k <= 0.0f) { c = Rgb{0.045f, 0.042f, 0.04f}; a = std::max(a, 0.75f * (1.0f + k / 0.035f)); }
            else if (a > 0 && v < 0.6f && rnd(x / 2, y / 2, 407) > 0.99f) c = Rgb{0.08f, 0.08f, 0.082f};
            // fine cracks in the asphalt that is still there
            if (a < 0.5f && std::fabs(fbm(fx, fy, cell, 12, 409) - 0.5f) < 0.008f + 0.01f * vv) { c = Rgb{0.04f, 0.037f, 0.035f}; a = std::max(a, 0.8f * std::clamp(v * 3.0f, 0.0f, 1.0f)); }
            put(3, x, y, c, a);
        }
    // slot 4: one wheel's track (tyre rubber on the asphalt): u across it, v along (tiles)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y), u = (fx + 0.5f) / float(cell);
            const float d = (u - 0.5f) / 0.5f;
            const float a = std::exp(-d * d * 3.0f) * (0.6f + 0.8f * fbm(fx, fy, cell, 4, 411)) * (rnd(x, y, 413) > 0.93f ? 0.4f : 1.0f);
            const float v = 0.20f + (fbm(fx, fy, cell, 8, 415) - 0.5f) * 0.03f;   // tinted down by the vertex colour
            put(4, x, y, Rgb{v, v, v * 1.02f}, a);
        }
    // slot 5: the dirt road's own surface, solid and tiling both ways (laid over the seam where a dirt road meets
    // its junction box); a little lighter than the road texture, the vertex colour tints it down to match
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y);
            const float n = fbm(fx, fy, cell, 4, 41), g = rnd(x, y, 17);
            const float v = (0.26f + (n - 0.5f) * 0.16f + (g - 0.5f) * 0.06f) * 1.435f;
            put(5, x, y, Rgb{v * 1.05f * 1.12f, v * 0.84f * 1.26f, v * 0.62f * 1.24f}, 1.0f);
        }
    // slot 6: plain white, solid (the wet film over the road surfaces: the vertex colour is the whole look)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) put(6, x, y, Rgb{1, 1, 1}, 1.0f);
    // slot 7: a puddle, filling the cell: an irregular, lobed pool (no circle), shallow at the rim (a feathered,
    // darker wet edge where the water thins out), the sky's light in the middle with a soft sheen across it;
    // white-ish so the vertex colour tints it (asphalt water, muddy water)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y);
            const float dx = (fx + 0.5f) / float(cell) * 2 - 1, dy = (fy + 0.5f) / float(cell) * 2 - 1;
            const float r = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx);
            // the shore: a few lobes plus noise along the angle (seamless around the circle)
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float lobes = 0.10f * std::sin(ang * 3 + 0.7f) + 0.06f * std::sin(ang * 5 + 2.1f) + 0.04f * std::sin(ang * 7 + 4.0f);
            const float wob = (fbm((ca + 1) * float(cell) * 0.25f, (sa + 1) * float(cell) * 0.25f, cell, 4, 701) - 0.5f) * 0.22f;
            const float shore = 0.80f + lobes + wob;
            const float d = (shore - r) / 0.16f;   // > 0 inside, 1 a little way in
            if (d <= -1) { put(7, x, y, Rgb{0, 0, 0}, 0); continue; }
            const float water = std::clamp(d, 0.0f, 1.0f);
            const float rim = std::clamp(d + 1, 0.0f, 1.0f) * (1 - water);   // the wet band outside the water
            const float sheen = 0.5f + 0.5f * fbm(fx, fy, cell, 3, 709);
            // standing water reads darker than the wet road around it, with the sky's sheen drifting across
            // (linear: the asphalt itself is about 0.05 in this space)
            Rgb c{0.030f + 0.040f * sheen * sheen, 0.036f + 0.048f * sheen * sheen, 0.046f + 0.062f * sheen * sheen};
            // the sky caught in the water: a soft, broken band of light across the pool
            const float band = std::exp(-std::pow((dx * 0.8f + dy * 0.6f + (sheen - 0.5f) * 0.5f) / 0.28f, 2.0f));
            c = mix(c, Rgb{0.15f, 0.17f, 0.21f}, band * 0.12f * std::clamp(d, 0.0f, 1.0f));   // a hint only: the water shader reflects the real sky
            c = mix(c, Rgb{0.028f, 0.028f, 0.030f}, std::clamp(1 - d * 1.6f, 0.0f, 1.0f) * 0.5f);   // shallow toward the shore
            const float a = water * 0.9f + rim * 0.30f;
            put(7, x, y, rim > water ? Rgb{0.022f, 0.022f, 0.024f} : c, a);
        }
    // slot 8: a patch of newer asphalt, filling the cell: a little darker and finer than the road's, its cut edge
    // sealed with a band of tar that wanders a little (the cell is stretched over the patch: the band stays thin)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y);
            const float u = (fx + 0.5f) / float(cell), v = (fy + 0.5f) / float(cell);
            const float g = rnd(x, y, 811), n = fbm(fx, fy, cell, 16, 813);
            const float e = std::min(std::min(u, 1 - u), std::min(v, 1 - v));   // to the cut
            const float seal = 0.035f + 0.02f * fbm(fx, fy, cell, 8, 817);
            const float tone = 0.098f + (n - 0.5f) * 0.02f + (g - 0.5f) * 0.014f;   // the road asphalt renders at about 0.12 here
            Rgb c{tone, tone, tone * 1.04f};
            if (e < seal) c = Rgb{0.045f, 0.045f, 0.048f};   // tar along the cut
            const float a = std::clamp(e / 0.008f, 0.0f, 1.0f) * 0.95f;
            put(8, x, y, c, a);
        }
    // slot 9: a crack: u across (a dark, broken line in the middle with crumbled shoulders), v along (tiles)
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < cell; ++x) {
            const float fx = float(x), fy = float(y);
            const float u = (fx + 0.5f) / float(cell);
            const float wob = (fbm(3.0f, fy, cell, 8, 911) - 0.5f) * 0.25f;
            const float w = 0.14f + 0.14f * fbm(5.0f, fy, cell, 6, 913);   // the crack opens and closes along its length
            const float d = std::fabs(u - 0.5f - wob);
            const float core = std::clamp((w - d) / 0.05f, 0.0f, 1.0f);
            const float crumb = std::clamp((w * 2.2f - d) / (w * 1.2f), 0.0f, 1.0f) * (rnd(x, y, 917) > 0.6f ? 0.5f : 0.15f);
            const float a = std::max(core * 0.9f, crumb) * (fbm(7.0f, fy, cell, 12, 919) > 0.25f ? 1.0f : 0.25f);
            put(9, x, y, Rgb{0.030f, 0.030f, 0.032f}, a * 0.85f);
        }
    return px;
}

bool roadSurfacePainted(const RoadTypeDef& t) {
    if (t.forward.empty() && t.backward.empty()) return false;
    if (t.surface != RoadSurface::Asphalt && t.surface != RoadSurface::Concrete && t.surface != RoadSurface::Dirt && t.surface != RoadSurface::Gravel) return false;
    return t.drivable();   // lines or not: the wheel tracks (ruts on dirt and gravel)
}

float paintMargin(const RoadTypeDef& t) {
    const auto& v = t.forward.empty() ? t.backward : t.forward;
    return v.empty() ? 0.5f : float(v.back().width.toDouble());
}

namespace {
// a dirt or gravel road: ruts worn by the wheels (darker, packed, damp at the bottom, a ridge of pushed-out soil
// along their outer edges), the crown between them looser; one pair down the middle (traffic both ways shares
// it on a narrow unpaved road), faint ones where each lane runs; a strip of grass between the ruts on a one-lane
// track; puddles here and there in the ruts
Image unpavedSurface(const RoadTypeDef& t, Image img, float total, float M, float span) {
    const int W = img.width, H = img.height;
    const bool gravel = t.surface == RoadSurface::Gravel;
    const float lane = t.forward.empty() ? float(t.backward[0].width.toDouble()) : float(t.forward[0].width.toDouble());
    const float perM = lane / 3.0f;
    const float c0 = float(t.carriageway(-1).toDouble());
    struct Rut { float u, depth; };
    std::vector<Rut> ruts;
    const float gauge = 0.80f * perM;
    const bool oneLane = t.lanes(1) + t.lanes(-1) <= 1;
    ruts.push_back({c0 - gauge, 1.0f});
    ruts.push_back({c0 + gauge, 1.0f});
    if (!oneLane)
        for (int dir : {1, -1}) {   // where cars keep to their side to pass each other
            float off = float(t.median.toDouble()) * 0.5f;
            for (auto& l : t.lanesOf(dir)) {
                const float w = float(l.width.toDouble()), cU = c0 + float(dir) * (off + w * 0.5f);
                off += w;
                ruts.push_back({cU - gauge, 0.35f});
                ruts.push_back({cU + gauge, 0.35f});
            }
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float fx = float(x), fy = float(y);
            const float u = (fx + 0.5f) / float(W) * span - M;
            const float g = rnd(x, y, gravel ? 13 : 17), n = fbm(fx, fy, H, gravel ? 32 : 4, gravel ? 31 : 41);
            Rgb c;
            if (gravel) { const float v = 0.30f + (n - 0.5f) * 0.2f + (g - 0.5f) * 0.14f; c = {v * 1.02f, v * 0.97f, v * 0.88f}; }
            else { const float v = 0.26f + (n - 0.5f) * 0.16f + (g - 0.5f) * 0.06f; c = {v * 1.05f, v * 0.84f, v * 0.62f}; }
            const float wander = (fbm(7.0f, fy, H, 2, 291) - 0.5f) * 0.35f * perM;
            float rut = 0, ridge = 0;
            for (auto& r : ruts) {
                const float d = (u - r.u - wander) / (0.34f * perM);
                rut = std::max(rut, r.depth * std::exp(-d * d * 1.2f));
                const float e = (std::fabs(d) - 1.55f) / 0.45f;   // the soil pushed out to both sides
                ridge = std::max(ridge, r.depth * std::exp(-e * e));
            }
            const float patch = 0.7f + 0.6f * fbm(fx, fy, H, 4, 293);
            rut = std::clamp(rut * patch, 0.0f, 1.0f);
            // packed and damp in the ruts: darker, the grit pressed in (no bright stones), a little smoother
            const float packed = gravel ? 0.30f : 0.24f;
            Rgb bottom = gravel ? Rgb{packed * 0.95f, packed * 0.88f, packed * 0.78f} : Rgb{packed * 0.98f, packed * 0.74f, packed * 0.52f};
            bottom = mix(bottom, Rgb{0, 0, 0}, 0.26f + (fbm(fx, fy, H, 8, 295) - 0.5f) * 0.2f);
            c = mix(c, bottom, rut * 0.75f);
            c = mix(c, Rgb{c.r * 1.12f, c.g * 1.12f, c.b * 1.12f}, ridge * 0.6f * (1 - rut));
            if (gravel && ridge > 0.3f && g > 0.9f) c = Rgb{0.42f, 0.40f, 0.36f};   // loose stones thrown to the side
            // the crown: grass on a one-lane track, a few tufts elsewhere
            const float crown = std::fabs(u - c0 - wander) / (0.45f * perM);
            const float tuft = fbm(fx, fy, H, 16, 297);
            float grass = 0;
            if (oneLane) grass = std::clamp((1.0f - crown) * 2.2f + (tuft - 0.5f) * 1.6f, 0.0f, 1.0f);
            else grass = std::clamp((tuft - 0.78f) * 4.0f, 0.0f, 1.0f) * (1 - rut) * (1 - std::clamp(crown, 0.0f, 1.0f) * 0.0f);
            // and toward the edges the verge's grass reaches in a little
            const float edge = std::min(u, total - u) / perM;
            grass = std::max(grass, std::clamp((0.45f - edge) / 0.45f + (tuft - 0.5f) * 1.2f, 0.0f, 1.0f) * (1 - rut));
            if (grass > 0) {
                const float gv = 0.22f + (fbm(fx, fy, H, 8, 51) - 0.5f) * 0.12f + (rnd(x, y, 19) - 0.5f) * 0.08f;
                c = mix(c, Rgb{gv * 0.62f, gv * 1.05f, gv * 0.42f}, grass);
            }
            // puddles: still water in the deepest stretches of the ruts
            const float pud = fbm(fx * 0.6f, fy, H, 3, 299);
            if (!gravel && rut > 0.7f && pud > 0.70f) {
                const float k = std::clamp((pud - 0.70f) / 0.04f, 0.0f, 1.0f) * std::clamp((rut - 0.7f) / 0.1f, 0.0f, 1.0f);
                c = mix(c, Rgb{0.07f, 0.075f, 0.08f}, k * 0.85f);
            }
            uint8_t* p = &img.rgba[(size_t(y) * size_t(W) + size_t(x)) * 4];
            p[0] = q(std::pow(std::clamp(c.r, 0.0f, 1.0f), 1 / 2.2f)); p[1] = q(std::pow(std::clamp(c.g, 0.0f, 1.0f), 1 / 2.2f)); p[2] = q(std::pow(std::clamp(c.b, 0.0f, 1.0f), 1 / 2.2f)); p[3] = 255;
        }
    img.repeat = true;
    return img;
}
} // namespace

Image roadSurface(const RoadTypeDef& t, int variant) {
    Image img;
    const float wl = float(t.carriageway(-1).toDouble()), wr = float(t.carriageway(1).toDouble());   // cells
    const float total = wl + wr, M = paintMargin(t), span = total + 2 * M;   // plain asphalt margin on both sides
    const int W = std::clamp(int(span * 64.0f), 64, 640), H = 256;
    img.name = "#road-surface-" + t.id + "-" + std::to_string(variant);
    img.width = W; img.height = H;
    img.rgba.resize(size_t(W) * size_t(H) * 4);
    if (t.unpaved()) return unpavedSurface(t, std::move(img), total, M, span);
    // lines: (centre offset in cells from the left edge, half width in cells, dash on / period in [0,1] of v, colour)
    struct L { float u, hw, on, phase; Rgb c; };
    std::vector<L> lines;
    const Rgb white{0.80f, 0.80f, 0.78f}, yellow{0.86f, 0.58f, 0.04f};
    // cells per metre of this type, from the standard 3.5 m lane: lane width is stored in cells already, so a line
    // width in metres needs the scale; take it from the data's metres (lane widths are 2.5..5 m in practice)
    const float lane = t.forward.empty() ? float(t.backward[0].width.toDouble()) : float(t.forward[0].width.toDouble());
    const float perM = lane / 3.5f;   // cells per metre, close enough for paint widths
    const float hwThin = 0.06f * perM, hwWide = 0.125f * perM, dash = 3.0f / kRoadPaintPeriod;
    const float med = float(t.median.toDouble()) * 0.5f;
    const bool both = !t.forward.empty() && !t.backward.empty();
    const float c0 = wl;   // the centre line in texture cells
    if (both && t.median.raw == 0 && !(variant & 16)) {   // 16: a driveway crosses here, the solid centre line breaks
        // American style: the line between the two directions is always yellow (dashed: passing allowed; double
        // solid on wider roads); lines between lanes of one direction and the right edges are white
        switch (t.centre) {
        case RoadMarking::Dashed: lines.push_back({c0, hwThin * 1.2f, dash, 0, yellow}); break;
        case RoadMarking::Solid: lines.push_back({c0, hwThin * 1.2f, 1, 0, yellow}); break;
        case RoadMarking::Double: case RoadMarking::Yellow:
            lines.push_back({c0 - 0.14f * perM, hwThin, 1, 0, yellow}); lines.push_back({c0 + 0.14f * perM, hwThin, 1, 0, yellow}); break;
        default: break;
        }
    }
    if (t.laneLines)
        for (int dir : {1, -1}) {
            const auto& v = t.lanesOf(dir);
            float off = med;
            for (size_t k = 0; k < v.size(); ++k) {
                // the left edge of a direction (at the median, or of a one-way road) is yellow
                if (k == 0 && (!both || t.median.raw > 0)) lines.push_back({c0 + float(dir) * (off + 0.1f * perM), hwThin, 1, 0, yellow});
                off += float(v[k].width.toDouble());
                const bool last = k + 1 == v.size();
                const bool parkingNext = !last && v[k + 1].use == RoadLaneUse::Parking;
                const bool bus = v[k].use == RoadLaneUse::Bus || (!last && v[k + 1].use == RoadLaneUse::Bus);
                if (!last) lines.push_back({c0 + float(dir) * off, bus ? hwWide : hwThin, parkingNext ? 0.5f : bus ? 1.0f : dash, 0, white});
                else if (v[k].use != RoadLaneUse::Parking) {
                    const bool open = dir > 0 ? (variant & 1) : (variant & 2);
                    if (!open) lines.push_back({c0 + float(dir) * (off - 0.15f * perM), hwThin, 1, 0, white});
                }
            }
        }
    if (variant & 4) lines.clear();   // tracks and grime only: where the road narrows or wears out
    // wheel paths: every driving lane is worn along two tracks (darker, smoother: tyres polish the stone and leave
    // rubber) with a faint oil line between them, wandering a little along the road
    struct Track { float u, hw; };
    std::vector<Track> tracks;
    std::vector<float> oil;
    for (int dir : {1, -1}) {
        if (variant & 8) break;   // no tracks either: decals draw them (they converge where the road narrows)
        float off = med;
        for (auto& l : t.lanesOf(dir)) {
            const float w = float(l.width.toDouble()), cU = c0 + float(dir) * (off + w * 0.5f);
            off += w;
            if (l.use == RoadLaneUse::Parking || l.use == RoadLaneUse::Bike) continue;
            const float gauge = 0.82f * perM;   // half the distance between the wheels
            tracks.push_back({cU - gauge, 0.32f * perM});
            tracks.push_back({cU + gauge, 0.32f * perM});
            oil.push_back(cU);
        }
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float fx = float(x), fy = float(y);
            // asphalt, seamless along v (the texture repeats along the road)
            const float n = fbm(fx, fy, H, 8, 11), g = rnd(x % H, y, 3);
            float a = 0.085f + (n - 0.5f) * 0.05f + (g > 0.93f ? 0.05f : g < 0.05f ? -0.03f : 0.0f);
            {
                const float u = (fx + 0.5f) / float(W) * span - M;
                const float wander = (fbm(7.0f, fy, H, 2, 91) - 0.5f) * 0.25f * perM;   // tracks are not ruler-straight
                float worn = 0;
                for (auto& tr : tracks) {
                    const float d = std::fabs(u - tr.u - wander) / tr.hw;
                    if (d < 1.6f) worn = std::max(worn, std::exp(-d * d * 1.8f));
                }
                const float patch = 0.75f + 0.5f * fbm(fx, fy, H, 4, 93);   // heavier in places
                const float speckle = g > 0.93f ? 0.05f * worn : 0.0f;       // the bright stones get worn away
                a = a - worn * patch * 0.028f - speckle;
                for (float ou : oil) {
                    const float d = std::fabs(u - ou - wander) / (0.22f * perM);
                    if (d < 2) a -= std::exp(-d * d * 2.0f) * 0.012f * (0.6f + 0.8f * fbm(fx, fy, H, 8, 97));
                }
            }
            Rgb c{a, a, a * 1.02f};
            const float u = (fx + 0.5f) / float(W) * span - M, v = (fy + 0.5f) / float(H);
            {   // dust and grime washed against the curbs (and the median's curbs): a brownish band with a ragged edge,
                // bits of grit and leaves in the gutter
                float d = std::min(std::fabs(u), std::fabs(u - total));
                if (!(variant & 4) && t.median.raw > 0 && t.look["median"].getBool("curb", false)) d = std::min(d, std::fabs(std::fabs(u - c0) - med));
                const float dm = d / perM;   // metres from the curb
                const float rag = 0.25f + 0.2f * fbm(fx * 0.3f, fy, H, 4, 181);
                const float grime = std::exp(-dm / rag) * (0.55f + 0.7f * fbm(fx, fy, H, 8, 183));
                if (grime > 0.01f) {
                    c = mix(c, Rgb{0.155f, 0.14f, 0.115f}, std::clamp(grime * 0.75f, 0.0f, 0.85f));
                    const float bit = rnd(x, y, 187);
                    if (dm < 0.35f && bit > 0.985f) c = bit > 0.995f ? Rgb{0.20f, 0.13f, 0.05f} : Rgb{0.24f, 0.23f, 0.21f};   // a leaf, a pebble
                }
            }
            const float px = span / float(W);   // cells per pixel: antialiasing width
            for (auto& l : lines) {
                const float d = std::fabs(u - l.u) - l.hw;
                float cov = std::clamp(0.5f - d / px, 0.0f, 1.0f);
                if (l.on < 1) { const float ph = std::fmod(v + l.phase, 1.0f); if (ph > l.on) cov = 0; }
                if (cov <= 0) continue;
                const float wear = 0.8f + 0.2f * fbm(fx, fy, H, 16, 77);
                c = mix(c, l.c, cov * wear);
            }
            uint8_t* p = &img.rgba[(size_t(y) * size_t(W) + size_t(x)) * 4];
            p[0] = q(std::pow(c.r, 1 / 2.2f)); p[1] = q(std::pow(c.g, 1 / 2.2f)); p[2] = q(std::pow(c.b, 1 / 2.2f)); p[3] = 255;
        }
    img.repeat = true;
    return img;
}

Material material(int mat) {
    using V = RoadView;
    Material m;
    m.vertexColors = false;
    m.roughness = 0.9f;
    switch (mat) {
    case V::kAsphalt: m.name = "road_asphalt"; m.roughness = 0.86f; m.vertexColors = true; break;
    case V::kAsphaltWorn: m.name = "road_asphalt_worn"; m.roughness = 0.92f; m.vertexColors = true; break;
    case V::kConcrete: m.name = "road_concrete"; m.roughness = 0.8f; break;
    case V::kCobble: m.name = "road_cobble"; m.roughness = 0.75f; break;
    case V::kPaving: m.name = "road_paving"; m.roughness = 0.8f; break;
    case V::kGravel: m.name = "road_gravel"; m.roughness = 0.95f; break;
    case V::kDirt: m.name = "road_dirt"; m.roughness = 0.97f; break;
    case V::kGrass: m.name = "road_grass"; m.roughness = 0.95f; break;
    case V::kCurb: m.name = "road_curb"; m.roughness = 0.7f; m.baseColor = {1.08f, 1.08f, 1.08f, 1}; break;
    case V::kWhite: m.name = "road_marking"; m.roughness = 0.55f; m.baseColor = {0.85f, 0.85f, 0.82f, 1}; break;
    case V::kYellow: m.name = "road_marking_yellow"; m.roughness = 0.55f; m.baseColor = {0.85f, 0.58f, 0.08f, 1}; break;
    case V::kMetal: m.name = "road_metal"; m.metallic = 0.85f; m.roughness = 0.42f; m.baseColor = {0.55f, 0.57f, 0.6f, 1}; break;
    case V::kWood: m.name = "road_wood"; m.roughness = 0.85f; m.baseColor = {0.30f, 0.20f, 0.12f, 1}; break;
    case V::kLeaves: m.name = "road_leaves"; m.roughness = 0.9f; m.baseColor = {0.13f, 0.30f, 0.08f, 1}; break;
    case V::kBark: m.name = "road_bark"; m.roughness = 0.95f; m.baseColor = {0.22f, 0.15f, 0.09f, 1}; break;
    case V::kGlow: m.name = "road_lamp"; m.roughness = 0.3f; m.baseColor = {1, 0.95f, 0.85f, 1}; m.emissive = {1.0f, 0.85f, 0.6f}; m.emissiveStrength = 2.0f; m.castShadow = false; break;
    case V::kSoil: m.name = "road_soil"; break;
    case V::kBridge: m.name = "road_bridge"; m.roughness = 0.82f; m.baseColor = {0.92f, 0.92f, 0.92f, 1}; break;
    case V::kHedge: m.name = "road_hedge"; m.roughness = 0.95f; m.baseColor = {0.10f, 0.25f, 0.07f, 1}; break;
    case V::kDark: m.name = "road_dark"; m.roughness = 0.6f; m.baseColor = {0.08f, 0.08f, 0.09f, 1}; break;
    case V::kSignalRed: m.name = "road_signal_red"; m.roughness = 0.25f; m.baseColor = {0.5f, 0.03f, 0.02f, 1}; m.emissive = {1.0f, 0.08f, 0.04f}; m.emissiveStrength = 1.2f; break;
    case V::kSignalAmber: m.name = "road_signal_amber"; m.roughness = 0.25f; m.baseColor = {0.35f, 0.2f, 0.02f, 1}; break;
    case V::kGlass: m.name = "road_glass"; m.roughness = 0.1f; m.metallic = 0.2f; m.baseColor = {0.55f, 0.7f, 0.75f, 0.35f}; m.alpha = AlphaMode::Blend; m.castShadow = false; break;
    case V::kSign: m.name = "road_sign_red"; m.roughness = 0.4f; m.baseColor = {0.55f, 0.03f, 0.03f, 1}; break;
    case V::kSignalGreen: m.name = "road_signal_green"; m.roughness = 0.25f; m.baseColor = {0.02f, 0.25f, 0.08f, 1}; break;
    default: break;
    }
    return m;
}

namespace {

void quad(std::vector<Tri>& out, int mat, float3 a, float3 b, float3 c, float3 d) {   // CCW seen from the front
    out.push_back({mat, a, b, c});
    out.push_back({mat, a, c, d});
}
void box(std::vector<Tri>& out, int mat, float3 lo, float3 hi) {
    const float3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    quad(out, mat, p[4], p[5], p[6], p[7]);   // +z
    quad(out, mat, p[1], p[0], p[3], p[2]);   // -z
    quad(out, mat, p[5], p[1], p[2], p[6]);   // +x
    quad(out, mat, p[0], p[4], p[7], p[3]);   // -x
    quad(out, mat, p[3], p[7], p[6], p[2]);   // +y
    quad(out, mat, p[0], p[1], p[5], p[4]);   // -y
}
void cylinder(std::vector<Tri>& out, int mat, float3 base, float r0, float r1, float h, int seg = 8, bool cap = true) {
    for (int i = 0; i < seg; ++i) {
        const float a0 = 6.2831853f * float(i) / float(seg), a1 = 6.2831853f * float(i + 1) / float(seg);
        const float3 p0{base.x + std::cos(a0) * r0, base.y, base.z + std::sin(a0) * r0}, p1{base.x + std::cos(a1) * r0, base.y, base.z + std::sin(a1) * r0};
        const float3 q0{base.x + std::cos(a0) * r1, base.y + h, base.z + std::sin(a0) * r1}, q1{base.x + std::cos(a1) * r1, base.y + h, base.z + std::sin(a1) * r1};
        quad(out, mat, p1, p0, q0, q1);
        if (cap && r1 > 0) out.push_back({mat, float3{base.x, base.y + h, base.z}, q1, q0});
    }
}
void sphere(std::vector<Tri>& out, int mat, float3 c, float3 r, int rings = 5, int seg = 8) {
    auto pt = [&](int i, int j) {
        const float th = 3.14159265f * float(i) / float(rings), ph = 6.2831853f * float(j) / float(seg);
        return float3{c.x + std::sin(th) * std::cos(ph) * r.x, c.y + std::cos(th) * r.y, c.z + std::sin(th) * std::sin(ph) * r.z};
    };
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < seg; ++j) {
            const float3 a = pt(i, j), b = pt(i, j + 1), cc = pt(i + 1, j + 1), d = pt(i + 1, j);
            if (i > 0) out.push_back({mat, a, b, d});
            if (i + 1 < rings) out.push_back({mat, b, cc, d});
        }
}

} // namespace

void shape(const std::string& id, float s, std::vector<Tri>& out) {
    using V = RoadView;
    if (id == "lamp") {   // 8 m pole, arm toward the road (-z in the prop frame: the road side), lamp head
        cylinder(out, V::kMetal, {0, 0, 0}, 0.11f * s, 0.07f * s, 8.0f * s, 8, false);
        box(out, V::kMetal, {-0.05f * s, 7.7f * s, -1.6f * s}, {0.05f * s, 7.85f * s, 0.0f});
        box(out, V::kDark, {-0.18f * s, 7.55f * s, -1.9f * s}, {0.18f * s, 7.75f * s, -1.2f * s});
        box(out, V::kGlow, {-0.14f * s, 7.5f * s, -1.85f * s}, {0.14f * s, 7.56f * s, -1.25f * s});
        cylinder(out, V::kDark, {0, 0, 0}, 0.2f * s, 0.16f * s, 0.6f * s, 8, true);
    } else if (id == "tree") {
        cylinder(out, V::kBark, {0, 0, 0}, 0.18f * s, 0.12f * s, 3.2f * s, 7, false);
        sphere(out, V::kLeaves, {0, 4.4f * s, 0}, {2.0f * s, 1.9f * s, 2.0f * s}, 5, 9);
        sphere(out, V::kLeaves, {0.6f * s, 5.6f * s, 0.3f * s}, {1.3f * s, 1.2f * s, 1.3f * s}, 4, 8);
    } else if (id == "tree_pit") {   // a tree pit in a paved sidewalk: soil and grass inside a low stone edge, 1.8 m square
        const float h = 0.9f * s, e = 0.14f * s, top = 0.10f * s;
        box(out, V::kCurb, {-h, 0, -h}, {h, top, -h + e});
        box(out, V::kCurb, {-h, 0, h - e}, {h, top, h});
        box(out, V::kCurb, {-h, 0, -h + e}, {-h + e, top, h - e});
        box(out, V::kCurb, {h - e, 0, -h + e}, {h, top, h - e});
        box(out, V::kSoil, {-h + e, 0, -h + e}, {h - e, 0.05f * s, h - e});
        for (int i = 0; i < 7; ++i) sphere(out, V::kLeaves, {(-0.55f + 0.18f * float(i)) * s, 0.06f * s, ((i % 3) - 1) * 0.35f * s}, {0.16f * s, 0.07f * s, 0.16f * s}, 2, 5);
    } else if (id == "bush") {
        sphere(out, V::kLeaves, {0, 0.55f * s, 0}, {0.9f * s, 0.65f * s, 0.9f * s}, 4, 8);
    } else if (id == "flowerbed") {
        box(out, V::kCurb, {-1.2f * s, 0, -0.6f * s}, {1.2f * s, 0.35f * s, 0.6f * s});
        box(out, V::kSoil, {-1.1f * s, 0.35f * s, -0.5f * s}, {1.1f * s, 0.4f * s, 0.5f * s});
        for (int i = 0; i < 6; ++i) sphere(out, V::kLeaves, {(-0.9f + 0.36f * float(i)) * s, 0.55f * s, ((i & 1) ? 0.18f : -0.18f) * s}, {0.28f * s, 0.22f * s, 0.28f * s}, 3, 6);
    } else if (id == "bollard") {
        cylinder(out, V::kDark, {0, 0, 0}, 0.1f * s, 0.09f * s, 0.9f * s, 8, true);
    } else if (id == "pole") {   // wooden utility pole with a cross arm
        cylinder(out, V::kWood, {0, 0, 0}, 0.15f * s, 0.12f * s, 9.0f * s, 7, true);
        box(out, V::kWood, {-0.08f * s, 8.3f * s, -1.1f * s}, {0.08f * s, 8.45f * s, 1.1f * s});
    } else if (id == "bench") {
        box(out, V::kWood, {-0.9f * s, 0.42f * s, -0.22f * s}, {0.9f * s, 0.48f * s, 0.22f * s});
        box(out, V::kWood, {-0.9f * s, 0.55f * s, 0.18f * s}, {0.9f * s, 0.9f * s, 0.24f * s});
        box(out, V::kMetal, {-0.8f * s, 0, -0.2f * s}, {-0.72f * s, 0.42f * s, 0.2f * s});
        box(out, V::kMetal, {0.72f * s, 0, -0.2f * s}, {0.8f * s, 0.42f * s, 0.2f * s});
    } else if (id == "stop_sign") {   // red octagon on a pole, the face toward -x (the traffic coming along +x)
        cylinder(out, V::kMetal, {0, 0, 0}, 0.04f * s, 0.04f * s, 2.3f * s, 6, true);
        const float r = 0.38f * s, cy = 2.25f * s;
        for (int i = 0; i < 8; ++i) {
            const float a0 = 0.3927f + 0.7854f * float(i), a1 = a0 + 0.7854f;
            const float3 c0{-0.03f * s, cy, 0}, p0{-0.03f * s, cy + std::sin(a0) * r, std::cos(a0) * r}, p1{-0.03f * s, cy + std::sin(a1) * r, std::cos(a1) * r};
            out.push_back({V::kSign, c0, p1, p0});
            const float3 q0{0.0f, p0.y, p0.z}, q1{0.0f, p1.y, p1.z};
            out.push_back({V::kWhite, float3{0, cy, 0}, q0, q1});
        }
    } else if (id == "shelter") {   // bus shelter: 3.6 m long, 1.5 m deep (+z, away from the road), glass back, roof, bench
        const float L = 1.8f * s, D = 1.5f * s, H = 2.5f * s, p = 0.05f * s;
        for (float x : {-L + p, L - p}) for (float z : {p, D - p}) box(out, V::kMetal, {x - p, 0, z - p}, {x + p, H, z + p});
        box(out, V::kMetal, {-L - 0.1f * s, H, -0.15f * s}, {L + 0.1f * s, H + 0.08f * s, D + 0.1f * s});
        box(out, V::kGlass, {-L + p, 0.15f * s, D - 0.03f * s}, {L - p, H - 0.1f * s, D - 0.01f * s});
        box(out, V::kGlass, {-L - 0.01f * s, 0.15f * s, 0.3f * s}, {-L + 0.01f * s, H - 0.1f * s, D - p});
        box(out, V::kWood, {-L * 0.7f, 0.42f * s, D - 0.5f * s}, {L * 0.7f, 0.48f * s, D - 0.1f * s});
        box(out, V::kMetal, {-L * 0.6f, 0, D - 0.3f * s}, {-L * 0.6f + 2 * p, 0.42f * s, D - 0.2f * s});
        box(out, V::kMetal, {L * 0.6f - 2 * p, 0, D - 0.3f * s}, {L * 0.6f, 0.42f * s, D - 0.2f * s});
        cylinder(out, V::kMetal, {L + 0.4f * s, 0, 0.1f * s}, 0.04f * s, 0.04f * s, 2.8f * s, 6, true);   // stop sign
        box(out, V::kSignalGreen, {L + 0.37f * s, 2.3f * s, -0.15f * s}, {L + 0.43f * s, 2.8f * s, 0.35f * s});
    } else if (id == "sign") {
        cylinder(out, V::kMetal, {0, 0, 0}, 0.04f * s, 0.04f * s, 2.6f * s, 6, true);
        box(out, V::kWhite, {-0.03f * s, 2.0f * s, -0.35f * s}, {0.03f * s, 2.7f * s, 0.35f * s});
    }
}

} // namespace aiverse::roads::roadkit
