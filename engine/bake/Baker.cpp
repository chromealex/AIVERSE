// Light baker (agent-lightbake): CPU path tracer for lightmaps. See BakeScene.h for units / inputs.
//
// 1. instances are flattened into world-space triangles (BVH);
// 2. every receiving instance gets a lightmap region sized by texels per unit and packed into pages;
// 3. triangles are rasterized into the pages (texel -> world position / normal);
// 4. per texel: cosine-weighted hemisphere paths (sky, emissive, bounced sun / lights) = indirect,
//    sun visibility (soft, maskSamples) = shadow mask, direct light of baked-only lights (and the sun in
//    subtractive mode); texels inside geometry (backface hits) are dropped and refilled by dilation;
// 5. indirect is denoised (edge-aware a-trous), regions are dilated into their padding, RGBM encoded.
// Every random stream is keyed by its texel, so the result does not depend on the number of threads.
#include "BakerInternal.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

namespace aiverse::bake {
using namespace detail;

namespace detail {

bool parallelFor(int n, int threads, const std::function<void(int)>& fn, const std::string& stage, const ProgressFn& progress) {
    std::atomic<int> next{0}, done{0};
    std::atomic<bool> cancel{false};
    const int t = std::max(1, threads);
    auto work = [&] {
        for (;;) {
            if (cancel.load(std::memory_order_relaxed)) return;
            const int i = next.fetch_add(1);
            if (i >= n) return;
            fn(i);
            done.fetch_add(1);
        }
    };
    std::vector<std::thread> pool;
    for (int i = 1; i < t; ++i) pool.emplace_back(work);
    if (t == 1 || !progress) {
        work();
    } else {
        // the calling thread reports progress while the pool works
        std::thread last(work);
        while (done.load() < n && !cancel.load()) {
            if (!progress(stage, float(done.load()) / float(std::max(n, 1)))) cancel = true;
            if (pool.empty() && done.load() >= n) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            bool finished = next.load() >= n && done.load() >= n;
            if (finished) break;
        }
        last.join();
    }
    for (auto& th : pool) th.join();
    if (progress && !cancel && !progress(stage, 1.0f)) cancel = true;
    return !cancel.load();
}

float3 SceneRT::albedoAt(const Tri& t, float u, float v) const {
    const BakeMaterial& m = scene->materials[t.material];
    float3 a = m.albedo;
    if (m.texture && m.texture->width > 0 && m.texture->rgba.size() >= size_t(m.texture->width) * size_t(m.texture->height) * 4) {
        const float w = 1 - u - v;
        float tu = t.t0.x * w + t.t1.x * u + t.t2.x * v, tv = t.t0.y * w + t.t1.y * u + t.t2.y * v;
        tu -= std::floor(tu); tv -= std::floor(tv);
        const int x = std::min(m.texture->width - 1, int(tu * float(m.texture->width)));
        const int y = std::min(m.texture->height - 1, int(tv * float(m.texture->height)));
        const uint8_t* p = &m.texture->rgba[(size_t(y) * size_t(m.texture->width) + size_t(x)) * 4];
        auto lin = [](uint8_t c) { const float s = float(c) / 255.0f; return s * (s * (s * 0.305306011f + 0.682171111f) + 0.012522878f); };
        a = mulv(a, {lin(p[0]), lin(p[1]), lin(p[2])});
    }
    return {std::min(a.x, 0.98f), std::min(a.y, 0.98f), std::min(a.z, 0.98f)};
}

Surface SceneRT::surface(const BvhHit& h, float3 org, float3 dir) const {
    const Tri& t = tris[h.tri];
    const float w = 1 - h.u - h.v;
    Surface s;
    s.pos = org + dir * h.t;
    s.n = normalize(t.n0 * w + t.n1 * h.u + t.n2 * h.v);
    const BakeMaterial& m = scene->materials[t.material];
    s.backface = dot(t.fn, dir) > 0;
    if (s.backface && m.doubleSided) { s.backface = false; s.n = -s.n; }
    else if (dot(s.n, t.fn) < 0) s.n = t.fn;   // broken normals: fall back to the face
    s.albedo = albedoAt(t, h.u, h.v);
    s.emissive = m.emissive;
    return s;
}

float3 SceneRT::direct(float3 pos, float3 n, Rng& rng, bool withSun, int lightsFilter) const {
    float3 sum{0, 0, 0};
    if (withSun && scene->sun.on) {
        const float3 d = coneDir(sunDir, sunCos, rng.uniform(), rng.uniform());
        const float ndl = dot(n, d);
        if (ndl > 0 && !bvh.occluded(pos + n * eps, d, 1e30f)) sum += sunColor * ndl;
    }
    if (lightsFilter > 0)
        for (const auto& l : scene->lights) {
            if (lightsFilter == 1 && !l.bakedOnly) continue;
            float3 d = l.pos - pos;
            const float d2 = dot(d, d);
            if (d2 >= l.radius * l.radius || d2 < 1e-8f) continue;
            float a = 1.0f - d2 / (l.radius * l.radius);
            a *= a;
            const float dist = std::sqrt(d2);
            d = d * (1.0f / dist);
            if (l.cosOuter > -1.5f) {
                const float c = dot(-d, normalize(l.dir));
                const float sp = std::clamp((c - l.cosOuter) / std::max(l.cosInner - l.cosOuter, 1e-4f), 0.0f, 1.0f);
                const float s3 = sp * sp * (3 - 2 * sp);
                a *= s3 * s3;
            }
            const float ndl = dot(n, d);
            if (ndl <= 0 || a <= 0) continue;
            if (!bvh.occluded(pos + n * eps, d, dist - eps * 2)) sum += l.color * (a * ndl);
        }
    return sum;
}

float3 SceneRT::radiance(float3 org, float3 dir, Rng& rng, int bounces, bool* backfaceFirst, float* firstT) const {
    float3 result{0, 0, 0}, through{1, 1, 1};
    for (int b = 0;; ++b) {
        BvhHit h;
        if (!bvh.intersect(org, dir, 1e30f, h)) {
            result += mulv(through, sky(dir));
            if (b == 0 && firstT) *firstT = 1e30f;
            break;
        }
        const Surface s = surface(h, org, dir);
        if (b == 0) { if (backfaceFirst) *backfaceFirst = s.backface; if (firstT) *firstT = h.t; }
        if (s.backface) break;   // inside closed geometry: no light
        result += mulv(through, s.emissive);
        if (b >= bounces) break;
        result += mulv(mulv(through, s.albedo), direct(s.pos, s.n, rng, true, 2));
        if (b + 1 >= bounces) break;
        through = mulv(through, s.albedo);
        if (std::max({through.x, through.y, through.z}) < 0.01f) break;
        dir = cosineDir(s.n, rng.uniform(), rng.uniform());
        org = s.pos + s.n * eps;
    }
    return result;
}

bool buildSceneRT(const BakeScene& scene, const BakeSettings& settings, SceneRT& rt, std::string& error) {
    rt.scene = &scene; rt.settings = &settings;
    rt.bmin = {1e30f, 1e30f, 1e30f}; rt.bmax = {-1e30f, -1e30f, -1e30f};
    std::vector<BvhTri> bt;
    std::vector<uint8_t> masked;
    for (size_t ii = 0; ii < scene.instances.size(); ++ii) {
        const BakeInstance& in = scene.instances[ii];
        if (!in.cast) continue;
        if (in.mesh < 0 || size_t(in.mesh) >= scene.meshes.size()) { error = "instance '" + in.name + "': bad mesh"; return false; }
        const BakeMesh& m = scene.meshes[size_t(in.mesh)];
        const mat4 nm = in.world.inverse();   // normals: inverse transpose
        auto nrmW = [&](float3 n) {
            return normalize(float3{nm.at(0, 0) * n.x + nm.at(1, 0) * n.y + nm.at(2, 0) * n.z,
                                    nm.at(0, 1) * n.x + nm.at(1, 1) * n.y + nm.at(2, 1) * n.z,
                                    nm.at(0, 2) * n.x + nm.at(1, 2) * n.y + nm.at(2, 2) * n.z});
        };
        const bool mirrored = dot(cross(in.world.transformDir({1, 0, 0}), in.world.transformDir({0, 1, 0})), in.world.transformDir({0, 0, 1})) < 0;
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
            uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
            if (a >= m.pos.size() || b >= m.pos.size() || c >= m.pos.size()) { error = "instance '" + in.name + "': index out of range"; return false; }
            if (mirrored) std::swap(b, c);
            const float3 p0 = in.world.transformPoint(m.pos[a]), p1 = in.world.transformPoint(m.pos[b]), p2 = in.world.transformPoint(m.pos[c]);
            const float3 fn = cross(p1 - p0, p2 - p0);
            if (dot(fn, fn) < 1e-20f) continue;
            Tri tr;
            tr.fn = normalize(fn);
            auto vn = [&](uint32_t i) { return i < m.nrm.size() ? nrmW(m.nrm[i]) : tr.fn; };
            tr.n0 = vn(a); tr.n1 = vn(b); tr.n2 = vn(c);
            auto uv = [&](uint32_t i) { return i < m.uv.size() ? m.uv[i] : float2{}; };
            tr.t0 = uv(a); tr.t1 = uv(b); tr.t2 = uv(c);
            tr.material = t / 3 < m.triMaterial.size() ? m.triMaterial[t / 3] : 0;
            if (!in.materials.empty()) tr.material = tr.material < in.materials.size() ? in.materials[tr.material] : in.materials.back();
            if (tr.material >= scene.materials.size()) { error = "instance '" + in.name + "': bad material"; return false; }
            tr.instance = int32_t(ii);
            const auto& mat = scene.materials[tr.material];
            masked.push_back(mat.alphaCutoff >= 0 && mat.texture ? 1 : 0);
            bt.push_back({p0, p1 - p0, p2 - p0, uint32_t(rt.tris.size())});
            rt.tris.push_back(tr);
            for (float3 p : {p0, p1, p2}) { rt.bmin = render::min3(rt.bmin, p); rt.bmax = render::max3(rt.bmax, p); }
        }
    }
    if (rt.tris.empty()) { error = "nothing to bake: the scene has no static geometry"; return false; }
    rt.bvh.build(std::move(bt));
    rt.bvh.setAlphaTest(std::move(masked), [&rt](uint32_t id, float u, float v) {
        const Tri& t = rt.tris[id];
        const BakeMaterial& m = rt.scene->materials[t.material];
        const auto& img = *m.texture;
        const float w = 1 - u - v;
        float tu = t.t0.x * w + t.t1.x * u + t.t2.x * v, tv = t.t0.y * w + t.t1.y * u + t.t2.y * v;
        tu -= std::floor(tu); tv -= std::floor(tv);
        const int x = std::min(img.width - 1, int(tu * float(img.width))), y = std::min(img.height - 1, int(tv * float(img.height)));
        const size_t at = (size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3;
        return at >= img.rgba.size() || float(img.rgba[at]) / 255.0f >= m.alphaCutoff;
    });
    const float3 ext = rt.bmax - rt.bmin;
    rt.eps = std::max(1e-4f, std::max({ext.x, ext.y, ext.z}) * 2e-5f + 0.01f / std::max(settings.texelsPerUnit, 0.01f));
    rt.sunDir = normalize(scene.sun.dir);
    rt.sunColor = scene.sun.color;
    rt.sunCos = std::cos(std::clamp(scene.sun.angle, 0.0f, 20.0f) * 0.5f * 0.017453292f);
    return true;
}

} // namespace detail

namespace {

struct Region { int inst = -1; int w = 0, h = 0; int page = -1, x = 0, y = 0; };   // w, h without padding

// Shelf packing, largest first; pages grow to pageSize, the last page shrinks to what it needs.
bool packRegions(std::vector<Region>& regs, int pageSize, int pad, std::vector<std::pair<int, int>>& pageDims) {
    std::vector<int> order(regs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return regs[size_t(a)].h != regs[size_t(b)].h ? regs[size_t(a)].h > regs[size_t(b)].h : regs[size_t(a)].w > regs[size_t(b)].w; });
    int page = -1, cx = 0, cy = 0, shelfH = 0, usedW = 0;
    auto newPage = [&] { ++page; cx = cy = shelfH = usedW = 0; pageDims.push_back({0, 0}); };
    for (int i : order) {
        Region& r = regs[size_t(i)];
        const int w = r.w + pad * 2, h = r.h + pad * 2;
        if (w > pageSize || h > pageSize) return false;
        if (page < 0) newPage();
        if (cx + w > pageSize) { cy += shelfH; cx = 0; shelfH = 0; }
        if (cy + h > pageSize) { newPage(); }
        r.page = page; r.x = cx; r.y = cy;
        cx += w; shelfH = std::max(shelfH, h);
        usedW = std::max(usedW, cx);
        auto& d = pageDims[size_t(page)];
        d.first = std::max(d.first, cx); d.second = std::max(d.second, cy + shelfH);
    }
    for (auto& d : pageDims) {   // power-of-two pages (mobile GPUs, block compression later)
        int w = 4, h = 4;
        while (w < d.first) w *= 2;
        while (h < d.second) h *= 2;
        d = {w, h};
    }
    return true;
}

float triArea2(float2 a, float2 b, float2 c) { return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)); }

