// Bounded CPU reference for *dynamic bounced diffuse* GI.
// This is a lighting-only overlay; direct light and baked GI remain independent.
// Do not run a full-world bake on the render thread. GPU backend comes later.
#pragma once
#include "GiUpdateScheduler.h"
#include "StaticDiffuseField.h"
#include "GiTraceGeometry.h"
#include "GiEnvironmentLight.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct GiMovingLight {
    uint64_t key = 0;     // stable external ID; does not change when light moves
    float3 position{};
    float3 color{1, 1, 1}; // linear RGB, never packed sRGB
    float radius = 4;
    float intensity = 1;
    // Optional spot cone. cosOuter <= -1.5 denotes omnidirectional point light.
    // Direction runs FROM the emitter toward illuminated surfaces, normalized.
    float3 direction{0,-1,0};
    float cosInner=-2, cosOuter=-2;
    bool spot() const { return cosOuter > -1.5f; }
};

struct GiDynamicCoefficient {
    // Each RGB row: {constant, x, y, z}; same L1 convention as StaticDiffuseProbe.
    std::array<float4, 3> irradiance{};
    float3 evaluate(float3 normal) const {
        const auto value = [&](const float4& c) {
            return std::max(0.0f, c.x + c.y * normal.x + c.z * normal.y + c.w * normal.z);
        };
        return {value(irradiance[0]), value(irradiance[1]), value(irradiance[2])};
    }
};

// CPU correctness reference and fallback for small volumes only. Single-threaded.
// Caller owns immutable BVH; rebake/rebuild it before publishing a new revision.
// Dynamic meshes require their own updated geometry snapshot (not handled here).
// In a production GPU path, the same scheduler tickets select bounded compute work.
class GiDynamicProbeReference {
public:
    struct Stats { size_t claimed=0, published=0, stale=0, primaryRays=0, visibilityRays=0; };

    void configure(float3 origin, float spacing, int nx, int ny, int nz,
                   int samples=32, float traceDistance=12) {
        if (samples < 8 || samples > 128 ||
            !std::isfinite(traceDistance) || traceDistance <= 0 || traceDistance > 1000)
            throw std::invalid_argument("invalid dynamic GI settings");
        scheduler_.configure(origin, spacing, nx, ny, nz);
        coeffs_.assign(scheduler_.size(), GiDynamicCoefficient{});
        sampleCount_ = samples;
        traceDistance_ = traceDistance;
        lights_.clear();
        environment_ = {};
        geometry_ = {};
        geometryRevision_ = 0;
    }

    GiUpdateScheduler& scheduler() { return scheduler_; }
    const GiUpdateScheduler& scheduler() const { return scheduler_; }
    const std::vector<GiDynamicCoefficient>& coefficients() const { return coeffs_; }
    const GiDynamicCoefficient& coefficient(uint32_t index) const { return coeffs_.at(index); }

    // Clear transport invalidated by world changes before shader sampling.
    // The caller first invalidates the corresponding scheduler tickets and
    // uploads these zero coefficients before rendering (including budget=0).
    void clearCoefficients(const std::vector<uint32_t>& ids) {
        for (uint32_t id:ids) coeffs_.at(id) = {};
    }
    void clearAllCoefficients() {
        std::fill(coeffs_.begin(), coeffs_.end(), GiDynamicCoefficient{});
    }

    const GiEnvironmentLight& environment() const { return environment_; }
    // Changes to distant sunlight or sky affect all probes, not only the
    // volume of one point/spot lamp. Dirty revisions retire in-flight tickets.
    // A caller must flush cleared coefficients to the GPU before sampling.
    bool setEnvironment(GiEnvironmentLight next,
                        std::vector<uint32_t>* cleared = nullptr) {
        if (!next.valid()) throw std::invalid_argument("invalid dynamic GI environment");
        if (next==environment_) return false;
        environment_=next;
        clearAllCoefficients();
        scheduler_.invalidateAll(GiDirtyReason::Environment);
        if (cleared)
            for (uint32_t i=0;i<coeffs_.size();++i) cleared->push_back(i);
        return true;
    }

