// AIVERSE MotionWeave — genre-neutral compiled facial Control Rig.
// Runs AFTER the sole host-owned Graph/Animator pose evaluation and BEFORE
// worldMatrices/Renderer3D. No new animation clock or gameplay authority.
#pragma once
#include "engine/render/AnimationRig.h"
#include "engine/view/ModelPoseRetarget.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::animation {

// One state PER character. Neither the compiled program nor this state
// owns the Model/GPU cache or an authoritative Simulation/World.
struct FacialControlState {
    std::vector<float> smoothed;
    render::AnimationRigState ik;
    bool initialized=false;
    void reset() {
        smoothed.clear();
        ik=render::AnimationRigState{};
        initialized=false;
    }
};

class FacialControlRig final {
public:
    enum class Blend : uint8_t { Override, Additive };
    struct Binding {
        std::string parameter;
        std::string nodeName;
        std::string morphName;
        int node=-1;
        int channel=-1;
        float gain=1.f;
        float bias=0.f;
        float halfLife=0.f; // 0 = immediate; seconds for 50% exponential decay
        Blend blend=Blend::Override;
    };

    // v1 document:
    // {"version":1,"morphs":[{"parameter":"jawOpen",
    // "node":"Face","target":"jawOpen","gain":1,
    // "bias":0,"halfLife":0.07,"blend":"override"}],
    // "rig":[{"bone":"Jaw","op":"transform",
    //         "rotationParameter":"jawEuler"}]}
    // The optional "rig" is the EXISTING AnimationRig program (Aim,
    // TwoBoneIK, FullBodyIK, AngularLimit, etc), not another solver.
    // Only exact importer-authored mesh-local morph names are accepted.
    static FacialControlRig compile(
        const render::Model& model,const Json& authored,
        const render::AnimationParameters& defaults) {
        if(!authored.isObject() || !authored["version"].isNumber() ||
           authored["version"].numberLiteral()!="1")
            throw std::invalid_argument("facial Control Rig expects version 1");
        if(!authored["morphs"].isArray() && !authored["morphs"].isNull())
            throw std::invalid_argument("facial morphs must be an array");
        if(!authored["rig"].isNull() && !authored["rig"].isArray())
            throw std::invalid_argument("facial rig must be an array");
        if(authored["morphs"].isArray() && authored["morphs"].size()>256)
            throw std::invalid_argument("facial morph binding limit exceeded");
        FacialControlRig out;
        out.model_=&model;
        out.names_=view::importedModelMorphChannelNames(model);
        out.nodeNames_.reserve(model.nodes.size());
        out.nodeParents_.reserve(model.nodes.size());
        out.nodeMeshes_.reserve(model.nodes.size());
        std::map<std::string,int> uniqueNodes;
        for(std::size_t i=0;i<model.nodes.size();++i) {
            const auto& node=model.nodes[i];
            out.nodeNames_.push_back(node.name);
            out.nodeParents_.push_back(node.parent);
            out.nodeMeshes_.push_back(node.mesh);
            out.bindTranslations_.push_back(node.t);
            out.bindRotations_.push_back(node.r);
            out.bindScales_.push_back(node.s);
            if(!node.name.empty()) {
                const auto [it,inserted]=uniqueNodes.emplace(node.name,int(i));
                // Duplicate names are allowed in a general Model, but
                // explicit facial selectors must never resolve ambiguously.
                if(!inserted)it->second=-1;
            }
        }
        // Validate morph-name metadata against actual GPU channel counts
        // before using it to index NodePose::w.
        std::vector<int> channelCount(model.nodes.size(),0);
        for(std::size_t i=0;i<model.nodes.size();++i) {
            const int mesh=model.nodes[i].mesh;
            if(mesh<0)continue;
            if(std::size_t(mesh)>=model.meshes.size())
                throw std::invalid_argument("facial node references invalid mesh");
            for(const auto& prim:model.meshes[std::size_t(mesh)].primitives) {
                if(prim.morphTargets<0 ||
                   prim.morphTargets>render::kMaxMorphTargets)
                    throw std::invalid_argument("facial mesh morph count invalid");
                channelCount[i]=std::max(channelCount[i],prim.morphTargets);
            }
        }
        out.channelCounts_=channelCount;
        std::set<std::pair<int,int>> used;
        if(authored["morphs"].isArray())
            for(const auto& field:authored["morphs"].items()) {
                if(!field.isObject() || !field["parameter"].isString() ||
                   !field["node"].isString() || !field["target"].isString())
                    throw std::invalid_argument("facial morph needs parameter/node/target");
                Binding b;
                b.parameter=field["parameter"].str();
                b.nodeName=field["node"].str();
                b.morphName=field["target"].str();
                if(b.parameter.empty() || b.nodeName.empty() ||
                   b.morphName.empty() ||
                   !defaults.values.contains(b.parameter) ||
                   !std::isfinite(defaults.values.at(b.parameter)))
                    throw std::invalid_argument("facial morph parameter missing or undeclared");
                const auto nodeIt=uniqueNodes.find(b.nodeName);
                if(nodeIt==uniqueNodes.end() || nodeIt->second<0)
                    throw std::invalid_argument("missing or ambiguous facial node "+b.nodeName);
                b.node=nodeIt->second;
                const auto node=std::size_t(b.node);
                if(out.names_[node].size()!=std::size_t(channelCount[node]) ||
                   out.names_[node].empty())
                    throw std::invalid_argument("incomplete named facial morph channel metadata");
                std::set<std::string> uniqueChannels;
                for(std::size_t i=0;i<out.names_[node].size();++i) {
                    const auto& name=out.names_[node][i];
                    if(name.empty() || !uniqueChannels.insert(name).second)
                        throw std::invalid_argument("duplicate/empty facial morph name");
                    if(name==b.morphName)b.channel=int(i);
                }
                if(b.channel<0 || !used.emplace(b.node,b.channel).second)
                    throw std::invalid_argument("missing/duplicated targeted facial morph channel");
                auto number=[&](const char* key,float fallback) {
                    if(!field.has(key))return fallback;
                    if(!field[key].isNumber())
                        throw std::invalid_argument("facial morph number must be numeric");
                    const double n=field[key].toDouble();
                    if(!std::isfinite(n) || std::abs(n)>1000.)
                        throw std::invalid_argument("facial morph number out of range");
                    return float(n);
                };
                b.gain=number("gain",1.f);
                b.bias=number("bias",0.f);
                b.halfLife=number("halfLife",0.f);
                if(b.gain<0.f || b.gain>4.f ||
                   b.bias< -1.f || b.bias>1.f ||
                   b.halfLife<0.f || b.halfLife>10.f)
                    throw std::invalid_argument("facial morph controls out of range");
                if(field.has("blend") && !field["blend"].isString())
                    throw std::invalid_argument("facial blend mode must be text");
                const std::string mode=field.getStr("blend","override");
                if(mode=="override")b.blend=Blend::Override;
                else if(mode=="additive")b.blend=Blend::Additive;
                else throw std::invalid_argument("unknown facial morph blend mode");
                out.bindings_.push_back(std::move(b));
            }
        // Execute all facial bone/eye/jaw IK using the existing battle-
        // tested AnimationRig, including its explicit parameter validation.
        if(authored["rig"].isArray() && authored["rig"].size()) {
            out.ik_=render::AnimationRig::compile(model,authored["rig"],defaults);
            for(const auto& unit:out.ik_->units()) {
                for(const auto& name:{
                    unit.target,unit.pole,unit.translation,
                    unit.rotation,unit.scale})
                    if(!name.empty())out.requiredVectors_.insert(name);
                if(!unit.weightParameter.empty())
                    out.requiredFloats_.insert(unit.weightParameter);
            }
        }
        if(out.bindings_.empty() && !out.ik_)
            throw std::invalid_argument("facial Control Rig has no work");
        return out;
    }

