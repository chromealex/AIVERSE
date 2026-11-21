// NEREID -> SpectraFX: optional, genre-independent presentation adapter.
// SpectraFX owns particle simulation; NEREID owns only effect event mapping.
// The caller supplies graph names already defined/loaded in the particle system.
#pragma once
#include "engine/fx/SpectraFX.h"
#include <aiverselib/water/NereidInteractionPresentation.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace aiverse::world {

struct NereidSpectraFxCue {
    std::string graph; // empty: ignore this event, no hard-coded effect assets
    float scale=1.f;
};

class NereidSpectraFxBridge {
public:
    explicit NereidSpectraFxBridge(spectra::Runtime& system):particles_(system) {}
    void setCue(lib::nereid::WaterInteractionKind kind,NereidSpectraFxCue cue) {
        const auto index=size_t(kind);
        if(index>=cues_.size())return;
        cue.scale=std::isfinite(cue.scale)?
            std::clamp(cue.scale,0.f,16.f):0.f;
        cues_[index]=std::move(cue);
    }
    // Host calls from WorldScene::ingestNereidInteractions callback.
    // Duplicated ticks are suppressed by WorldScene's ripple field.
    void emit(const lib::nereid::WaterPresentationEvent& value) {
        const auto& e=value.event;
        const auto index=size_t(e.kind);
        if(index>=cues_.size() || cues_[index].graph.empty() ||
           cues_[index].scale<=0.f)return;
        const auto& cue=cues_[index];
        const float scale=std::clamp(float(e.strength.toDouble())*cue.scale,
                                     0.f,16.f);
        if(scale<=0.f)return;
        auto handle=particles_.play(cue.graph,{
            float(e.position.x.toDouble()),float(e.position.y.toDouble()),
            float(e.position.z.toDouble())},{0.f,1.f,0.f},scale);
        if(handle.valid()) {
            if(handles_.size()>=kMaxTracked) {
                // First forget cues that have finished emitting (their
                // particles fade out on their own), so a full list does not
                // cut off a cue that is still playing.
                handles_.erase(std::remove_if(handles_.begin(),handles_.end(),
                    [&](spectra::Handle h){return !particles_.playing(h);}),
                    handles_.end());
            }
            if(handles_.size()>=kMaxTracked) {
                // This bridge owns only its own one-shot handles. Stopping an
                // oldest cue caps rollback tracking, not global SpectraFX.
                particles_.stop(handles_.front());
                handles_.erase(handles_.begin());
            }
            handles_.push_back(handle);
        }
    }
    // Use on scene rollback/replacement to stop visual ghosts.
    void reset() {
        for(auto h:handles_)particles_.kill(h);
        handles_.clear();
    }
    size_t tracked()const{return handles_.size();}
private:
    static constexpr size_t kMaxTracked=256;
    spectra::Runtime& particles_;
    std::array<NereidSpectraFxCue,5> cues_{};
    std::vector<spectra::Handle> handles_;
};

} // namespace aiverse::world
