// SpectraFX: a shared presentation particle world for ALL game genres.
// Multiple independent graph instances can enqueue effects here; the existing
// ParticleSystem batches their sprites/trails across effect and instance IDs
// whenever GPU texture, blend state, shader-graph and primitive type match.
// No global state, no FPS/RTS dependencies, no authoritative ECS mutation.
#pragma once

#include "VfxGraph.h"
#include "ParticleSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aiverse::spectra {

class BatchWorld final {
public:
    BatchWorld() = default;
    ~BatchWorld() { shutdown(); }
    BatchWorld(const BatchWorld&) = delete;
    BatchWorld& operator=(const BatchWorld&) = delete;

    // The caller owns Device and must shut down this presenter first.
    // Headless (nullptr) is supported for deterministic API smoke tests.
    bool init(gfx::Device* device, fx::FxQuality quality = fx::FxQuality::High);
    void shutdown();

    // Define once per distinct full effect data, not per instance. This
    // deduplicates equivalent definitions but NEVER merges distinct logic
    // merely because both effects refer to the same texture.
    int defineEffect(const fx::EffectDef& definition);
    int defineGraph(const Json& graph,
                    const Json& overrides = Json::object());
    fx::FxHandle play(int effect, fx::float3 position,
                      fx::float3 direction = {0,1,0}, float scale = 1,
                      fx::float4 tint = {1,1,1,1});
    fx::FxHandle playWithProgramParameters(
        int effect,fx::float3 position,fx::float3 direction,float scale,
        fx::float4 tint,const std::array<float,8>& params);

    // Presentation-only runtime parameters for a specific effect handle.
    // No graph rebuild and no new ECS component type.
    bool setProgramParameter(fx::FxHandle h, uint32_t slot, float value,
                             bool affectExisting = false);
    void move(fx::FxHandle h, fx::float3 position);
    void move(fx::FxHandle h, fx::float3 position, fx::float3 direction);
    void stop(fx::FxHandle h);
    void kill(fx::FxHandle h);
    bool playing(fx::FxHandle h) const;
    // Instance still exists (emitting or owning live particles); handle
    // lists use it to drop finished handles instead of growing to a cap.
    bool alive(fx::FxHandle h) const;

    // EXACTLY ONCE per presentation frame, after all emitters registered
    // their births/moves. draw() EXACTLY ONCE during the scene render pass.
    // Never call update()/draw() independently from each GraphRuntime.
    void setView(const fx::SpectraFxView& view);
    template<class Camera>
    void setView(const Camera& camera,const render::mat4& projection,int width,int height) {
        return setView(fx::SpectraFxView::fromCamera(camera,projection,width,height));
    }
    void update(float dt);
    void draw(const fx::FxFog& fog = {});
    void collectLights(std::vector<fx::FxPointLight>& lights, int maxLights = 32) const;

    // Clear the entire match/scene, including all connected graph instances.
    // For one graph's particles use GraphRuntime::clear() instead.
    void clear();
    bool ready() const { return initialized_; }
    const std::string& error() const { return error_; }
    const std::vector<std::string>& errors() const { return errors_; }
    const fx::FxStats& stats() const { return particles_.stats(); }
    const fx::ParticleSystem& particles() const { return particles_; }

    // Use before defineEffect; asset loaders keep game/platform-specific I/O
    // outside this universal presentation context.
    void setTextureLoader(fx::TextureLoader loader) {
        particles_.setTextureLoader(std::move(loader));
    }
    void setMeshLoader(fx::MeshLoader loader) {
        particles_.setMeshLoader(std::move(loader));
    }

private:
    fx::ParticleSystem particles_;
    // Complete serialized definition fingerprint; distinct effects stay
    // separate even when they share one atlas texture / RHI draw call.
    std::map<std::string,int> definitionIds_;
    bool initialized_ = false;
    std::string error_;
    std::vector<std::string> errors_;
};

} // namespace aiverse::spectra
