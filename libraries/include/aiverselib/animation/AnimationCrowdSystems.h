// Lightweight GPU baked-crowd player: no CPU poses or bone palettes per entity.
// Renderer3D already batches compatible ModelDraw::baked instances.
#pragma once
#include "engine/core/ECS.h"
#include "engine/render/Animation.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
namespace aiverse::lib::animation {
// Opt-in only for simple clips without per-bone graph, overlays or contacts.
// The immutable model must have a GPU baked joint-matrix texture.
struct AnimationCrowdComponent {
    const render::Model* model=nullptr;
    render::Animator animator;
    int idle=-1,walk=-1,run=-1,death=-1;
    int playing=-2; // -2 means not initialized
    float speed=0.f,walkSpeed=1.2f,runSpeed=3.2f;
    float fade=.18f;
    float initialNormalizedPhase=-1.f; // opt-in backend phase handoff
    bool dead=false;
};
struct AnimationCrowdSampleComponent {
    render::BakedSample sample;
    bool valid=false;
};
inline void stepAnimationCrowd(World& world,float dt) {
    if(!std::isfinite(dt) || dt<0) return;
    auto* crowds=world.prepareExistingWrite<AnimationCrowdComponent>();
    auto* samples=world.prepareExistingWrite<AnimationCrowdSampleComponent>();
    if(!crowds || !samples)return;
    for(size_t i=0;i<crowds->size();++i) {
        const Entity entity=crowds->entityAt(i);
        if(!world.alive(entity))continue;
        auto* output=samples->tryGet(entity.index);
        if(!output)continue;
        auto& c=crowds->dataAt(i);
        output->valid=false;
        if(!c.model || !c.model->baked.tex.valid())continue;
        c.animator.setModel(c.model);
        int target=c.idle;
        if(c.dead && c.death>=0)target=c.death;
        else if(c.speed>.05f) target=c.walk>=0?c.walk:c.run;
        if(target<0 || size_t(target)>=c.model->clips.size())continue;
        const bool once=c.dead && target==c.death;
        if(target!=c.playing) {
            render::Animator::Play play;play.fade=std::clamp(c.fade,0.f,5.f);
            play.loop=!once;
            c.animator.play(0,target,play);
            if(std::isfinite(c.initialNormalizedPhase) &&
               c.initialNormalizedPhase>=0.f) {
                c.animator.seek(0,std::clamp(c.initialNormalizedPhase,0.f,.999999f)*
                    c.model->clips[size_t(target)].duration);
                c.initialNormalizedPhase=-1.f;
            }
            c.playing=target;
        }
        if(!once && c.walk>=0 && c.run>=0 && c.playing==c.walk) {
            const float denom=std::max(.01f,c.runSpeed-c.walkSpeed);
            const float alpha=std::clamp((c.speed-c.walkSpeed)/denom,0.f,1.f);
            c.animator.setBlend(0,c.run,alpha);
            c.animator.setSpeed(0,c.speed>.05f?
                std::clamp(c.speed/std::max(.01f,
                    c.walkSpeed+(c.runSpeed-c.walkSpeed)*alpha),.2f,4.f):1.f);
        } else c.animator.setBlend(0,-1,0.f);
        c.animator.update(std::min(dt,.25f));
        const auto raw=c.animator.baseSample();
        output->sample={raw.clipA,raw.clipB,raw.timeA,raw.timeB,raw.w,
                        raw.loopA,raw.loopB};
        output->valid=true;
    }
}
} // namespace aiverse::lib::animation
