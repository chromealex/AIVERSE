// Light probes (agent-lightbake): a regular grid over the static scene; each probe integrates the
// radiance of uniform sphere directions (sky, emissive, bounced sun / lights) into the ambient +
// gradient form the shaders use (a = mean L, b = 2 mean(L d): irradiance(n) = a + b.n, exact for a
// linear sky), plus the direct light of baked-only lights. Probes inside geometry (many backface hits)
// are refilled from valid neighbours. The sun's direct light stays realtime for dynamic objects.
#include "BakerInternal.h"
#include <thread>

namespace aiverse::bake::detail {

void bakeProbes(const SceneRT& rt, LightProbeGrid& g, const ProgressFn& progress, bool& cancelled) {
    const BakeSettings& st = *rt.settings;
    float spacing = std::max(st.probeSpacing, 0.25f);
    const float3 lo = rt.bmin;
    const float3 hi{rt.bmax.x, std::min(rt.bmax.y, rt.bmin.y + std::max(st.probeHeight, spacing)), rt.bmax.z};
    auto dims = [&](float s, int& nx, int& ny, int& nz) {
        nx = std::max(2, int(std::ceil((hi.x - lo.x) / s)) + 1);
        ny = std::max(2, int(std::ceil((hi.y - lo.y) / s)) + 1);
        nz = std::max(2, int(std::ceil((hi.z - lo.z) / s)) + 1);
    };
    int nx, ny, nz;
    dims(spacing, nx, ny, nz);
    while (size_t(nx) * size_t(ny) * size_t(nz) > size_t(std::max(st.maxProbes, 8))) { spacing *= 1.25f; dims(spacing, nx, ny, nz); }
    g = LightProbeGrid{};
    g.origin = lo;
    g.cell = {nx > 1 ? (hi.x - lo.x) / float(nx - 1) : 1.0f, ny > 1 ? std::max(hi.y - lo.y, 0.01f) / float(ny - 1) : 1.0f, nz > 1 ? (hi.z - lo.z) / float(nz - 1) : 1.0f};
    g.cell = {std::max(g.cell.x, 1e-3f), std::max(g.cell.y, 1e-3f), std::max(g.cell.z, 1e-3f)};
    g.nx = nx; g.ny = ny; g.nz = nz;
    const int count = nx * ny * nz;
    g.coeffs.assign(size_t(count) * 12, 0.0f);
    std::vector<uint8_t> valid(size_t(count), 0);
    const int samples = std::clamp(st.probeSamples, 16, 16384);
    const int threads = st.threads > 0 ? st.threads : std::max(1u, std::thread::hardware_concurrency());
    const bool ok = parallelFor(count, threads, [&](int i) {
        const int x = i % nx, y = (i / nx) % ny, z = i / (nx * ny);
        const float3 p{g.origin.x + g.cell.x * float(x), g.origin.y + g.cell.y * float(y) + 0.05f, g.origin.z + g.cell.z * float(z)};
        Rng rng(mixKey(st.seed ^ 0x5052u, uint64_t(i)));
        const float r1 = rng.uniform(), r2 = rng.uniform();
        float a[3] = {}, b[9] = {};
        int back = 0;
        for (int s = 0; s < samples; ++s) {
            float u1 = r1 + 0.7548776662f * float(s + 1), u2 = r2 + 0.5698402910f * float(s + 1);
            u1 -= std::floor(u1); u2 -= std::floor(u2);
            const float cz = 1 - 2 * u1, sz = std::sqrt(std::max(0.0f, 1 - cz * cz)), phi = 6.2831853f * u2;
            const float3 d{sz * std::cos(phi), cz, sz * std::sin(phi)};
            bool bf = false; float t = 0;
            const float3 L = rt.radiance(p, d, rng, std::max(st.bounces, 1), &bf, &t);
            if (bf) ++back;
            const float lc[3] = {L.x, L.y, L.z}, dc[3] = {d.x, d.y, d.z};
            for (int c = 0; c < 3; ++c) { a[c] += lc[c]; for (int k = 0; k < 3; ++k) b[k * 3 + c] += lc[c] * dc[k]; }
        }
        const float inv = 1.0f / float(samples);
        float* o = &g.coeffs[size_t(i) * 12];
        for (int c = 0; c < 3; ++c) { o[c] = a[c] * inv * st.indirectIntensity; for (int k = 0; k < 3; ++k) o[3 + k * 3 + c] = 2.0f * b[k * 3 + c] * inv * st.indirectIntensity; }
        // baked-only lights reach dynamic objects only through the probes: their direct light as L1
        for (const auto& l : rt.scene->lights) {
            if (!l.bakedOnly) continue;
            float3 dl = l.pos - p;
            const float d2 = dot(dl, dl);
            if (d2 >= l.radius * l.radius || d2 < 1e-8f) continue;
            float att = 1 - d2 / (l.radius * l.radius); att *= att;
            const float dist = std::sqrt(d2);
            dl = dl * (1.0f / dist);
            if (l.cosOuter > -1.5f) {
                const float sp = std::clamp((dot(-dl, normalize(l.dir)) - l.cosOuter) / std::max(l.cosInner - l.cosOuter, 1e-4f), 0.0f, 1.0f);
                const float s3 = sp * sp * (3 - 2 * sp);
                att *= s3 * s3;
            }
            if (att <= 0 || rt.bvh.occluded(p, dl, dist - rt.eps)) continue;
            const float3 I = l.color * att;
            const float ic[3] = {I.x, I.y, I.z}, dc[3] = {dl.x, dl.y, dl.z};
            for (int c = 0; c < 3; ++c) { o[c] += ic[c] * 0.25f; for (int k = 0; k < 3; ++k) o[3 + k * 3 + c] += ic[c] * dc[k] * 0.5f; }
        }
        valid[size_t(i)] = back * 4 < samples ? 1 : 0;
    }, "probes", progress);
    if (!ok) { cancelled = true; return; }
    // refill probes inside geometry from valid 6-neighbours (several rounds)
    for (int round = 0; round < 16; ++round) {
        std::vector<uint8_t> nv = valid;
        bool changed = false;
        for (int i = 0; i < count; ++i) {
            if (valid[size_t(i)]) continue;
            const int x = i % nx, y = (i / nx) % ny, z = i / (nx * ny);
            float acc[12] = {}; int n = 0;
            const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& o : off) {
                const int xx = x + o[0], yy = y + o[1], zz = z + o[2];
                if (xx < 0 || yy < 0 || zz < 0 || xx >= nx || yy >= ny || zz >= nz) continue;
                const int j = xx + nx * (yy + ny * zz);
                if (!valid[size_t(j)]) continue;
                for (int k = 0; k < 12; ++k) acc[k] += g.coeffs[size_t(j) * 12 + size_t(k)];
                ++n;
            }
            if (!n) continue;
            for (int k = 0; k < 12; ++k) g.coeffs[size_t(i) * 12 + size_t(k)] = acc[k] / float(n);
            nv[size_t(i)] = 1; changed = true;
        }
        valid.swap(nv);
        if (!changed) break;
    }
}

} // namespace aiverse::bake::detail
