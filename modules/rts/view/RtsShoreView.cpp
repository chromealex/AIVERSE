#include "RtsShoreView.h"
#include "RtsRoadKit.h"
#include "RtsRoadView.h"
#include "../../../engine/render/FailedModelUpload.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>

namespace aiverse::game {

using namespace render;
using namespace water;

namespace {

constexpr int kChunk = 32;
constexpr float kPi = 3.14159265f;
using RV = RtsRoadView;

struct Acc {
    std::vector<std::vector<ModelVertex>> v;
    std::vector<std::vector<uint32_t>> i;
    void need(int m) { if (int(v.size()) <= m) { v.resize(size_t(m) + 1); i.resize(size_t(m) + 1); } }
    bool empty() const { for (auto& x : i) if (!x.empty()) return false; return true; }
};

uint32_t rgba8(float r, float g, float b, float a = 1) {
    auto c = [](float x) { return uint32_t(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

// ---- procedural textures (seamless 256 x 256) ----
float wrapNoise(float x, float y, int period, int seed) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0), u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    auto h = [&](int a, int b) { return hash01(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    const float a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * u, b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * u;
    return a + (b - a) * v;
}
float fbm(float x, float y, int seed, int oct = 5) {
    float s = 0, amp = 0.5f, tot = 0;
    for (int o = 0; o < oct; ++o) { const int p = 8 << o; s += wrapNoise(x * float(p), y * float(p), p, seed + o) * amp; tot += amp; amp *= 0.5f; }
    return s / tot;
}
Image texture(int mat, int size = 256) {
    Image img;
    img.width = img.height = size;
    img.rgba.resize(size_t(size) * size_t(size) * 4);
    img.repeat = true;
    img.name = "builtin:shore-" + std::to_string(mat);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = float(x) / float(size), v = float(y) / float(size);
            float r = 0.5f, g = 0.5f, b = 0.5f;
            if (mat == RtsShoreView::kStone) {
                // ashlar: 4 courses, 2 blocks per course, offset every other course
                const int row = int(v * 4);
                const float bu = std::fmod(u * 2 + (row % 2 ? 0.5f : 0.0f), 1.0f), bv = std::fmod(v * 4, 1.0f);
                const float mortar = std::min({bu, 1 - bu, bv * 2, (1 - bv) * 2});
                const int block = row * 4 + int(std::floor(u * 2 + (row % 2 ? 0.5f : 0.0f)));
                const float tone = 0.46f + hash01(block, 3, 9) * 0.12f + (fbm(u, v, 21) - 0.5f) * 0.18f;
                r = tone * 1.02f; g = tone * 0.99f; b = tone * 0.93f;
                if (mortar < 0.025f) { r = g = b = 0.28f; }
                else if (mortar < 0.05f) { r *= 0.82f; g *= 0.82f; b *= 0.82f; }
            } else if (mat == RtsShoreView::kRock) {
                const float n = fbm(u, v, 41), c = fbm(u * 1.0f + 0.3f, v, 57, 4);
                const float crack = std::fabs(c - 0.5f) < 0.018f ? 0.55f : 1.0f;
                const float t = (0.30f + (n - 0.5f) * 0.45f) * crack;
                r = t * 1.05f; g = t * 0.98f; b = t * 0.90f;
            } else if (mat == RtsShoreView::kSand) {
                const float n = fbm(u, v, 71), sp = hash01(x, y, 5);
                const float t = 0.80f + (n - 0.5f) * 0.18f - (sp > 0.985f ? 0.25f : 0.0f) + (sp < 0.01f ? 0.12f : 0.0f);
                r = t * 0.96f; g = t * 0.86f; b = t * 0.66f;
            } else if (mat == RtsShoreView::kFoam) {
                const float n = fbm(u, v, 91, 4);
                r = g = b = 0.97f;
                img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4 + 3] = uint8_t(std::clamp((n - 0.35f) * 2.2f, 0.0f, 1.0f) * 255);
                img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4 + 0] = img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4 + 1] =
                    img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4 + 2] = 247;
                continue;
            }
            const uint32_t c = rgba8(r, g, b);
            std::memcpy(&img.rgba[(size_t(y) * size_t(size) + size_t(x)) * 4], &c, 4);
        }
    return img;
}
Material material(int mat) {
    Material m;
    m.vertexColors = true;
    switch (mat) {
    case RtsShoreView::kStone: m.name = "shore_stone"; m.roughness = 0.9f; break;
    case RtsShoreView::kRock: m.name = "shore_rock"; m.roughness = 0.95f; break;
    case RtsShoreView::kSand: m.name = "shore_sand"; m.roughness = 1.0f; break;
    case RtsShoreView::kFoam: m.name = "shore_foam"; m.alpha = AlphaMode::Blend; m.transparent = true; m.castShadow = false; m.roughness = 0.4f; break;
    case RtsShoreView::kCanvas: m.name = "shore_canvas"; m.baseColor = {0.80f, 0.10f, 0.08f, 1}; m.doubleSided = true; m.roughness = 0.8f; break;
    case RtsShoreView::kPaint: m.name = "shore_paint"; m.baseColor = {0.86f, 0.86f, 0.84f, 1}; m.roughness = 0.6f; break;
    default: break;
    }
    return m;
}
int surfaceMat(const std::string& s) {
    if (s == "stone") return RtsShoreView::kStone;
    if (s == "rock") return RtsShoreView::kRock;
    if (s == "sand") return RtsShoreView::kSand;
    if (s == "paving") return RV::kPaving;
    if (s == "concrete") return RV::kConcrete;
    if (s == "cobble") return RV::kCobble;
    if (s == "grass") return RV::kGrass;
    if (s == "soil" || s == "dirt") return RV::kSoil;
    if (s == "gravel") return RV::kGravel;
    if (s == "asphalt") return RV::kAsphalt;
    if (s == "wood") return RV::kWood;
    return RtsShoreView::kStone;
}

// one point of a cross-section: d across (cells, landward positive), y (world), material of the span to the next
struct PP { float d, y; int mat; };

class Mesher {
public:
    Mesher(const ShoreTypes& T, const Network& N, const Geometry& G, const std::function<float(float, float)>& ground, float wy)
        : T_(T), N_(N), G_(G), ground_(ground), wy_(wy), m_(1.0f / T.cellMeters) {}
    std::map<int64_t, Acc> chunks;
    Acc foam;
    struct Prop { std::string shape, model; mat4 xf; };
    std::vector<Prop> props;

    void build() {
        for (auto& bank : G_.banks) {
            const Body& B = N_.bodies[size_t(bank.body)];
            std::vector<const Segment*> segs;
            for (auto& s : B.shores) if (s.side == bank.side && s.to > 0 && s.from < bank.length) segs.push_back(&s);
            for (size_t k = 0; k < segs.size(); ++k) {
                const Segment& s = *segs[k];
                const int ti = T_.find(s.type);
                if (ti < 0) continue;
                const int prev = k > 0 && std::fabs(segs[k - 1]->to - s.from) < 1 ? T_.find(segs[k - 1]->type) : -1;
                const int next = k + 1 < segs.size() && std::fabs(segs[k + 1]->from - s.to) < 1 ? T_.find(segs[k + 1]->type) : -1;
                segment(bank, B, s, T_.list[size_t(ti)], prev, next);
            }
        }
    }

private:
    const ShoreTypes& T_;
    const Network& N_;
    const Geometry& G_;
    std::function<float(float, float)> ground_;
    float wy_, m_;

