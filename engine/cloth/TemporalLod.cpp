#include "TemporalLod.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::cloth {
TemporalLod::TemporalLod(LodSettings s):settings_(s){
    float previous=0;
    for(float d:s.distances){if(!std::isfinite(d)||d<=previous)throw std::invalid_argument("cloth LOD distances must be positive and increasing");previous=d;}
    unsigned period=0;
    for(auto p:s.periods){if(p<1||p>6||p<period)throw std::invalid_argument("cloth LOD periods must increase within 1..6 ticks");period=p;}
    if(s.periods[0]!=1||!std::isfinite(s.hysteresis)||s.hysteresis<0||s.hysteresis>=0.5f)throw std::invalid_argument("cloth LOD hysteresis/near period");
}
void TemporalLod::requestUpdate(size_t i){if(i<states_.size())states_[i].force=true;}
void TemporalLod::reset(){states_.clear();steps_.clear();counts_={};tick_=0;}
void TemporalLod::step(std::span<Cloth> cloths,Batch& batch,Vec camera,const Environment& env,std::span<const Environment> inputs,std::span<const LodHint> hints){
    if(!std::isfinite(camera.x)||!std::isfinite(camera.y)||!std::isfinite(camera.z)||(!inputs.empty()&&inputs.size()!=cloths.size()))return;
    if(!hints.empty()&&hints.size()!=cloths.size())return;
    for(auto hint:hints)if(hint.level < -1||hint.level>3)return;
    ++tick_;
    states_.resize(cloths.size());steps_.resize(cloths.size());groupLevels_.assign(cloths.size(),3);groupForced_.assign(cloths.size(),0);counts_={};
    for(size_t i=0;i<cloths.size();++i){auto& state=states_[i];auto& cloth=cloths[i];steps_[i]=0;
        if(!cloth.active||(enabled_&&!hints.empty()&&!hints[i].visible)){state.elapsed=0;state.force=false;continue;}
        auto& s=cloth.grid().settings;float distance=render::length(cloth.origin()+Vec{s.width/2,-s.height/2,0}-camera);
        if(!enabled_)state.level=0;
        else if(!hints.empty()&&hints[i].level>=0)state.level=unsigned(hints[i].level);
        else if(!state.initialized){state.level=0;while(state.level<3&&distance>settings_.distances[state.level])++state.level;}
        else {
            while(state.level<3&&distance>settings_.distances[state.level]*(1+settings_.hysteresis))++state.level;
            while(state.level>0&&distance<settings_.distances[state.level-1]*(1-settings_.hysteresis))--state.level;
        }
        state.initialized=true;++state.elapsed;
        steps_[i]=std::min(0.1f,float(state.elapsed)/60.0f);
    }
    if(!enabled_){
        for(size_t i=0;i<cloths.size();++i)if(cloths[i].active){states_[i].elapsed=0;states_[i].force=false;++counts_[0];}
        batch.stepScheduled(cloths,steps_,env,inputs);return;
    }
    // Plan by exact state, current inputs and elapsed time before selecting the group's rate.
    // The nearest member wins; distant clones must not split an otherwise reusable simulation.
    batch.run(cloths,0,env,inputs,steps_,false);
    auto sources=batch.representatives();
    for(size_t i=0;i<cloths.size();++i){if(steps_[i]==0)continue;auto source=sources[i];
        groupLevels_[source]=std::min(groupLevels_[source],std::max(minimumLevel_,states_[i].level));
        groupForced_[source]|=uint8_t(states_[i].force);
    }
    for(size_t i=0;i<cloths.size();++i){if(steps_[i]==0)continue;auto& state=states_[i];auto source=sources[i];unsigned level=groupLevels_[source];++counts_[level];
        if(groupForced_[source]||state.elapsed>=settings_.periods[level]||(settings_.stagger&&tick_%settings_.periods[level]==(source/size_t(batch.workers()))%settings_.periods[level])){state.elapsed=0;state.force=false;}
        else steps_[i]=0;
    }
    batch.stepScheduled(cloths,steps_,env,inputs);
}
}