    bool matches(const render::Model& model) const {
        if(&model!=model_ || model.nodes.size()!=nodeNames_.size())
            return false;
        // Reject source reimports that reuse the same Model pointer and
        // reorder bones/mesh assignments or rename/reorder morph channels.
        static const std::vector<std::string> noMorphs;
        for(std::size_t i=0;i<model.nodes.size();++i) {
            const auto& bone=model.nodes[i];
            if(bone.name!=nodeNames_[i] ||
               bone.parent!=nodeParents_[i] ||
               bone.mesh!=nodeMeshes_[i] ||
               bone.t.x!=bindTranslations_[i].x ||
               bone.t.y!=bindTranslations_[i].y ||
               bone.t.z!=bindTranslations_[i].z ||
               bone.r.x!=bindRotations_[i].x ||
               bone.r.y!=bindRotations_[i].y ||
               bone.r.z!=bindRotations_[i].z ||
               bone.r.w!=bindRotations_[i].w ||
               bone.s.x!=bindScales_[i].x ||
               bone.s.y!=bindScales_[i].y ||
               bone.s.z!=bindScales_[i].z)
                return false;
            int count=0;
            if(bone.mesh>=0) {
                if(std::size_t(bone.mesh)>=model.meshes.size())
                    return false;
                for(const auto& prim:
                    model.meshes[std::size_t(bone.mesh)].primitives)
                    count=std::max(count,prim.morphTargets);
            }
            if(count!=channelCounts_[i])return false;
            const auto& morphNames=bone.mesh<0
                ?noMorphs:model.meshes[std::size_t(bone.mesh)].morphTargetNames;
            if(morphNames!=names_[i])return false;
        }
        return true;
    }