    Acc& chunkAt(float x, float z) {
        const int64_t cx = int64_t(std::floor(x / float(kChunk))), cz = int64_t(std::floor(z / float(kChunk)));
        return chunks[(cx << 32) ^ (cz & 0xffffffff)];
    }
    // banks drawn against the other hand (land on the other side of the direction of travel) flip their faces
    bool flip_ = false;
    void quad(Acc& a, int mat, float3 p0, float3 p1, float3 p2, float3 p3, float2 t0, float2 t1, float2 t2, float2 t3, uint32_t col = 0xffffffffu, uint32_t col2 = 0) {
        if (flip_) { std::swap(p1, p3); std::swap(t1, t3); }
        float3 n = normalize(cross(p1 - p0, p3 - p0));
        if (length(cross(p1 - p0, p3 - p0)) < 1e-9f) n = normalize(cross(p2 - p1, p0 - p1));
        a.need(mat);
        auto& V = a.v[size_t(mat)];
        const uint32_t base = uint32_t(V.size());
        const float3 P[4] = {p0, p1, p2, p3};
        const float2 T[4] = {t0, t1, t2, t3};
        for (int k = 0; k < 4; ++k) {
            ModelVertex v; v.pos = P[k]; v.nrm = n; v.uv = T[k]; v.color = (col2 && (k == 1 || k == 2)) ? col2 : col;
            if (col == 0xffffffffu && !col2) v.color = tint(mat, P[k]);
            V.push_back(v);
        }
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) a.i[size_t(mat)].push_back(base + k);
    }
    // vertex colour by world position: the rock gets slow light / dark / warm patches over tens of metres (the
    // texture's repeats stop lining up), sand and soil darken under the water to meet the lake bed without an edge
    uint32_t tint(int mat, float3 p) const {
        auto n3 = [](float x, float y, float z, int seed) {
            const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z));
            const float fx = x - float(xi), fy = y - float(yi), fz = z - float(zi);
            const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy), w = fz * fz * (3 - 2 * fz);
            auto h = [&](int a, int b, int c) { return hash01(a + seed * 131, b * 7 + c * 13, c + seed); };
            auto lx = [&](int b, int c) { return h(xi, b, c) + (h(xi + 1, b, c) - h(xi, b, c)) * u; };
            auto ly = [&](int c) { return lx(yi, c) + (lx(yi + 1, c) - lx(yi, c)) * v; };
            return ly(zi) + (ly(zi + 1) - ly(zi)) * w;
        };
        const float mx = p.x / m_, my = p.y / m_, mz = p.z / m_;   // metres
        if (mat == RtsShoreView::kRock) {
            const float big = n3(mx / 23, my / 9, mz / 23, 3), mid = n3(mx / 6, my / 3, mz / 6, 5), strata = 0.5f + 0.5f * std::sin(my * 1.7f + mid * 3.0f);
            const float k = 0.62f + 0.55f * big + 0.18f * (mid - 0.5f) + 0.08f * (strata - 0.5f);
            const float warm = n3(mx / 40, 0, mz / 40, 9);
            return rgba8(k * (1.0f + 0.10f * warm), k * (1.0f + 0.03f * warm), k * (1.0f - 0.08f * warm));
        }
        if (mat == RtsShoreView::kSand || mat == RV::kSoil) {
            const float depth = std::max(0.0f, wy_ - p.y) / m_;              // metres under the surface
            const float k = 1.0f - 0.55f * std::clamp(depth / 1.6f, 0.0f, 1.0f);
            const float wet = depth > 0 ? 1.0f : std::clamp(1.0f - (p.y - wy_) / m_ / 0.35f, 0.0f, 1.0f);   // wet sand at the waterline
            const float g = k * (1.0f - 0.18f * wet) * (0.92f + 0.16f * n3(mx / 9, 0, mz / 9, 4));
            return rgba8(g * 0.97f, g, g * 1.02f);
        }
        return 0xffffffffu;
    }

    // a weathered stone: an icosphere (80 faces) squashed, turned and pushed in and out by noise; smooth normals
    void boulder(Acc& a, float3 c, float rx, float ry, float rz, float yaw, int seed) {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<float3> v{{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                              {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        std::vector<std::array<int, 3>> f{{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                         {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        for (auto& q : v) q = normalize(q);
        std::map<std::pair<int, int>, int> mid;
        auto split = [&](int i, int j) {
            const auto key = std::minmax(i, j);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            v.push_back(normalize((v[size_t(i)] + v[size_t(j)]) * 0.5f));
            return mid[key] = int(v.size()) - 1;
        };
        std::vector<std::array<int, 3>> g;
        for (auto& tr : f) {
            const int ab = split(tr[0], tr[1]), bc = split(tr[1], tr[2]), ca = split(tr[2], tr[0]);
            g.push_back({tr[0], ab, ca}); g.push_back({tr[1], bc, ab}); g.push_back({tr[2], ca, bc}); g.push_back({ab, bc, ca});
        }
        const float cs = std::cos(yaw), sn = std::sin(yaw);
        std::vector<float3> P(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            const float3 q = v[i];
            // lumps and flat facets: noise over the direction, a few planes cut off
            float k = 0.78f + 0.32f * hash01(int(std::lround(q.x * 7)) + seed, int(std::lround(q.y * 7)), int(std::lround(q.z * 7)) + seed * 3);
            if (q.y < -0.35f) k *= 0.85f;   // a flatter underside
            const float x = q.x * rx * k, y = q.y * ry * k, z = q.z * rz * k;
            P[i] = {c.x + x * cs - z * sn, c.y + y, c.z + x * sn + z * cs};
        }
        std::vector<float3> N(v.size(), float3{0, 0, 0});
        for (auto& tr : g) {
            const float3 n = cross(P[size_t(tr[1])] - P[size_t(tr[0])], P[size_t(tr[2])] - P[size_t(tr[0])]);
            for (int k : tr) N[size_t(k)] += n;
        }
        const int mat = RtsShoreView::kRock;
        a.need(mat);
        auto& V = a.v[size_t(mat)];
        const uint32_t base = uint32_t(V.size());
        for (size_t i = 0; i < P.size(); ++i) {
            ModelVertex mv; mv.pos = P[i]; mv.nrm = normalize(N[i]);
            mv.uv = {(P[i].x + P[i].z) / m_ / 3.0f, P[i].y / m_ / 3.0f};
            mv.color = tint(mat, P[i]);
            V.push_back(mv);
        }
        for (auto& tr : g) {
            const float3 n = cross(P[size_t(tr[1])] - P[size_t(tr[0])], P[size_t(tr[2])] - P[size_t(tr[0])]);
            const float3 out = P[size_t(tr[0])] - c;
            const bool ccw = n.x * out.x + n.y * out.y + n.z * out.z >= 0;
            a.i[size_t(mat)].push_back(base + uint32_t(tr[0]));
            a.i[size_t(mat)].push_back(base + uint32_t(ccw ? tr[1] : tr[2]));
            a.i[size_t(mat)].push_back(base + uint32_t(ccw ? tr[2] : tr[1]));
        }
    }

    // a box at `c` (world), size (world) along the frame t (x), up (y), n (z)
    void box(Acc& a, int mat, float3 c, float3 t, float3 n, float sx, float sy, float sz) {
        const float3 up{0, 1, 0};
        auto P = [&](float i, float j, float k) { return c + t * (i * sx * 0.5f) + up * (j * sy) + n * (k * sz * 0.5f); };
        const float2 a0{0, 0}, a1{sx / m_ / 4, 0}, a2{sx / m_ / 4, sy / m_ / 4}, a3{0, sy / m_ / 4};
        quad(a, mat, P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), a0, a1, a2, a3);       // top
        quad(a, mat, P(-1, 0, -1), P(1, 0, -1), P(1, 1, -1), P(-1, 1, -1), a0, a1, a2, a3);     // -n
        quad(a, mat, P(1, 0, 1), P(-1, 0, 1), P(-1, 1, 1), P(1, 1, 1), a0, a1, a2, a3);         // +n
        quad(a, mat, P(-1, 0, 1), P(-1, 0, -1), P(-1, 1, -1), P(-1, 1, 1), a0, a1, a2, a3);     // -t
        quad(a, mat, P(1, 0, -1), P(1, 0, 1), P(1, 1, 1), P(1, 1, -1), a0, a1, a2, a3);         // +t
    }
    mat4 frame(float3 pos, float3 t, float3 n, float k = 1) {
        mat4 m;
        m.at(0, 0) = t.x; m.at(1, 0) = 0; m.at(2, 0) = t.z;
        m.at(0, 1) = 0; m.at(1, 1) = 1; m.at(2, 1) = 0;
        m.at(0, 2) = n.x; m.at(1, 2) = 0; m.at(2, 2) = n.z;
        m.at(0, 3) = pos.x; m.at(1, 3) = pos.y; m.at(2, 3) = pos.z;
        return m * mat4::scale({k, k, k});
    }

    struct Station { float x, z, nx, nz, s; bool open = false; };
    std::vector<Station> stations(const Bank& b, float from, float to) {
        std::vector<Station> out;
        auto lerp = [&](float s) {
            size_t j = 0;
            while (j + 2 < b.pts.size() && b.pts[j + 1].s < s) ++j;
            const BankPoint& p = b.pts[j]; const BankPoint& q = b.pts[j + 1];
            const float t = q.s > p.s ? std::clamp((s - p.s) / (q.s - p.s), 0.0f, 1.0f) : 0;
            float nx = p.nx + (q.nx - p.nx) * t, nz = p.nz + (q.nz - p.nz) * t; const float l = std::hypot(nx, nz);
            return Station{p.x + (q.x - p.x) * t, p.z + (q.z - p.z) * t, nx / l, nz / l, s, t < 0.5f ? p.open : q.open};
        };
        from = std::max(0.0f, from); to = std::min(b.length, to);
        if (to - from < 0.1f) return out;
        out.push_back(lerp(from));
        for (auto& p : b.pts) if (p.s > from + 0.05f && p.s < to - 0.05f) out.push_back({p.x, p.z, p.nx, p.nz, p.s, p.open});
        out.push_back(lerp(to));
        return out;
    }
    float groundAt(const Station& st, float d) { return ground_ ? ground_(st.x + st.nx * d, st.z + st.nz * d) : 0.0f; }
    float landY(const Station& st) { return std::max(groundAt(st, 1.6f), wy_ + 0.05f * m_); }
    float3 at(const Station& st, float d, float y) { return {st.x + st.nx * d, y, st.z + st.nz * d}; }

    // joins two cross-sections of equal length (one per station) with quads
    void loft(const Station& a, const std::vector<PP>& A, const Station& b, const std::vector<PP>& B) {
        if (A.size() != B.size() || A.size() < 2) return;
        Acc& acc = chunkAt((a.x + b.x) * 0.5f, (a.z + b.z) * 0.5f);
        float va = 0, vb = 0;
        for (size_t k = 0; k + 1 < A.size(); ++k) {
            // metres per texture repeat: rock and sand cover more ground per tile (less visible repetition)
            const float tile = A[k].mat == RtsShoreView::kRock ? 11.0f : A[k].mat == RtsShoreView::kSand ? 7.0f : 4.0f;
            const float u0 = a.s / tile, u1 = b.s / tile;
            const float3 p0 = at(a, A[k].d, A[k].y), p1 = at(a, A[k + 1].d, A[k + 1].y), q0 = at(b, B[k].d, B[k].y), q1 = at(b, B[k + 1].d, B[k + 1].y);
            const float la = length(p1 - p0) / m_ / tile, lb = length(q1 - q0) / m_ / tile;
            if (la < 1e-5f && lb < 1e-5f) continue;
            quad(acc, A[k].mat, p0, q0, q1, p1, {u0, va}, {u1, vb}, {u1, vb + lb}, {u0, va + la});
            va += la; vb += lb;
        }
    }
    // closes a cross-section at a segment end: quads from the profile down to the bed
    void cap(const Station& st, const std::vector<PP>& A, bool start) {
        Acc& acc = chunkAt(st.x, st.z);
        const float bed = wy_ - 0.5f;
        for (size_t k = 0; k + 1 < A.size(); ++k) {
            float3 p0 = at(st, A[k].d, A[k].y), p1 = at(st, A[k + 1].d, A[k + 1].y);
            float3 b0 = at(st, A[k].d, std::min(bed, A[k].y)), b1 = at(st, A[k + 1].d, std::min(bed, A[k + 1].y));
            const int mat = A[k].mat == RtsShoreView::kFoam ? RtsShoreView::kStone : A[k].mat;
            if (start) quad(acc, mat, b0, b1, p1, p0, {A[k].d, 0}, {A[k + 1].d, 0}, {A[k + 1].d, 1}, {A[k].d, 1});
            else quad(acc, mat, b1, b0, p0, p1, {A[k + 1].d, 0}, {A[k].d, 0}, {A[k].d, 1}, {A[k + 1].d, 1});
        }
    }
    void foamStrip(const Station& a, const Station& b, float d0, float d1, float alpha) {
        const bool keep = flip_;
        flip_ = false;   // the foam is unlit-ish and drawn from above only: wind it explicitly below
        const bool up = keep;
        const Station& A = up ? b : a; const Station& Bq = up ? a : b;
        foamStripRaw(A, Bq, d0, d1, alpha);
        flip_ = keep;
    }
    void foamStripRaw(const Station& a, const Station& b, float d0, float d1, float alpha) {
        const uint32_t c0 = rgba8(1, 1, 1, 0), c1 = rgba8(1, 1, 1, alpha);
        const float y = wy_ + 0.012f;
        const float dm = (d0 + d1) * 0.5f;
        quad(foam, RtsShoreView::kFoam, at(a, d0, y), at(b, d0, y), at(b, dm, y), at(a, dm, y), {a.s / 6, 0}, {b.s / 6, 0}, {b.s / 6, 0.5f}, {a.s / 6, 0.5f}, c0, 0);
        // the second half fades out again towards d1
        Acc& f = foam;
        auto& V = f.v[size_t(RtsShoreView::kFoam)];
        V[V.size() - 2].color = c1; V[V.size() - 1].color = c1;
        quad(foam, RtsShoreView::kFoam, at(a, dm, y), at(b, dm, y), at(b, d1, y), at(a, d1, y), {a.s / 6, 0.5f}, {b.s / 6, 0.5f}, {b.s / 6, 1}, {a.s / 6, 1}, c1, 0);
        V[V.size() - 2].color = c0; V[V.size() - 1].color = c0;
    }

    // ---- cross-sections ----
    std::vector<PP> embankment(const ShoreType& t, const Station& st) {
        const float g = landY(st), gp = g + 0.03f * m_ + 0.004f;
        const float bed = wy_ - std::min(t.wallHeight + 2.0f, 3.0f) * m_;
        const float top = std::max(gp + 0.15f * m_, wy_ + t.wallHeight * m_), cw = 0.55f * m_, W = std::max({t.promenade * m_, cw + 0.5f * m_, 1.5f});   // past the widest water cell
        std::vector<PP> p{{-0.02f * m_, bed, surfaceMat(t.wallSurface)}, {0, top, RV::kConcrete}};
        if (t.parapet == "stone") {
            const float ph = t.parapetHeight * m_, pw = 0.45f * m_;
            p.back().mat = surfaceMat(t.wallSurface);
            p.push_back({0, top + ph, RV::kConcrete});
            p.push_back({pw, top + ph, surfaceMat(t.wallSurface)});
            p.push_back({pw, gp, surfaceMat(t.promenadeSurface)});
        } else {
            p.push_back({cw, top, RV::kConcrete});
            p.push_back({cw, gp, surfaceMat(t.promenadeSurface)});
        }
        p.push_back({W, gp, RV::kCurb});
        p.push_back({W + 0.12f * m_, g + 0.001f, RV::kCurb});
        return p;
    }
    std::vector<PP> cliff(const ShoreType& t, const Body& B, const Station& st, int side, float from, float to, bool ts, bool te) {
        const float g = landY(st);
        const float H = cliffHeight(t, B.id, side, st.s, from, to, ts, te) * m_;
        const float slope = (t.slopeMin + (t.slopeMax - t.slopeMin) * noise1(st.s / 17.0f, B.id * 7 + side)) * kPi / 180.0f;
        const float run = (H / m_) / std::tan(slope) * m_;
        const float j1 = (noise1(st.s / 3.0f, B.id + 3) - 0.5f) * 0.9f * m_, j2 = (noise1(st.s / 2.3f, B.id + 9) - 0.5f) * 1.1f * m_;
        const float top = t.top * m_, back = H * 1.4f;
        const int rock = surfaceMat(t.rockSurface), grass = surfaceMat(t.topSurface);
        return {{-1.6f * m_, wy_ - 1.6f * m_, rock}, {-0.25f * m_ + j1 * 0.3f, wy_ - 0.05f * m_, rock},
                {run * 0.3f + j1, g + H * 0.35f, rock}, {run * 0.65f + j2, g + H * 0.72f, rock}, {run, g + H, rock},
                {run + 0.6f * m_, g + H + 0.12f * m_, grass}, {run + top, g + H * 0.95f, grass},
                {run + top + back * 0.5f, std::max(g + H * 0.45f, groundAt(st, run + top + back * 0.5f) + 0.01f), grass},
                {run + top + back, groundAt(st, run + top + back) + 0.004f, grass}, {run + top + back + 0.5f * m_, groundAt(st, run + top + back + 0.5f * m_) - 0.04f * m_, grass}};
    }
    std::vector<PP> beach(const ShoreType& t, const Body& B, const Station& st, int side, bool natural) {
        // over the land the surface follows the ground (a little above it: the cell-based slope of the terrain into
        // the water must not poke through), towards the water it runs down under the surface
        const float W = beachWidth(t, B.id, side, st.s) * m_, lift = 0.05f * m_;
        const int sand = surfaceMat(t.sandSurface);
        auto over = [&](float d) { return std::max(groundAt(st, d), wy_) + lift; };
        if (natural)   // a muddy slope from the water up to the ground; the terrain's own grass carries on behind it
            return {{-1.0f * m_, wy_ - 0.14f * m_, sand}, {0, wy_ + 0.004f, sand}, {W * 0.35f, over(W * 0.35f), sand},
                    {W * 0.7f, over(W * 0.7f), sand}, {W * 0.7f + 0.6f * m_, groundAt(st, W * 0.7f + 0.6f * m_) - 0.06f * m_, sand}};
        // under the water the sand only dips a little: the lake bed of the terrain carries on, so no slab shows through
        return {{-1.2f * m_, wy_ - 0.15f * m_, sand}, {0, wy_ + 0.004f, sand}, {W * 0.15f, over(W * 0.15f), sand}, {W * 0.4f, over(W * 0.4f), sand},
                {W * 0.7f, over(W * 0.7f), sand}, {W, over(W), sand}, {W + 0.8f * m_, groundAt(st, W + 0.8f * m_) - 0.05f * m_, sand}};
    }

    void segment(const Bank& bank, const Body& B, const Segment& seg, const ShoreType& t, int prev, int next) {
        const auto st = stations(bank, seg.from, seg.to);
        if (st.size() < 2) return;
        {
            const float tx = bank.pts.back().x - bank.pts.front().x, tz = bank.pts.back().z - bank.pts.front().z;
            const BankPoint& m = bank.pts[bank.pts.size() / 2];
            flip_ = tx * m.nz - tz * m.nx > 0;
        }
        const bool ts = prev < 0 || T_.list[size_t(prev)].kind != ShoreKind::Cliff, te = next < 0 || T_.list[size_t(next)].kind != ShoreKind::Cliff;
        auto section = [&](const Station& s) {
            switch (t.kind) {
            case ShoreKind::Embankment: return embankment(t, s);
            case ShoreKind::Cliff: return cliff(t, B, s, bank.side, seg.from, seg.to, ts && seg.from > 0.5f, te && seg.to < bank.length - 0.5f);
            case ShoreKind::Beach: return beach(t, B, s, bank.side, false);
            default: return beach(t, B, s, bank.side, true);
            }
        };
        std::vector<PP> last = section(st[0]);
        const bool walls = t.kind == ShoreKind::Embankment || t.kind == ShoreKind::Cliff;   // beaches just end
        if (walls && prev >= 0 && T_.list[size_t(prev)].kind != t.kind) cap(st[0], last, true);
        // a flight of steps down into the water every `stairs` metres of quay (a gap in the railing there)
        std::vector<float> stairAt;
        if (t.kind == ShoreKind::Embankment && t.stairs > 0)
            for (float s = seg.from + t.stairs * 0.5f; s < seg.to - 12; s += t.stairs) stairAt.push_back(s);
        auto nearStair = [&](float s, float r) { for (float a : stairAt) if (std::fabs(s - a) < r) return true; return false; };
        for (size_t i = 1; i < st.size(); ++i) {
            std::vector<PP> cur = section(st[i]);
            const Station& a = st[i - 1]; const Station& b = st[i];
            if (a.open || b.open) {   // a mouth: no shore here; close the shore where it stops / starts again
                if (walls && !a.open && b.open) cap(a, last, false);
                if (walls && a.open && !b.open) cap(b, cur, true);
                last = std::move(cur);
                continue;
            }
            loft(st[i - 1], last, st[i], cur);
            if (t.kind == ShoreKind::Embankment && t.parapet == "railing" && !nearStair((a.s + b.s) * 0.5f, 2.2f)) {   // a, b are not open here   // rail on the coping
                const float top = last[1].y, ph = t.parapetHeight * m_, d = 0.27f * m_;
                Acc& acc = chunkAt(a.x, a.z);
                for (float hy : {ph, ph * 0.5f}) {
                    const float r = 0.03f * m_;
                    quad(acc, RV::kMetal, at(a, d - r, top + hy), at(b, d - r, top + hy), at(b, d + r, top + hy), at(a, d + r, top + hy), {0, 0}, {1, 0}, {1, 1}, {0, 1});
                    quad(acc, RV::kMetal, at(a, d - r, top + hy - 2 * r), at(b, d - r, top + hy - 2 * r), at(b, d - r, top + hy), at(a, d - r, top + hy), {0, 0}, {1, 0}, {1, 1}, {0, 1});
                    quad(acc, RV::kMetal, at(b, d + r, top + hy - 2 * r), at(a, d + r, top + hy - 2 * r), at(a, d + r, top + hy), at(b, d + r, top + hy), {0, 0}, {1, 0}, {1, 1}, {0, 1});
                }
            }
            if (t.kind == ShoreKind::Cliff && t.foam) foamStrip(a, b, -2.6f * m_, 0.1f * m_, 0.55f);
            if (t.kind == ShoreKind::Beach && t.surf) { foamStrip(a, b, -1.6f * m_, 0.25f * m_, 0.35f); }
            last = std::move(cur);
        }
        if (walls && next >= 0 && T_.list[size_t(next)].kind != t.kind) cap(st.back(), last, false);
        flip_ = false;   // boxes and shapes build their own frames
        details(bank, B, seg, t, st, stairAt, prev, next);
    }

    Station stationAt(const Bank& bank, float s) { auto v = stations(bank, std::max(0.0f, s - 0.2f), s + 0.2f); return v.empty() ? Station{} : v.front(); }

    void details(const Bank& bank, const Body& B, const Segment& seg, const ShoreType& t, const std::vector<Station>& st,
                 const std::vector<float>& stairAt, int prev, int next) {
        auto tangent = [&](const Station& s) { return float3{s.nz, 0, -s.nx}; };   // along the bank (either way: shapes are symmetric enough)
        if (t.kind == ShoreKind::Embankment) {
            const auto sec = embankment(t, st[0]);
            const float top = sec[1].y;
            if (t.parapet == "railing")
                for (float s = seg.from + 0.5f; s < seg.to; s += t.postSpacing) {
                    bool gap = false; for (float a : stairAt) if (std::fabs(s - a) < 2.2f) gap = true;
                    if (gap) continue;
                    const Station p = stationAt(bank, s);
                    if (p.open) continue;
                    box(chunkAt(p.x, p.z), RV::kMetal, at(p, 0.27f * m_, top), tangent(p), {p.nx, 0, p.nz}, 0.07f * m_, t.parapetHeight * m_, 0.07f * m_);
                }
            for (float a : stairAt) {   // steps down the face of the wall, along the bank
                const Station p = stationAt(bank, a);
                if (p.open) continue;
                const float3 tt = tangent(p), n{p.nx, 0, p.nz};
                const int steps = 8;
                for (int k = 0; k < steps; ++k) {
                    const float y0 = wy_ - 0.2f * m_, y1 = top - (top - y0) * float(k) / float(steps);
                    const float3 c = at(p, -0.6f * m_, y0) + tt * ((float(k) - float(steps) * 0.5f) * 0.45f * m_);
                    box(chunkAt(p.x, p.z), surfaceMat(t.wallSurface), c, tt, n, 0.45f * m_, y1 - y0, 1.2f * m_);
                }
            }
            for (int end = 0; end < 2; ++end) {   // steps from the promenade down to a beach / natural bank next to it
                const int o = end ? next : prev;
                if (o < 0 || (T_.list[size_t(o)].kind != ShoreKind::Beach && T_.list[size_t(o)].kind != ShoreKind::Natural)) continue;
                const float s0 = end ? seg.to : seg.from;
                const Station p = stationAt(bank, s0);
                if (p.open) continue;
                const float3 n{p.nx, 0, p.nz};
                float3 tt = tangent(p);
                const Station q = stationAt(bank, s0 + (end ? 1.0f : -1.0f));
                if ((q.x - p.x) * tt.x + (q.z - p.z) * tt.z < 0) tt = tt * -1.0f;   // pointing out of the quay
                const float g = landY(p), gp = g + 0.03f * m_, W = t.promenade * m_;
                for (int k = 0; k < 5; ++k) {
                    const float h = (gp - (wy_ + 0.02f)) * (1 - float(k) / 5.0f);
                    if (h <= 0) continue;
                    box(chunkAt(p.x, p.z), RV::kConcrete, at(p, W * 0.5f, wy_ + 0.02f) + tt * ((float(k) + 0.5f) * 0.5f * m_), tt, n, 0.5f * m_, h, W);
                }
            }
        }
        if (t.kind == ShoreKind::Cliff && t.rubble) {   // boulders at the foot
            for (float s = seg.from + 1; s < seg.to - 1; s += 3.5f) {
                const float r = hash01(B.id, int(s * 10), 13);
                if (r < 0.35f) continue;
                const Station p = stationAt(bank, s);
                if (p.open) continue;
                // a cluster of one to three stones, half in the water
                const int count = 1 + int(hash01(B.id, int(s * 10), 29) * 2.99f);
                for (int k = 0; k < count; ++k) {
                    const int key = int(s * 10) * 7 + k;
                    const float size = (0.5f + hash01(B.id, key, 17) * (k ? 0.7f : 1.3f)) * m_;
                    const float d = -(0.3f + hash01(B.id, key, 19) * 2.4f) * m_;
                    const float along = (hash01(B.id, key, 31) - 0.5f) * 2.4f * m_;
                    const float3 c = at(p, d, wy_ - size * (0.25f + 0.3f * hash01(B.id, key, 37))) + float3{p.nz, 0, -p.nx} * along;
                    boulder(chunkAt(p.x, p.z), c, size, size * (0.55f + r * 0.35f), size * (0.7f + 0.3f * hash01(B.id, key, 41)),
                            hash01(B.id, key, 23) * 2 * kPi, B.id * 1000 + key);
                }
            }
        }
        // props
        for (auto& pd : t.props) {
            for (float s = seg.from + pd.phase; s < seg.to - 1; s += pd.spacing) {
                bool gap = false; for (float a : stairAt) if (std::fabs(s - a) < 3) gap = true;
                if (gap) continue;
                const Station p = stationAt(bank, s);
                if (p.open) continue;
                const float3 n{p.nx, 0, p.nz};
                const float3 tt = tangent(p);
                float d = pd.offset >= 0 ? pd.offset * m_ : 0;
                float y = landY(p) + 0.03f * m_;
                if (t.kind == ShoreKind::Embankment && pd.offset < 0) {
                    if (pd.shape == "lamp") d = t.promenade * m_ - 0.5f * m_;
                    else if (pd.shape == "bench") d = t.promenade * m_ - 1.1f * m_;
                    else if (pd.shape == "bollard") { d = 0.27f * m_; y = embankment(t, p)[1].y; }
                    else d = t.promenade * m_ * 0.5f;
                }
                if (t.kind == ShoreKind::Beach && pd.offset < 0) {
                    const float W = beachWidth(t, B.id, bank.side, s) * m_;
                    d = pd.shape == "lifeguard" ? W * 0.3f : W * 0.55f;
                    y = std::max(wy_, landY(p) - 0.02f * m_);
                }
                const float3 pos = at(p, d, y);
                if (!pd.model.empty()) { props.push_back({"", pd.model, frame(pos, tt, n, pd.scale)}); continue; }
                if (pd.shape == "umbrella") umbrella(pos, tt, n, pd.scale);
                else if (pd.shape == "lounger") { box(chunkAt(pos.x, pos.z), RtsShoreView::kPaint, pos + tt * (0.9f * m_), n, tt, 0.65f * m_, 0.32f * m_, 1.9f * m_);
                                                  box(chunkAt(pos.x, pos.z), RtsShoreView::kPaint, pos - tt * (0.9f * m_), n, tt, 0.65f * m_, 0.32f * m_, 1.9f * m_); }
                else if (pd.shape == "lifeguard") tower(pos, tt, n);
                else if (pd.shape == "boulder") boulder(chunkAt(pos.x, pos.z), pos + float3{0, 0.4f * m_, 0}, 0.8f * m_ * pd.scale, 0.55f * m_ * pd.scale, 0.7f * m_ * pd.scale, s, int(s * 10));
                else props.push_back({pd.shape, "", frame(pos, tt, n, pd.scale)});
            }
        }
    }
    void umbrella(float3 pos, float3 t, float3 n, float k) {
        Acc& a = chunkAt(pos.x, pos.z);
        const float h = 2.3f * m_ * k, r = 1.3f * m_ * k;
        box(a, RV::kMetal, pos, t, n, 0.06f * m_, h, 0.06f * m_);
        const float3 apex = pos + float3{0, h + 0.25f * m_, 0};
        const int seg = 8;
        for (int i = 0; i < seg; ++i) {
            const float a0 = float(i) / seg * 2 * kPi, a1 = float(i + 1) / seg * 2 * kPi;
            const float3 p0 = pos + float3{std::cos(a0) * r, h - 0.15f * m_, std::sin(a0) * r}, p1 = pos + float3{std::cos(a1) * r, h - 0.15f * m_, std::sin(a1) * r};
            quad(a, i % 2 ? RtsShoreView::kCanvas : RtsShoreView::kPaint, p0, apex, apex, p1, {0, 0}, {0.5f, 1}, {0.5f, 1}, {1, 0});
        }
    }
    void tower(float3 pos, float3 t, float3 n) {
        Acc& a = chunkAt(pos.x, pos.z);
        const float leg = 2.2f * m_, w = 1.8f * m_;
        for (float i : {-1.0f, 1.0f}) for (float j : {-1.0f, 1.0f})
            box(a, RV::kWood, pos + t * (i * w * 0.42f) + n * (j * w * 0.42f), t, n, 0.12f * m_, leg, 0.12f * m_);
        box(a, RtsShoreView::kPaint, pos + float3{0, leg, 0}, t, n, w, 1.3f * m_, w);
        box(a, RtsShoreView::kCanvas, pos + float3{0, leg + 1.3f * m_, 0}, t, n, w * 1.2f, 0.18f * m_, w * 1.2f);
    }
};

std::unique_ptr<Model> toModel(const std::string& name, Acc& a, const std::vector<Image>& images, const std::vector<Material>& materials) {
    auto m = std::make_unique<Model>();
    m->path = name;
    m->materials = materials;
    for (auto& img : images) { Image b; b.name = img.name; b.tex = img.tex; b.borrowedTexture = true; b.repeat = true; b.width = img.width; b.height = img.height; m->images.push_back(b); }
    Node root; root.name = name; root.mesh = 0;
    MeshDef md; md.name = name;
    for (int k = 0; k < int(a.i.size()); ++k) {
        if (a.i[size_t(k)].empty() || size_t(k) >= materials.size()) continue;
        Primitive p;
        p.firstIndex = uint32_t(m->indices.size());
        p.indexCount = uint32_t(a.i[size_t(k)].size());
        p.firstVertex = uint32_t(m->vertices.size());
        p.vertexCount = uint32_t(a.v[size_t(k)].size());
        p.material = k;
        const uint32_t base = uint32_t(m->vertices.size());
        float3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (auto& v : a.v[size_t(k)]) {
            lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
            hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
            m->vertices.push_back(v);
        }
        for (uint32_t i : a.i[size_t(k)]) m->indices.push_back(base + i);
        {   // smooth shading of the rock and sand surfaces: average normals of vertices that share a position
            std::map<std::tuple<int64_t, int64_t, int64_t>, float3> acc;
            auto key = [](float3 q) { return std::tuple<int64_t, int64_t, int64_t>{std::llround(q.x * 512.0f), std::llround(q.y * 512.0f), std::llround(q.z * 512.0f)}; };
            const bool soft = k == RtsShoreView::kRock || k == RtsShoreView::kSand || k == RV::kGrass || k == RV::kSoil;
            if (soft) {
                for (uint32_t v = base; v < uint32_t(m->vertices.size()); ++v) acc[key(m->vertices[v].pos)] += m->vertices[v].nrm;
                for (uint32_t v = base; v < uint32_t(m->vertices.size()); ++v) m->vertices[v].nrm = normalize(acc[key(m->vertices[v].pos)]);
            }
        }
        generateTangents(m->vertices, m->indices, p.firstIndex, p.indexCount);
        p.boundsMin = lo; p.boundsMax = hi;
        md.primitives.push_back(p);
    }
    if (md.primitives.empty()) return nullptr;
    m->meshes.push_back(md);
    m->nodes.push_back(root);
    m->finalize();
    return m;
}

void addShape(Acc& a, const std::string& shape, const mat4& xf, float worldPerMetre, std::map<std::string, std::vector<roadkit::Tri>>& cache) {
    auto it = cache.find(shape);
    if (it == cache.end()) { std::vector<roadkit::Tri> tris; roadkit::shape(shape, worldPerMetre, tris); it = cache.emplace(shape, std::move(tris)).first; }
    for (auto& t : it->second) {
        float3 p[3] = {xf.transformPoint(t.a), xf.transformPoint(t.b), xf.transformPoint(t.c)};
        const float3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));
        a.need(t.mat);
        auto& V = a.v[size_t(t.mat)];
        const uint32_t base = uint32_t(V.size());
        for (auto& q : p) { ModelVertex mv; mv.pos = q; mv.nrm = n; mv.uv = {q.x + q.z, q.y}; V.push_back(mv); }
        for (uint32_t k = 0; k < 3; ++k) a.i[size_t(t.mat)].push_back(base + k);
    }
}

} // namespace

void RtsShoreView::init(gfx::Device& dev, const Json& shoreTypes, FileReader reader) {
    if(dev_) {
        shutdown(*dev_);
        if(dev_)throw std::runtime_error(
            "shore View cannot rebind before verified GPU retirement");
    }
    dev_ = &dev;
    ++stateRevision_;
    read_ = std::move(reader);
    warnings_.clear();
    dirty_ = true;
    try { types_ = loadShoreTypes(shoreTypes); }
    catch (const std::exception& e) { warnings_.push_back(e.what()); types_ = loadShoreTypes(Json()); }
    // materials: the road kit's, then the shore kit's; the game's textures replace a surface's
    std::map<int, std::string> custom;
    for (auto& t : types_.list) for (auto& [surface, path] : t.textures) if (!custom.count(surfaceMat(surface))) custom[surfaceMat(surface)] = path;
    buildingKit_=std::make_unique<Model>();
    materials_.assign(size_t(kShoreMatEnd), Material{});
    for (int m = 0; m < kShoreMatEnd; ++m) {
        if (m >= RV::kMatCount && m < kStone) continue;
        Image img;
        if (auto it = custom.find(m); it != custom.end()) {
            std::string bytes;
            if (read_ && read_(it->second, bytes) && decodeImage(bytes, img)) { img.repeat = true; img.name = it->second; }
            else { warnings_.push_back("shore texture '" + it->second + "': cannot read"); img = Image{}; }
        }
        if (img.rgba.empty() && !img.compressed) img = m < RV::kMatCount ? roadkit::texture(m) : (m == kStone || m == kRock || m == kSand || m == kFoam ? texture(m, m == kRock ? 512 : 256) : Image{});
        const bool upload=!img.rgba.empty() || img.compressed;
        const int tex=upload?addImage(dev,*buildingKit_,std::move(img)):-1;
        if(upload && tex<0)
            throw std::runtime_error("shore View: GPU kit texture allocation failed");
        Material mt = m < RV::kMatCount ? roadkit::material(m) : material(m);
        if (m != kFoam) { mt.alpha = AlphaMode::Opaque; mt.transparent = false; }   // the road kit's verge grass is blended; shores are solid
        if (custom.count(m)) mt.baseColor = {1, 1, 1, mt.baseColor.w};
        mt.baseTex = tex;
        materials_[size_t(m)] = mt;
    }
    images_=std::move(buildingKit_->images);
    buildingKit_.reset();
    for(auto& img:images_)img.borrowedTexture=false;
    if (netJson_.isObject()) {   // new types: parse the network again
        try { net_ = parseNetwork(netJson_, types_); }
        catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = Network{}; }
    }
}

