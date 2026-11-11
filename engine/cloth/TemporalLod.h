#pragma once
#include "Cloth.h"
#include <array>
namespace aiverse::cloth {
struct LodSettings {
    std::array<float,3> distances{20,50,100}; // metres from camera to rest-patch centre
    std::array<unsigned,4> periods{1,2,4,6}; // ticks at 60 Hz: 60 / 30 / 15 / 10 Hz
    float hysteresis=0.1f;
    bool stagger=true; // distribute independent groups over ticks
};
// Resolved host auto-LOD overrides distance (including orthographic cameras).
struct LodHint { int level=-1; bool visible=true; }; // -1 = distance fallback, otherwise 0..3
// Call exactly once per visual simulation tick (60 Hz), not once per arbitrary rendered frame.
// Indices must identify the same cloths across calls. reset() after reorder/replacement.
class TemporalLod {
public:
    explicit TemporalLod(LodSettings settings={});
    void step(std::span<Cloth>,Batch&,Vec camera,const Environment&,std::span<const Environment> perObject={},std::span<const LodHint> hints={});
    void setEnabled(bool enabled){enabled_=enabled;}
    void setMinimumLevel(unsigned level){minimumLevel_=level>3?3:level;}
    bool enabled()const{return enabled_;}
    void requestUpdate(size_t index); // flush elapsed time on the next tick (e.g. impact/pin change)
    void reset();
    const std::array<size_t,4>& counts()const{return counts_;}
    std::span<const float> timeSteps()const{return steps_;}
private:
    struct State {unsigned elapsed=0,level=0;bool initialized=false,force=false;};
    LodSettings settings_;
    bool enabled_=true;
    unsigned minimumLevel_=0;
    uint64_t tick_=0;
    std::vector<State> states_;
    std::vector<float> steps_;
    std::vector<unsigned> groupLevels_;
    std::vector<uint8_t> groupForced_;
    std::array<size_t,4> counts_{};
};
}