    // A null BVH makes dynamic bounce zero, not "unchanged/stale".
    void setGeometry(const StaticShadowGeometry* geometry, uint64_t revision) {
        setTraceGeometry(GiTraceGeometryView::fromStatic(geometry), revision);
    }

    // New chunk snapshots and old monolithic BVH both implement GiTraceGeometryView.
    void setTraceGeometry(GiTraceGeometryView geometry, uint64_t revision) {
        if (geometry == geometry_ && revision == geometryRevision_) return;
        if (bool(geometry.source) != bool(geometry.query))
            throw std::invalid_argument("incomplete GI geometry view");
        geometry_ = geometry;
        geometryRevision_ = revision;
        // Light transport from the previous obstacle layout is now invalid.
        // Prefer a temporarily dark result to ghost light leaking through a removed wall.
        std::fill(coeffs_.begin(), coeffs_.end(), GiDynamicCoefficient{});
        if (!geometry_) scheduler_.resetClean();
        else scheduler_.invalidateAll(GiDirtyReason::Geometry);
    }

    // After the host has rebuilt the entire immutable BVH snapshot, local edits can
    // invalidate only probes whose primary/secondary rays could depend on the edit.
    // An initial/no-geometry snapshot must instead use setGeometry() (full reset).
    // `influenceRadius` is an optional larger safety margin; the conservative
    // minimum includes max probe tracing distance and the widest light radius.
    // The new geometry snapshot must stay alive until the next replacement.
    size_t setGeometryRegion(const StaticShadowGeometry* geometry, uint64_t revision,
                             GiBounds changed, float influenceRadius = 0,
                             std::vector<uint32_t>* cleared = nullptr) {
        return setTraceGeometryRegion(GiTraceGeometryView::fromStatic(geometry),
                                      revision, changed, influenceRadius, cleared);
    }

    size_t setTraceGeometryRegion(GiTraceGeometryView geometry, uint64_t revision,
                                  GiBounds changed, float influenceRadius = 0,
                                  std::vector<uint32_t>* cleared = nullptr) {
        if (!geometry || !geometry_)
            throw std::logic_error("regional GI geometry requires both snapshots");
        if (geometry == geometry_ && revision == geometryRevision_) return 0;
        float radius = traceDistance_;
        for (const auto& source : lights_) radius = std::max(radius, traceDistance_ + source.radius);
        radius = std::max(radius, influenceRadius);
        std::vector<uint32_t> affected;
        // A failed invalidation must not replace the previously valid ray-query snapshot.
        scheduler_.invalidate(changed, GiDirtyReason::Geometry, radius, &affected);
        geometry_ = geometry;
        geometryRevision_ = revision;
        for (auto id : affected) {
            coeffs_[id] = {};
            if (cleared) cleared->push_back(id);
        }
        return affected.size();
    }