void RtsShoreView::inheritRuntimeStateForReload(const RtsShoreView& old) {
    // The old GPU meshes and materials are never copied. Parsing the new
    // shore type kit will rebuild the water network against the new rules.
    netJson_=old.netJson_;
    net_=old.net_;
    dirty_=true;
}

void RtsShoreView::release(gfx::Device& dev) {
    // Retain old cliff/beach/foam GPU allocations until an actual RHI
    // completion token proves that the last submitted frame is finished.
    for (auto& c : chunks_) retirement_.retireModel(dev,c);
    for (auto& c : transparent_) retirement_.retireModel(dev,c);
    chunks_.clear(); transparent_.clear(); props_.clear();
}

void RtsShoreView::shutdown(gfx::Device& dev) {
    if(!dev_)return;
    if(dev_!=&dev)throw std::logic_error("shore View GPU owner mismatch");
    ++stateRevision_;
    release(dev);
    for (auto& p : propModels_) retirement_.retireModel(dev,p.model);
    propModels_.clear();
    // A single explicit full GPU idle check at shutdown also protects
    // host-owned kit textures. Never destroy them if completion fails.
    if(!retirement_.completeForShutdown(dev)) {
        warnings_.push_back(
            "shore View shutdown: GPU idle unproven, resources held fail-closed");
        // Do not let RtsView::shutdown() continue and tear down other GPU
        // owners after this host failed to establish a completion barrier.
        throw std::runtime_error("shore View GPU retirement barrier failed");
    }
    if(buildingKit_) {
        releaseModel(dev,*buildingKit_);
        buildingKit_.reset();
    }
    for (auto& img : images_) if (img.tex.valid()) {
        dev.destroyTexture(img.tex); img.tex = {};
    }
    images_.clear();
    dev_ = nullptr;
}

