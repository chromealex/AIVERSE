#include "SpectraBatchWorld.h"
#include <utility>

namespace aiverse::spectra {

bool BatchWorld::init(gfx::Device* device, fx::FxQuality quality) {
    shutdown();
    particles_.setQuality(quality);
    if (!particles_.init(device)) {
        error_ = "SpectraFX shared ParticleSystem init failed: " + particles_.lastError();
        return false;
    }
    initialized_ = true;
    return true;
}

void BatchWorld::shutdown() {
    particles_.shutdown();
    definitionIds_.clear();
    initialized_ = false;
    error_.clear();
    errors_.clear();
}

int BatchWorld::defineEffect(const fx::EffectDef& definition) {
    if (!initialized_) {
        error_ = "SpectraFX BatchWorld must be initialized before defining effects";
        return -1;
    }
    if (definition.emitters.empty()) {
        error_ = "SpectraFX shared effect requires at least one emitter";
        return -1;
    }
    // Canonical full definitions (including all modules), not texture names,
    // are the effect identity. Rendering batches are independently keyed by
    // the existing ParticleSystem::batchFor material state.
    const std::string identity = fx::effectToJson(definition).dump(-1);
    const auto existing = definitionIds_.find(identity);
    if (existing != definitionIds_.end()) return existing->second;

    const std::string internalName = "__spectrafx_batch_" +
                                     std::to_string(definitionIds_.size());
    const int id = particles_.define(internalName, definition);
    if (id < 0) {
        error_ = "SpectraFX shared ParticleSystem rejected an effect";
        for (const auto& e : particles_.errors()) errors_.push_back(e);
        return -1;
    }
    definitionIds_.emplace(identity, id);
    error_.clear();
    return id;
}

int BatchWorld::defineGraph(const Json& graph, const Json& overrides) {
    fx::EffectDef definition;
    errors_.clear();
    if (!fx::compileSpectraGraph(graph, overrides, definition, errors_)) {
        error_ = errors_.empty() ? "SpectraFX graph compilation failed" : errors_.front();
        return -1;
    }
    return defineEffect(definition);
}

fx::FxHandle BatchWorld::play(int effect, fx::float3 position,
                               fx::float3 direction, float scale,
                               fx::float4 tint) {
    return initialized_ ? particles_.play(effect, position, direction, scale, tint)
                        : fx::FxHandle{};
}
bool BatchWorld::setProgramParameter(fx::FxHandle h,uint32_t slot,float value,
                                     bool affectExisting) {
    return initialized_ && particles_.setProgramParameter(h,slot,value,affectExisting);
}
fx::FxHandle BatchWorld::playWithProgramParameters(
    int effect,fx::float3 position,fx::float3 direction,float scale,
    fx::float4 tint,const std::array<float,8>& params) {
    return initialized_
        ? particles_.playWithProgramParameters(effect,position,direction,
                                                scale,tint,params)
        : fx::FxHandle{};
}
void BatchWorld::move(fx::FxHandle h, fx::float3 pos) {
    if (initialized_) particles_.move(h, pos);
}
void BatchWorld::move(fx::FxHandle h, fx::float3 pos, fx::float3 dir) {
    if (initialized_) particles_.move(h, pos, dir);
}
void BatchWorld::stop(fx::FxHandle h) {
    if (initialized_) particles_.stop(h);
}
void BatchWorld::kill(fx::FxHandle h) {
    if (initialized_) particles_.kill(h);
}
bool BatchWorld::alive(fx::FxHandle h) const {
    return initialized_ && particles_.alive(h);
}
bool BatchWorld::playing(fx::FxHandle h) const {
    return initialized_ && particles_.playing(h);
}
void BatchWorld::setView(const fx::SpectraFxView& view) {
    if (initialized_) particles_.setView(view);
}
void BatchWorld::update(float dt) {
    if (initialized_) particles_.update(dt);
}
void BatchWorld::draw(const fx::FxFog& fog) {
    if (initialized_) particles_.render(fog);
}
void BatchWorld::collectLights(std::vector<fx::FxPointLight>& lights,
                                int maxLights) const {
    if (initialized_) particles_.collectLights(lights, maxLights);
}
void BatchWorld::clear() {
    if (initialized_) particles_.clear();
}

} // namespace aiverse::spectra