    // Transactional: a rejected frame leaves last-good pose and smoothing
    // state untouched. Caller supplies its independently evaluated pose.
    bool apply(const render::Model& model,
               const render::AnimationParameters& parameters,
               render::Pose& pose,FacialControlState& state,
               float dt,float globalWeight=1.f,
               std::string* error=nullptr) const {
        if(error)error->clear();
        try {
            if(!matches(model) || pose.size()!=model.nodes.size())
                throw std::invalid_argument("stale facial rig/model or incompatible pose");
            if(!std::isfinite(dt) || dt<0.f ||
               !std::isfinite(globalWeight) ||
               globalWeight<0.f || globalWeight>1.f)
                throw std::invalid_argument("invalid facial dt or weight");
            if(state.initialized && state.smoothed.size()!=bindings_.size())
                throw std::invalid_argument("stale facial instance state");
            for(const auto& [name,value]:parameters.values) {
                (void)name;
                if(!std::isfinite(value))
                    throw std::invalid_argument("nonfinite facial/IK float parameter");
            }
            for(const auto& [name,value]:parameters.vectors) {
                (void)name;
                if(!std::isfinite(value.x) || !std::isfinite(value.y) ||
                   !std::isfinite(value.z))
                    throw std::invalid_argument("nonfinite facial/IK vector parameter");
            }
            for(const auto& name:requiredVectors_)
                if(!parameters.vectors.contains(name))
                    throw std::invalid_argument("missing facial IK vector: "+name);
            for(const auto& name:requiredFloats_)
                if(!parameters.values.contains(name))
                    throw std::invalid_argument("missing facial IK float: "+name);
            render::Pose candidate=pose;
            FacialControlState next=state;
            if(!next.initialized) {
                next.smoothed.resize(bindings_.size(),0.f);
                next.initialized=true;
            }
            for(std::size_t i=0;i<bindings_.size();++i) {
                const auto& b=bindings_[i];
                const auto input=parameters.values.find(b.parameter);
                if(input==parameters.values.end() ||
                   !std::isfinite(input->second))
                    throw std::invalid_argument("facial parameter missing or nonfinite: "+b.parameter);
                const float target=std::clamp(
                    input->second*b.gain+b.bias,0.f,1.f);
                const float old=next.smoothed[i];
                // Initial frame snaps to authored value; subsequent frames
                // are half-life smoothed even when dt is a paused 0.
                const float alpha=!state.initialized || b.halfLife==0.f
                    ?1.f :1.f-std::exp2(-dt/b.halfLife);
                const float value=old+(target-old)*alpha;
                next.smoothed[i]=value;
                auto& channel=candidate[std::size_t(b.node)].w[b.channel];
                if(!std::isfinite(channel))
                    throw std::invalid_argument("nonfinite incoming facial morph pose");
                const float desired=b.blend==Blend::Override
                    ? value : std::clamp(channel+value,0.f,1.f);
                channel=channel+(desired-channel)*globalWeight;
            }
            if(ik_ && globalWeight>0.f) {
                // No second Animator/Graph evaluation: only the existing
                // post-pose AnimationRig solve, reusing its Aim/IK/limits.
                std::vector<render::mat4> scratch;
                next.ik.pendingTime=dt;
                ik_->execute(model,parameters,candidate,scratch,&next.ik);
                if(globalWeight<1.f) {
                    for(std::size_t i=0;i<candidate.size();++i) {
                        candidate[i].t=render::lerp(pose[i].t,candidate[i].t,globalWeight);
                        candidate[i].r=render::nlerp(pose[i].r,candidate[i].r,globalWeight);
                        candidate[i].s=render::lerp(pose[i].s,candidate[i].s,globalWeight);
                    }
                }
            }
            pose=std::move(candidate);
            state=std::move(next);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }catch(...) {
            if(error)*error="unknown facial Control Rig evaluation failure";
            return false;
        }
    }

    const std::vector<Binding>& bindings() const noexcept{return bindings_;}
    bool hasIk() const noexcept{return bool(ik_);}

private:
    const render::Model* model_=nullptr; // borrowed until host releases GPU owner
    view::ModelMorphChannelNames names_;
    std::vector<std::string> nodeNames_;
    std::vector<int> nodeParents_,nodeMeshes_,channelCounts_;
    std::vector<render::float3> bindTranslations_,bindScales_;
    std::vector<render::quat> bindRotations_;
    std::vector<Binding> bindings_;
    std::set<std::string> requiredVectors_,requiredFloats_;
    std::shared_ptr<const render::AnimationRig> ik_;
};

} // namespace aiverse::lib::animation