struct Texel {
    float3 pos, nrm, fn;
    int32_t region = -1;
};

} // namespace

bool bakeLighting(const BakeScene& scene, const BakeSettings& settingsIn, LightmapData& out, std::string& error,
                  const ProgressFn& progress, BakeStats* stats) {
    const auto started = std::chrono::steady_clock::now();
    BakeSettings st = settingsIn;
    st.samples = std::clamp(st.samples, 1, 16384);
    st.bounces = std::clamp(st.bounces, 0, 8);
    st.pageSize = std::clamp(st.pageSize, 64, 8192);
    st.padding = std::clamp(st.padding, 1, 16);
    st.maskSamples = std::clamp(st.maskSamples, 1, 1024);
    const int threads = st.threads > 0 ? st.threads : std::max(1u, std::thread::hardware_concurrency());
    SceneRT rt;
    if (!buildSceneRT(scene, st, rt, error)) return false;
    if (progress && !progress("scene", 1.0f)) { error = "cancelled"; return false; }

    // ---- regions
    std::vector<Region> regs;
    for (size_t ii = 0; ii < scene.instances.size(); ++ii) {
        const BakeInstance& in = scene.instances[ii];
        if (!in.receive || in.mesh < 0 || size_t(in.mesh) >= scene.meshes.size()) continue;
        const BakeMesh& m = scene.meshes[size_t(in.mesh)];
        if (m.uv2.size() != m.pos.size()) continue;
        Region r; r.inst = int(ii);
        const float tpu = st.texelsPerUnit * std::max(in.scale, 0.01f);
        if (int(ii) == scene.terrain.instance) {
            r.w = std::clamp(int(std::ceil(scene.terrain.extent[0] * tpu)), st.minRegion, st.pageSize - st.padding * 2);
            r.h = std::clamp(int(std::ceil(scene.terrain.extent[1] * tpu)), st.minRegion, st.pageSize - st.padding * 2);
        } else {
            double aw = 0, au = 0;
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                const float3 p0 = in.world.transformPoint(m.pos[m.idx[t]]), p1 = in.world.transformPoint(m.pos[m.idx[t + 1]]), p2 = in.world.transformPoint(m.pos[m.idx[t + 2]]);
                aw += 0.5 * double(length(cross(p1 - p0, p2 - p0)));
                au += 0.5 * double(triArea2(m.uv2[m.idx[t]], m.uv2[m.idx[t + 1]], m.uv2[m.idx[t + 2]]));
            }
            if (aw <= 0 || au <= 1e-12) continue;
            const int side = int(std::ceil(double(tpu) * std::sqrt(aw / au)));
            r.w = r.h = std::clamp(side, st.minRegion, std::min(st.maxRegion, st.pageSize - st.padding * 2));
        }
        regs.push_back(r);
    }
    std::vector<std::pair<int, int>> pageDims;
    if (!packRegions(regs, st.pageSize, st.padding, pageDims)) { error = "a lightmap region does not fit a page (raise pageSize)"; return false; }

    // ---- rasterize into texel G-buffers
    out = LightmapData{};
    out.mode = st.mode;
    out.rgbmRange = st.rgbmRange;
    std::vector<std::vector<Texel>> gbuf(pageDims.size());
    for (size_t p = 0; p < pageDims.size(); ++p) gbuf[p].resize(size_t(pageDims[p].first) * size_t(pageDims[p].second));
    for (size_t ri = 0; ri < regs.size(); ++ri) {
        const Region& r = regs[ri];
        const BakeInstance& in = scene.instances[size_t(r.inst)];
        const BakeMesh& m = scene.meshes[size_t(in.mesh)];
        const int W = pageDims[size_t(r.page)].first;
        auto& g = gbuf[size_t(r.page)];
        const mat4 nm = in.world.inverse();
        auto nrmW = [&](float3 n) {
            return normalize(float3{nm.at(0, 0) * n.x + nm.at(1, 0) * n.y + nm.at(2, 0) * n.z,
                                    nm.at(0, 1) * n.x + nm.at(1, 1) * n.y + nm.at(2, 1) * n.z,
                                    nm.at(0, 2) * n.x + nm.at(1, 2) * n.y + nm.at(2, 2) * n.z});
        };
        const bool mirrored = dot(cross(in.world.transformDir({1, 0, 0}), in.world.transformDir({0, 1, 0})), in.world.transformDir({0, 0, 1})) < 0;
        // two passes: texel centres inside triangles, then texels the triangles only touch (conservative)
        for (int pass = 0; pass < 2; ++pass)
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                const uint32_t ia = m.idx[t], ib = m.idx[t + 1], ic = m.idx[t + 2];
                const float3 p[3] = {in.world.transformPoint(m.pos[ia]), in.world.transformPoint(m.pos[ib]), in.world.transformPoint(m.pos[ic])};
                float3 fn = cross(p[1] - p[0], p[2] - p[0]);
                if (dot(fn, fn) < 1e-20f) continue;
                fn = normalize(fn);
                if (mirrored) fn = -fn;
                const float3 n[3] = {ia < m.nrm.size() ? nrmW(m.nrm[ia]) : fn, ib < m.nrm.size() ? nrmW(m.nrm[ib]) : fn, ic < m.nrm.size() ? nrmW(m.nrm[ic]) : fn};
                float2 q[3];
                const uint32_t ix[3] = {ia, ib, ic};
                for (int k = 0; k < 3; ++k) q[k] = {m.uv2[ix[k]].x * float(r.w) + float(r.x + st.padding), m.uv2[ix[k]].y * float(r.h) + float(r.y + st.padding)};
                const float area = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y);
                if (std::fabs(area) < 1e-12f) continue;
                const float grow = pass == 0 ? 0.0f : 0.75f;
                const int x0 = std::max(r.x, int(std::floor(std::min({q[0].x, q[1].x, q[2].x}) - grow)));
                const int x1 = std::min(r.x + r.w + st.padding * 2 - 1, int(std::ceil(std::max({q[0].x, q[1].x, q[2].x}) + grow)));
                const int y0 = std::max(r.y, int(std::floor(std::min({q[0].y, q[1].y, q[2].y}) - grow)));
                const int y1 = std::min(r.y + r.h + st.padding * 2 - 1, int(std::ceil(std::max({q[0].y, q[1].y, q[2].y}) + grow)));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        Texel& tx = g[size_t(y) * size_t(W) + size_t(x)];
                        if (pass == 1 && tx.region >= 0) continue;
                        const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                        float b1 = ((px - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (py - q[0].y)) / area;
                        float b2 = ((q[1].x - q[0].x) * (py - q[0].y) - (px - q[0].x) * (q[1].y - q[0].y)) / area;
                        float b0 = 1 - b1 - b2;
                        if (pass == 0) {
                            if (b0 < -1e-5f || b1 < -1e-5f || b2 < -1e-5f) continue;
                        } else {
                            // distance of the texel centre from the triangle in texels must be under ~0.75
                            const float lim = -grow * 2.0f / std::sqrt(std::fabs(area));
                            if (b0 < lim || b1 < lim || b2 < lim) continue;
                            b0 = std::clamp(b0, 0.0f, 1.0f); b1 = std::clamp(b1, 0.0f, 1.0f); b2 = std::clamp(b2, 0.0f, 1.0f);
                            const float s = b0 + b1 + b2; b0 /= s; b1 /= s; b2 /= s;
                        }
                        tx.pos = p[0] * b0 + p[1] * b1 + p[2] * b2;
                        tx.nrm = normalize(n[0] * b0 + n[1] * b1 + n[2] * b2);
                        if (dot(tx.nrm, fn) < 0.05f) tx.nrm = fn;
                        tx.fn = fn;
                        tx.region = int32_t(ri);
                    }
            }
    }

    // ---- integrate
    int texels = 0;
    for (auto& g : gbuf) for (auto& t : g) texels += t.region >= 0;
    const bool subtractive = st.mode == BakedMode::Subtractive;
    const float texelWorld = 1.0f / std::max(st.texelsPerUnit, 0.01f);
    struct Result { std::vector<float> indirect, direct; std::vector<float> mask; std::vector<uint8_t> valid; };
    std::vector<Result> res(pageDims.size());
    for (size_t p = 0; p < pageDims.size(); ++p) {
        const int W = pageDims[p].first, H = pageDims[p].second;
        auto& R = res[p];
        R.indirect.assign(size_t(W) * size_t(H) * 3, 0.0f); R.direct.assign(R.indirect.size(), 0.0f);
        R.mask.assign(size_t(W) * size_t(H), 1.0f); R.valid.assign(size_t(W) * size_t(H), 0);
        const auto& g = gbuf[p];
        const std::string stage = "lightmap " + std::to_string(p + 1) + "/" + std::to_string(pageDims.size());
        const bool ok = parallelFor(H, threads, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t at = size_t(y) * size_t(W) + size_t(x);
                const Texel& tx = g[at];
                if (tx.region < 0) continue;
                Rng rng(mixKey(mixKey(st.seed, p), at));
                const float3 org = tx.pos + tx.fn * rt.eps;
                const float r1 = rng.uniform(), r2 = rng.uniform();
                float3 sum{0, 0, 0};
                int back = 0, aoHits = 0;
                for (int s = 0; s < st.samples; ++s) {
                    // R2 low-discrepancy sequence, rotated per texel
                    float u1 = r1 + 0.7548776662f * float(s + 1), u2 = r2 + 0.5698402910f * float(s + 1);
                    u1 -= std::floor(u1); u2 -= std::floor(u2);
                    const float3 d = cosineDir(tx.nrm, u1, u2);
                    if (dot(d, tx.fn) <= 0) continue;
                    bool bf = false; float t = 0;
                    sum += rt.radiance(org, d, rng, st.bounces, &bf, &t);
                    if (bf && t < texelWorld * 2.0f) ++back;
                    if (st.aoDistance > 0 && t < st.aoDistance) ++aoHits;
                }
                if (back * 10 > st.samples) continue;   // texel buried in geometry: dilation fills it
                float3 ind = sum * (st.indirectIntensity / float(st.samples));
                if (st.aoDistance > 0) ind = ind * std::pow(std::max(0.0f, 1.0f - float(aoHits) / float(st.samples)), st.aoStrength);
                float vis = 0;
                if (scene.sun.on) {
                    for (int s = 0; s < st.maskSamples; ++s) {
                        const float3 d = coneDir(rt.sunDir, rt.sunCos, rng.uniform(), rng.uniform());
                        if (!rt.bvh.occluded(org, d, 1e30f)) vis += 1;
                    }
                    vis /= float(st.maskSamples);
                }
                float3 dir = rt.direct(org, tx.nrm, rng, false, 1);
                if (subtractive && scene.sun.on) dir += rt.sunColor * (std::max(0.0f, dot(tx.nrm, rt.sunDir)) * vis);
                R.indirect[at * 3] = ind.x; R.indirect[at * 3 + 1] = ind.y; R.indirect[at * 3 + 2] = ind.z;
                R.direct[at * 3] = dir.x; R.direct[at * 3 + 1] = dir.y; R.direct[at * 3 + 2] = dir.z;
                R.mask[at] = vis;
                R.valid[at] = 1;
            }
        }, stage, progress);
        if (!ok) { error = "cancelled"; return false; }
    }

    // ---- denoise, dilate, encode
    for (size_t p = 0; p < pageDims.size(); ++p) {
        const int W = pageDims[p].first, H = pageDims[p].second;
        auto& R = res[p];
        const auto& g = gbuf[p];
        std::vector<int32_t> id(size_t(W) * size_t(H), -1);
        std::vector<float3> nrm(id.size()), pos(id.size());
        for (size_t i = 0; i < id.size(); ++i) if (R.valid[i]) { id[i] = g[i].region; nrm[i] = g[i].nrm; pos[i] = g[i].pos; }
        if (st.denoise) denoiseTexels(R.indirect, nrm, pos, id, W, H, texelWorld);
        // dilation inside each region's padded rect: invalid texels take the mean of valid neighbours
        std::vector<int32_t> owner(id.size(), -1);
        for (size_t ri = 0; ri < regs.size(); ++ri) {
            const Region& r = regs[ri];
            if (r.page != int(p)) continue;
            for (int y = r.y; y < r.y + r.h + st.padding * 2; ++y)
                for (int x = r.x; x < r.x + r.w + st.padding * 2; ++x) owner[size_t(y) * size_t(W) + size_t(x)] = int32_t(ri);
        }
        for (int it = 0; it < st.padding + 6; ++it) {
            std::vector<uint8_t> nv = R.valid;
            bool changed = false;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const size_t at = size_t(y) * size_t(W) + size_t(x);
                    if (R.valid[at] || owner[at] < 0) continue;
                    float acc[7] = {}; int n = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                            const size_t b = size_t(yy) * size_t(W) + size_t(xx);
                            if (!R.valid[b] || owner[b] != owner[at]) continue;
                            for (int k = 0; k < 3; ++k) { acc[k] += R.indirect[b * 3 + size_t(k)]; acc[3 + k] += R.direct[b * 3 + size_t(k)]; }
                            acc[6] += R.mask[b]; ++n;
                        }
                    if (!n) continue;
                    for (int k = 0; k < 3; ++k) { R.indirect[at * 3 + size_t(k)] = acc[k] / float(n); R.direct[at * 3 + size_t(k)] = acc[3 + k] / float(n); }
                    R.mask[at] = acc[6] / float(n);
                    nv[at] = 1; changed = true;
                }
            R.valid.swap(nv);
            if (!changed) break;
        }
        LightmapPage pg;
        pg.width = W; pg.height = H;
        pg.rgbm.assign(size_t(W) * size_t(H) * 4, 0);
        pg.mask.assign(size_t(W) * size_t(H) * 4, 255);
        for (size_t i = 0; i < size_t(W) * size_t(H); ++i) {
            const float3 c{R.indirect[i * 3] + R.direct[i * 3], R.indirect[i * 3 + 1] + R.direct[i * 3 + 1], R.indirect[i * 3 + 2] + R.direct[i * 3 + 2]};
            encodeRgbm(c, st.rgbmRange, &pg.rgbm[i * 4]);
            const uint8_t mv = uint8_t(std::clamp(R.mask[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            pg.mask[i * 4] = pg.mask[i * 4 + 1] = pg.mask[i * 4 + 2] = mv;
        }
        if (st.mode != BakedMode::Shadowmask) pg.mask.clear();
        out.pages.push_back(std::move(pg));
    }
    for (size_t ri = 0; ri < regs.size(); ++ri) {
        const Region& r = regs[ri];
        const auto [W, H] = pageDims[size_t(r.page)];
        LightmapRegion lr;
        lr.page = uint16_t(r.page);
        lr.st[0] = float(r.w) / float(W); lr.st[1] = float(r.h) / float(H);
        lr.st[2] = float(r.x + st.padding) / float(W); lr.st[3] = float(r.y + st.padding) / float(H);
        if (r.inst == scene.terrain.instance) {
            out.terrain.page = r.page;
            std::copy(lr.st, lr.st + 4, out.terrain.st);
            out.terrain.origin[0] = scene.terrain.origin[0]; out.terrain.origin[1] = scene.terrain.origin[1];
            out.terrain.extent[0] = scene.terrain.extent[0]; out.terrain.extent[1] = scene.terrain.extent[1];
        } else {
            out.regions.emplace(scene.instances[size_t(r.inst)].key, lr);   // first wins for duplicate placements
        }
    }

    // ---- probes
    bool cancelled = false;
    if (st.probeSpacing > 0) bakeProbes(rt, out.probes, progress, cancelled);
    if (cancelled) { error = "cancelled"; return false; }

    out.hash = hashBakeInput(scene, settingsIn);
    out.settingsJson = settingsToJson(settingsIn);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    BakeStats s;
    s.instances = int(scene.instances.size()); s.regions = int(regs.size()); s.pages = int(out.pages.size());
    s.texels = texels; s.probes = int(out.probes.coeffs.size() / 12); s.triangles = int(rt.tris.size()); s.seconds = secs;
    out.statsJson = "{\"instances\": " + std::to_string(s.instances) + ", \"regions\": " + std::to_string(s.regions) + ", \"pages\": " + std::to_string(s.pages) +
                    ", \"texels\": " + std::to_string(s.texels) + ", \"probes\": " + std::to_string(s.probes) + ", \"triangles\": " + std::to_string(s.triangles) + "}";
    if (stats) *stats = s;
    return true;
}

} // namespace aiverse::bake