void RtsShoreView::setMap(const Json& map) {
    ++stateRevision_;
    net_ = Network{};
    dirty_ = true;
    netJson_ = map["waterNetwork"];
    if (!netJson_.isObject()) { netJson_ = Json(); return; }
    try { net_ = parseNetwork(netJson_, types_); }
    catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = Network{}; }
}

int RtsShoreView::propModel(gfx::Device& dev, const std::string& path) {
    for (size_t i = 0; i < propModels_.size(); ++i) if (propModels_[i].path == path) return propModels_[i].model ? int(i) : -1;
    // Make publication allocation-safe BEFORE the GPU Model is created.
    propModels_.reserve(propModels_.size()+1);
    PropModel pm; pm.path = path;
    bool uploadStarted=false;
    try {
        pm.model=loadModel(path,read_);
        if(!pm.model)throw std::runtime_error("Model import returned no data");
        uploadStarted=true;
        if(!uploadModel(dev,*pm.model))
            throw std::runtime_error("GPU Model upload returned false");
    } catch(const std::exception& e) {
        if(uploadStarted) {
            const auto status=abortFailedModelUpload(dev,pm.model);
            if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                warnings_.push_back("shore prop "+path+
                    ": GPU completion unavailable; partial Model quarantined");
        } else pm.model.reset();
        warnings_.push_back("shore prop "+path+": "+e.what());
    }
    propModels_.push_back(std::move(pm));
    return propModels_.back().model ? int(propModels_.size() - 1) : -1;
}