    // Must be called after ECS light-change barrier with a stable key per source.
    // Only changed/new/removed light volumes are invalidated, expanded by traceDistance
    // to account for light -> surface -> probe transport rather than direct light alone.
    void setLights(std::vector<GiMovingLight> current,
                   std::vector<uint32_t>* cleared = nullptr) {
        if (current.size() > 64) throw std::invalid_argument("too many dynamic GI lights");
        const auto valid = [](const GiMovingLight& l) {
            const auto finite3 = [](float3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
            return finite3(l.position) && finite3(l.color) &&
                   l.color.x >= 0 && l.color.y >= 0 && l.color.z >= 0 &&
                   l.color.x <= 10000 && l.color.y <= 10000 && l.color.z <= 10000 &&
                   std::isfinite(l.radius) && l.radius > 0 && l.radius <= 1000 &&
                   std::isfinite(l.intensity) && l.intensity >= 0 && l.intensity <= 10000 &&
                   std::isfinite(l.cosOuter) && std::isfinite(l.cosInner) &&
                   (!l.spot() || (finite3(l.direction) &&
                       std::abs(dot(l.direction,l.direction)-1.0f)<.002f &&
                       l.cosOuter>=-1.0f && l.cosOuter<1.0f &&
                       l.cosInner>=l.cosOuter && l.cosInner<=1.0f));
        };
        for (const auto& light : current)
            if (!valid(light)) throw std::invalid_argument("invalid dynamic GI light");
        std::sort(current.begin(), current.end(),
                  [](const GiMovingLight& a, const GiMovingLight& b) { return a.key < b.key; });
        for (size_t i = 1; i < current.size(); ++i)
            if (current[i].key == current[i - 1].key)
                throw std::invalid_argument("duplicate dynamic GI light ID");

        auto changed = [](const GiMovingLight& a, const GiMovingLight& b) {
            return a.position.x != b.position.x || a.position.y != b.position.y ||
                   a.position.z != b.position.z || a.color.x != b.color.x ||
                   a.color.y != b.color.y || a.color.z != b.color.z ||
                   a.radius != b.radius || a.intensity != b.intensity ||
                   a.direction.x != b.direction.x || a.direction.y != b.direction.y ||
                   a.direction.z != b.direction.z ||
                   a.cosInner != b.cosInner || a.cosOuter != b.cosOuter;
        };
        const auto find = [](const std::vector<GiMovingLight>& src, uint64_t key) -> const GiMovingLight* {
            const auto it = std::lower_bound(src.begin(), src.end(), key,
                                             [](const GiMovingLight& a, uint64_t k) { return a.key < k; });
            return it != src.end() && it->key == key ? &*it : nullptr;
        };
        for (const auto& old : lights_) {
            const auto* newer = find(current, old.key);
            if (!newer || changed(old, *newer)) invalidateLight(old, cleared);
        }
        for (const auto& next : current) {
            const auto* old = find(lights_, next.key);
            if (!old || changed(*old, next)) invalidateLight(next, cleared);
        }
        lights_ = std::move(current);
    }

    // Budget controls number of probes rebuilt, not their light count or shader pixels.
    // Zero budget is a no-op. Caller must publish the independent dynamic texture after
    // receiving new coefficients; CPU publication here does not touch GPU state.
    Stats update(size_t probeBudget, float3 camera, std::vector<uint32_t>* published = nullptr) {
        Stats stats;
        if (!geometry_ || probeBudget == 0) return stats;
        const auto tickets = scheduler_.claim(probeBudget, camera);
        stats.claimed = tickets.size();
        for (const auto& t : tickets) {
            GiDynamicCoefficient out = compute(t.position, stats);
            if (scheduler_.complete(t)) {
                coeffs_[t.index] = out;
                if (published) published->push_back(t.index);
                ++stats.published;
            } else ++stats.stale;
        }
        return stats;
    }

private:
    // Invalidation also retires old coefficients immediately: a removed or moving
    // light must not keep illuminating Vulkan/GLES/Metal while the next frame's
    // bounded tracing budget catches up. The host uploads these cleared IDs.
    void invalidateLight(const GiMovingLight& light, std::vector<uint32_t>* cleared) {
        const float r = light.radius + traceDistance_;
        const float3 ext{r,r,r};
        std::vector<uint32_t> affected;
        scheduler_.invalidate({light.position-ext, light.position+ext},
                              GiDirtyReason::Lighting, 0, &affected);
        for (uint32_t id : affected) {
            coeffs_[id] = {};
            if (cleared) cleared->push_back(id);
        }
    }
    static float3 direction(int i, int count) {
        const float y = 1 - 2 * (float(i)+.5f)/float(count);
        const float phi = float(i)*2.39996323f;
        const float radius = std::sqrt(std::max(0.0f,1-y*y));
        return {radius*std::cos(phi),y,radius*std::sin(phi)};
    }
    GiDynamicCoefficient compute(float3 p, Stats& stats) const {
        GiDynamicCoefficient result;
        if (lights_.empty() && !environment_.enabled()) return result;
        auto add = [&](float3 rgb, float3 dir) {
            const float colors[3] = {rgb.x,rgb.y,rgb.z};
            const float components[3] = {dir.x,dir.y,dir.z};
            for (size_t c = 0; c < 3; ++c) {
                auto& sh = result.irradiance[c];
                const float v = colors[c] / float(sampleCount_);
                sh.x += v; sh.y += 2*v*components[0];
                sh.z += 2*v*components[1]; sh.w += 2*v*components[2];
            }
        };
        for (int i = 0; i < sampleCount_; ++i) {
            const auto dir = direction(i, sampleCount_);
            float3 normal{}, albedo{};
            ++stats.primaryRays;
            const float distance = geometry_.distance(p, dir, traceDistance_, &normal, &albedo);
            if (distance >= traceDistance_ || distance <= .05f || dot(normal,normal) < .5f)
                continue;
            if (dot(normal, -dir) < 0) normal = -normal; // receiver side faces probe
            const float3 surface = p + dir*distance + normal*.03f;
            float3 outgoing{};
            for (const auto& light : lights_) {
                if (light.intensity == 0) continue;
                const float3 delta = light.position - surface;
                const float d2 = dot(delta,delta);
                if (d2 < .0025f || d2 >= light.radius*light.radius) continue;
                const float d = std::sqrt(d2);
                const float3 towardLight = delta*(1/d);
                const float ndl = std::max(0.0f, dot(normal,towardLight));
                if (ndl == 0) continue;
                float cone=1.0f;
                if (light.spot()) {
                    // The surface sees the light along -towardLight.
                    const float angle=dot(light.direction,-towardLight);
                    const float width=std::max(light.cosInner-light.cosOuter,1e-5f);
                    cone=std::clamp((angle-light.cosOuter)/width,0.0f,1.0f);
                    if (cone<=0) continue;
                }
                // Static BVH is only a reference. Dynamic blockers require a new snapshot.
                ++stats.visibilityRays;
                if (geometry_.distance(surface, towardLight, d) < d - .03f) continue;
                float falloff = 1.0f - d2/(light.radius*light.radius);
                falloff *= falloff;
                const float strength = light.intensity * ndl * cone * falloff / (1.0f + d2);
                outgoing += float3{light.color.x*albedo.x,light.color.y*albedo.y,
                                   light.color.z*albedo.z} * strength;
            }
            // Sun/sky terms illuminate the hit SURFACE, not the camera/probe
            // directly. We project only that surface's indirect re-emission.
            if (environment_.sunIntensity>0) {
                const float ndl=std::max(0.0f,dot(normal,environment_.sunDirection));
                if (ndl>0) {
                    ++stats.visibilityRays;
                    if (geometry_.distance(surface,environment_.sunDirection,traceDistance_) >=
                        traceDistance_-.03f) {
                        outgoing += float3{environment_.sunColor.x*albedo.x,
                                           environment_.sunColor.y*albedo.y,
                                           environment_.sunColor.z*albedo.z} *
                                    (environment_.sunIntensity*ndl);
                    }
                }
            }
            if (environment_.skyIntensity>0) {
                const float skyCos=std::max(0.0f,normal.y);
                if (skyCos>0) {
                    // Explicit bounded +Y overhead visibility. Occluded by roof
                    // and any other published opaque geometry.
                    ++stats.visibilityRays;
                    if (geometry_.distance(surface,{0,1,0},traceDistance_) >=
                        traceDistance_-.03f) {
                        outgoing += float3{environment_.skyColor.x*albedo.x,
                                           environment_.skyColor.y*albedo.y,
                                           environment_.skyColor.z*albedo.z} *
                                    (environment_.skyIntensity*skyCos);
                    }
                }
            }
            // The same first-order directional convention used by the baked L1 probes.
            // No direct-light term is inserted; the bounced radiance is a separate delta.
            add(outgoing,dir);
        }
        return result;
    }

    GiUpdateScheduler scheduler_;
    std::vector<GiDynamicCoefficient> coeffs_;
    std::vector<GiMovingLight> lights_;
    GiEnvironmentLight environment_{};
    GiTraceGeometryView geometry_{};
    uint64_t geometryRevision_ = 0;
    int sampleCount_ = 32;
    float traceDistance_ = 12;
};

} // namespace aiverse::render
