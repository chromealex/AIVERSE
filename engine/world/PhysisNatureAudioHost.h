// PHYSIS nature -> audio::AudioEngine (looping positional fire, wind bed).
// Main-thread adapter for NatureAudioPlanner. The bank must contain
// physis_fire_loop / physis_wind_loop: call addPhysisNatureSounds(bankJson)
// before AudioEngine::loadBank (project recordings with those names win).
#pragma once
#include "engine/world/PhysisNatureCues.h"
#include "engine/audio/Audio.h"
#include <map>

namespace aiverse::world {

class PhysisNatureAudioHost {
public:
    explicit PhysisNatureAudioHost(NatureAudioPolicy policy = {}) : planner_(policy) {}

    void update(audio::AudioEngine& engine, const NaturePresentationFrame& frame,
                float listenerX, float listenerY, float listenerZ, float dt) {
        apply(engine, planner_.plan(frame, listenerX, listenerY, listenerZ, dt));
    }
    void reset(audio::AudioEngine& engine) { apply(engine, planner_.clear()); voices_.clear(); }
    std::size_t voices() const { return voices_.size(); }

private:
    void apply(audio::AudioEngine& engine, const std::vector<NatureAudioCommand>& commands) {
        for (const auto& c : commands) {
            const auto id = std::make_pair(std::uint8_t(c.sound), c.key);
            switch (c.op) {
            case NatureAudioOp::Start: {
                audio::PlayParams p;
                p.volume = c.volume; p.pitch = c.pitch; p.loop = 1;
                p.positional = c.positional; p.x = c.x; p.y = c.y; p.z = c.z;
                const auto v = engine.play(natureSoundName(c.sound), p);
                if (v != audio::kNoVoice) voices_[id] = v;
                break;
            }
            case NatureAudioOp::Update: {
                const auto it = voices_.find(id);
                if (it == voices_.end()) break;
                audio::VoiceParams vp;
                vp.volume = c.volume; vp.pitch = c.pitch;
                engine.setVoice(it->second, vp);
                if (c.positional) engine.setVoicePosition(it->second, c.x, c.y, c.z);
                break;
            }
            case NatureAudioOp::Stop:
                if (auto it = voices_.find(id); it != voices_.end()) {
                    engine.stop(it->second, 0.6f);
                    voices_.erase(it);
                }
                break;
            }
        }
    }
    NatureAudioPlanner planner_;
    std::map<std::pair<std::uint8_t, std::uint64_t>, audio::VoiceId> voices_;
};

} // namespace aiverse::world