void RtsShoreView::buildUnpublished(gfx::Device& dev, const std::function<float(float, float)>& heightAt, float waterY, int w, int h) {
    // Invoked ONLY on a private candidate owner whose published containers
    // are empty. Do not retire or mutate the live ShoreView here.
    dirty_ = false;
    builtWaterY_ = waterY; builtW_ = w; builtH_ = h;
    if (net_.empty()) return;
    Geometry geom = buildGeometry(net_, w, h, types_.cellMeters);
    markOpen(geom, waterCells(net_, geom, w, h, types_.cellMeters), w, h);
    for (auto& i : geom.issues) warnings_.push_back(i);
    Mesher mesh(types_, net_, geom, heightAt, waterY);
    mesh.build();
    std::map<std::string, std::vector<roadkit::Tri>> shapes;
    for (auto& p : mesh.props) {
        if (!p.model.empty()) { const int k = propModel(dev, p.model); if (k >= 0) props_.push_back({k, p.xf}); continue; }
        const float3 at = p.xf.transformPoint({0, 0, 0});
        const int64_t cx = int64_t(std::floor(at.x / float(kChunk))), cz = int64_t(std::floor(at.z / float(kChunk)));
        addShape(mesh.chunks[(cx << 32) ^ (cz & 0xffffffff)], p.shape, p.xf, 1.0f / types_.cellMeters, shapes);
    }
    chunks_.reserve(chunks_.size()+mesh.chunks.size());
    for (auto& [key, a] : mesh.chunks) {
        if (a.empty()) continue;
        auto m = toModel("shores", a, images_, materials_);
        if (!m) continue;
        try {
            if(!uploadModel(dev,*m))
                throw std::runtime_error("GPU Model upload returned false");
            chunks_.push_back(std::move(m));
        } catch(const std::exception& e) {
            const auto status=abortFailedModelUpload(dev,m);
            if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                warnings_.push_back("shore chunk: GPU completion unavailable; partial Model quarantined");
            warnings_.push_back(std::string("shore chunk: ")+e.what());
            throw; // one late upload must cancel the entire candidate
        }
    }
    if (!mesh.foam.empty()) {
        auto m = toModel("shore-foam", mesh.foam, images_, materials_);
        if (m) {
            transparent_.reserve(transparent_.size()+1);
            try {
                if(!uploadModel(dev,*m))
                    throw std::runtime_error("GPU foam Model upload returned false");
                transparent_.push_back(std::move(m));
            } catch(const std::exception& e) {
                const auto status=abortFailedModelUpload(dev,m);
                if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                    warnings_.push_back("shore foam: GPU completion unavailable; partial Model quarantined");
                warnings_.push_back(std::string("shore foam: ")+e.what());
                throw; // never publish partial foam/bank scenery
            }
        }
    }
}

