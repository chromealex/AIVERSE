// Sectioned overlay animation, reusable across RTS/FPS/RPG/cinematics.
// Local animation tracks only: never authoritatively commits damage/movement.
#pragma once
#include "AnimationPlayback.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::lib::animation {
struct MontageSection {
    std::string name;
    int clip=-1;
    float begin=0, end=0; // source-clip seconds
    int next=-1; // -1: stop; >=0: section index (may loop)
    std::vector<AnimationNotifyKey> notifies; // source-clip seconds
};
struct MontageDefinition {
    const render::Model* model=nullptr; // host guarantees lifetime
    std::string slot;
    int maskRoot=-1; // -1 full body, otherwise subtree mask
    float blendIn=0.1f, blendOut=0.1f;
    std::vector<MontageSection> sections;
    std::vector<uint8_t> mask;

    static std::shared_ptr<const MontageDefinition> compile(MontageDefinition d) {
        if (!d.model || d.slot.empty() || d.slot.size()>128 ||
            d.sections.empty() || d.sections.size()>128 ||
            d.blendIn<0 || d.blendOut<0 ||
            !std::isfinite(d.blendIn) || !std::isfinite(d.blendOut))
            throw std::invalid_argument("montage: invalid definition");
        std::set<std::string> names;
        for (const auto& sec:d.sections) {
            if (sec.name.empty() || !names.insert(sec.name).second ||
                sec.clip<0 || size_t(sec.clip)>=d.model->clips.size() ||
                !std::isfinite(sec.begin) || !std::isfinite(sec.end) ||
                sec.begin<0 || sec.end<=sec.begin ||
                sec.end>d.model->clips[size_t(sec.clip)].duration ||
                sec.next < -1 || sec.next>=int(d.sections.size()))
                throw std::invalid_argument("montage: invalid section");
            for (const auto& key:sec.notifies)
                if (key.name.empty() || !std::isfinite(key.time) ||
                    key.time<sec.begin || key.time>sec.end)
                    throw std::invalid_argument("montage: invalid notify");
        }
        if (d.maskRoot>=int(d.model->nodes.size()))
            throw std::invalid_argument("montage: invalid bone mask");
        if (d.maskRoot>=0) d.mask=render::subtreeMask(*d.model,d.maskRoot);
        return std::make_shared<const MontageDefinition>(std::move(d));
    }
};
class MontagePlayer {
public:
    // Reusing the same slot should be enforced by the owning ECS adapter.
    bool play(std::shared_ptr<const MontageDefinition> definition,
              int section=0, float speed=1.f) {
        if (!definition || section<0 ||
            section>=int(definition->sections.size()) ||
            !(speed>0) || !std::isfinite(speed)) return false;
        definition_=std::move(definition);
        section_=section;
        time_=definition_->sections[size_t(section)].begin;
        speed_=speed;
        enter_=true;
        active_=true;
        events_.clear();
        error_.clear();
        return true;
    }
    void stop() {active_=false;events_.clear();}
    bool active() const {return active_;}
    int section() const {return active_?section_:-1;}
    float time() const {return time_;}
    const std::string& slot() const {
        static const std::string empty;
        return definition_?definition_->slot:empty;
    }
    const std::string& error() const {return error_;}
    const std::vector<AnimationNotifyEvent>& events() const {return events_;}
    void update(float dt) {
        events_.clear();
        if (!active_ || !std::isfinite(dt) || dt<=0) return;
        float remaining=dt*speed_;
        if (!std::isfinite(remaining)) { stop(); error_="invalid playback time"; return; }
        for (int transitions=0;active_ && remaining>0; ++transitions) {
            if (transitions>=64) {
                stop();error_="section transition budget exceeded";return;
            }
            const auto& sec=definition_->sections[size_t(section_)];
            const float step=std::min(remaining,std::max(0.f,sec.end-time_));
            const float from=enter_?std::nextafter(time_,-INFINITY):time_;
            const auto crossings=clipNotifies(sec.notifies,
                definition_->model->clips[size_t(sec.clip)].duration,
                from,double(time_+step),false,4096-events_.size());
            if (!crossings) {stop();error_="notify overflow";return;}
            for (const auto& ev:*crossings) events_.push_back(ev);
            enter_=false;
            time_+=step;
            remaining-=step;
            if (time_+1.e-6f<sec.end) break;
            if (sec.next<0) {active_=false; break;}
            section_=sec.next;
            time_=definition_->sections[size_t(section_)].begin;
            enter_=true;
        }
    }
    // Override blending of the played clip's channels into the graph pose.
    // Skipped if clip finished/asset incompatible. Must execute after graph
    // evaluation and BEFORE skeletal palette preparation.
    bool apply(const render::Model& model,render::Pose& pose) const {
        if (!active_ || !definition_ || definition_->model!=&model ||
            pose.size()!=model.nodes.size()) return false;
        const auto& sec=definition_->sections[size_t(section_)];
        float weight=1.f;
        if (definition_->blendIn>0)
            weight=std::min(weight,(time_-sec.begin)/definition_->blendIn);
        if (sec.next<0 && definition_->blendOut>0)
            weight=std::min(weight,(sec.end-time_)/definition_->blendOut);
        weight=std::clamp(weight,0.f,1.f);
        if (weight<=0) return false;
        const auto* mask=definition_->mask.empty()?nullptr:&definition_->mask;
        render::sampleClip(model.clips[size_t(sec.clip)],time_,pose,weight,mask);
        return true;
    }
private:
    std::shared_ptr<const MontageDefinition> definition_;
    int section_=-1;
    float time_=0,speed_=1;
    bool enter_=false,active_=false;
    std::vector<AnimationNotifyEvent> events_;
    std::string error_;
};
} // namespace aiverse::lib::animation
