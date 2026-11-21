// MotionWeave — reusable host-facing facial preview/authoring session.
// Native ModelView and future HTML5 Studio native bridges use this same
// genre-neutral compiled rig, one parent pose and actor-local smoothing state.
#pragma once
#include <aiverselib/animation/FacialControlRig.h>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>

namespace aiverse::lib::animation {

class FacialPreviewSession final {
public:
    using Scalars=std::map<std::string,float>;
    using Vectors=std::map<std::string,render::float3>;

    // Atomic replacement: invalid/missing imported channels and IK targets
    // preserve the last-good compiled rig and per-instance smoothing state.
    bool configure(const render::Model& model,const Json& document,
                   const Scalars& scalars,const Vectors& vectors,
                   std::string& error) {
        error.clear();
        try {
            render::AnimationParameters declared;
            const Json& morphs=document["morphs"];
            if(morphs.isArray())
                for(const auto& morph:morphs.items())
                    if(morph["parameter"].isString())
                        declared.values.try_emplace(morph["parameter"].str(),0.f);
            const Json& ik=document["rig"];
            if(ik.isArray())
                for(const auto& unit:ik.items()) {
                    const std::string weight=unit.getStr("weightParameter");
                    if(!weight.empty())declared.values.try_emplace(weight,1.f);
                    for(const char* key:{
                        "target","pole","translationParameter",
                        "rotationParameter","scaleParameter"}) {
                        const std::string value=unit.getStr(key);
                        if(!value.empty())declared.vectors.try_emplace(
                            value,render::float3{});
                    }
                }
            // Every live control name must be declared by this document;
            // never silently add unknown user input or guess a target.
            for(const auto& [key,value]:scalars) {
                auto found=declared.values.find(key);
                if(found==declared.values.end())
                    throw std::invalid_argument("unknown facial scalar: "+key);
                found->second=value;
            }
            for(const auto& [key,value]:vectors) {
                auto found=declared.vectors.find(key);
                if(found==declared.vectors.end())
                    throw std::invalid_argument("unknown facial vector: "+key);
                found->second=value;
            }
            auto compiled=std::make_shared<const FacialControlRig>(
                FacialControlRig::compile(
                    model,document,declared));
            // Publish only after the entire rig/model/parameter transaction
            // validated. No second animation clock, Model owner or GPU upload.
            rig_=std::move(compiled);
            parameters_=std::move(declared);
            state_.reset();
            return true;
        }catch(const std::exception& ex) {
            error=ex.what();
            return false;
        }
    }
    bool ready() const noexcept {return bool(rig_);}
    bool apply(const render::Model& model,render::Pose& evaluatedPose,
               float dt,std::string& error) {
        if(!rig_) {
            error="facial preview rig is not compiled";
            return false;
        }
        return rig_->apply(model,parameters_,evaluatedPose,state_,dt,1.f,&error);
    }
    bool scalar(const std::string& key,float value) {
        if(!std::isfinite(value) || value<0.f || value>1.f)return false;
        const auto found=parameters_.values.find(key);
        if(found==parameters_.values.end())return false;
        found->second=value;
        return true;
    }
    bool vector(const std::string& key,render::float3 value) {
        if(!std::isfinite(value.x) || !std::isfinite(value.y) ||
           !std::isfinite(value.z))return false;
        const auto found=parameters_.vectors.find(key);
        if(found==parameters_.vectors.end())return false;
        found->second=value;
        return true;
    }
    const render::AnimationParameters& controls() const noexcept {
        return parameters_;
    }
    void clear() noexcept {
        rig_.reset();
        parameters_={};
        state_.reset();
    }
private:
    std::shared_ptr<const FacialControlRig> rig_;
    render::AnimationParameters parameters_;
    FacialControlState state_;
};

} // namespace aiverse::lib::animation