struct RtsShoreView::PreparedRebuild {
    gfx::Device* device=nullptr;
    std::unique_ptr<RtsShoreView> staged;
    bool committed=false;
    ~PreparedRebuild() noexcept {
        if(!device || !staged)return;
        // These replacements never reached a submitted camera. The borrowed
        // shore material textures continue to belong to the active kit.
        for(auto& model:staged->chunks_)
            if(model)releaseModel(*device,*model);
        for(auto& model:staged->transparent_)
            if(model)releaseModel(*device,*model);
        for(auto& prop:staged->propModels_)
            if(prop.model)releaseModel(*device,*prop.model);
    }
};

std::shared_ptr<RtsShoreView::PreparedRebuild> RtsShoreView::prepareRebuild(
        gfx::Device& dev,const std::function<float(float,float)>& heightAt,
        float waterY,int w,int h,bool forceGround) {
    if(dev_!=&dev)
        throw std::logic_error("RtsShoreView prepare: GPU owner mismatch");
    if(!needsVisualPreparation(waterY,w,h) && !forceGround)return {};
    auto candidate=std::make_shared<PreparedRebuild>();
    candidate->device=&dev;
    candidate->staged=std::make_unique<RtsShoreView>();
    RtsShoreView& staged=*candidate->staged;
    staged.dev_=&dev;
    staged.read_=read_;
    staged.types_=types_;
    staged.net_=net_;
    staged.images_=images_;
    staged.materials_=materials_;
    staged.warnings_=warnings_;
    staged.dirty_=true;
    staged.buildUnpublished(dev,heightAt,waterY,w,h);
    return candidate;
}

void RtsShoreView::commitRebuild(std::shared_ptr<PreparedRebuild> candidate) noexcept {
    if(!candidate || candidate->committed)return;
    ++stateRevision_;
    candidate->committed=true;
    RtsShoreView& staged=*candidate->staged;
    release(*dev_);
    for(auto& p:propModels_)retirement_.retireModel(*dev_,p.model);
    propModels_.swap(staged.propModels_);
    chunks_.swap(staged.chunks_);
    transparent_.swap(staged.transparent_);
    props_.swap(staged.props_);
    warnings_.swap(staged.warnings_);
    builtWaterY_=staged.builtWaterY_;
    builtW_=staged.builtW_;
    builtH_=staged.builtH_;
    dirty_=false;
}

void RtsShoreView::rebuild(gfx::Device& dev,
        const std::function<float(float,float)>& heightAt,
        float waterY,int w,int h) {
    auto prepared=prepareRebuild(dev,heightAt,waterY,w,h);
    commitRebuild(std::move(prepared));
}

void RtsShoreView::drawCandidate(const std::shared_ptr<PreparedRebuild>& c,Renderer3D& r) {
    if(!c){drawPrepared(r);return;}
    if(c->committed || c->device!=dev_)throw std::logic_error("Shore: stale candidate draw");
    c->staged->drawPrepared(r);
}

void RtsShoreView::draw(Renderer3D& r, const std::function<float(float, float)>& heightAt, float waterY, int w, int h) {
    if (!dev_) return;
    if (dirty_ || waterY != builtWaterY_ || w != builtW_ || h != builtH_) rebuild(*dev_, heightAt, waterY, w, h);
    drawPrepared(r);
}
void RtsShoreView::drawPrepared(Renderer3D& r) {
    for (auto& c : chunks_) { ModelDraw d; d.model = c.get(); d.flags = kDrawStatic; r.drawModel(d); }
    for (auto& p : props_) { ModelDraw d; d.model = propModels_[size_t(p.model)].model.get(); d.world = p.xf; d.flags = kDrawStatic; r.drawModel(d); }
    for (auto& c : transparent_) { ModelDraw d; d.model = c.get(); d.flags = kDrawNoCastShadow | kDrawTransparent; r.drawModel(d); }
}

} // namespace aiverse::game
