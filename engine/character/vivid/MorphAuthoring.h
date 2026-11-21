// VIVID editor-side sparse facial sculpt data. Never mutates the source mesh.
#pragma once
#include "SparseMorph.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class MorphAuthoring {
public:
    static MorphAuthoring fromCompiled(const SparseMorphLibrary& compiled) {
        MorphAuthoring out;
        out.vertices_=compiled.vertices();
        for(std::size_t i=0;i<compiled.targets();++i) {
            MorphTargetInput target;
            target.name=std::string(compiled.targetName(i));
            const auto range=compiled.gpuRanges()[i];
            for(std::uint32_t k=0;k<range.count;++k) {
                const auto& gpu=compiled.gpuDeltas()[range.first+k];
                MorphDelta delta;
                delta.vertex=gpu.vertex;
                for(int n=0;n<3;++n) {
                    delta.position[std::size_t(n)]=gpu.position[n];
                    delta.normal[std::size_t(n)]=gpu.normal[n];
                    delta.tangent[std::size_t(n)]=gpu.tangent[n];
                }
                target.deltas.push_back(delta);
            }
            out.targets_.push_back(std::move(target));
        }
        return out;
    }
    std::size_t vertices() const noexcept {return vertices_;}
    std::size_t targets() const noexcept {return targets_.size();}
    const std::vector<MorphTargetInput>& shapes() const noexcept {return targets_;}
    bool canUndo() const noexcept {return !undo_.empty();}
    bool canRedo() const noexcept {return !redo_.empty();}
    // Edits sparse offsets by source model vertex ID. A zero triplet removes
    // an existing sparse entry. Failed edits are fully transactional.
    bool addTarget(const std::string& name) {
        if(name.empty()||name.size()>128 || targets_.size()>=4096)return false;
        for(const auto& target:targets_)if(target.name==name)return false;
        auto next=targets_;
        next.push_back({name,{}});
        try { (void)SparseMorphLibrary::compile(vertices_,next); }
        catch(...) {return false;}
        commit(std::move(next));
        return true;
    }
    bool removeTarget(std::size_t index) {
        if(index>=targets_.size())return false;
        auto next=targets_;
        next.erase(next.begin()+std::ptrdiff_t(index));
        try { (void)SparseMorphLibrary::compile(vertices_,next); }
        catch(...) {return false;}
        commit(std::move(next));
        return true;
    }
    bool setPosition(std::size_t target,std::uint32_t vertex,MorphVec3 value) {
        if(target>=targets_.size() || vertex>=vertices_)return false;
        for(float c:value)if(!std::isfinite(c)||std::fabs(c)>10.0f)return false;
        auto next=targets_;
        auto& deltas=next[target].deltas;
        auto it=std::find_if(deltas.begin(),deltas.end(),
            [&](const MorphDelta& d){return d.vertex==vertex;});
        if(it==deltas.end()) {
            if(value==MorphVec3{})return true;
            MorphDelta delta;
            delta.vertex=vertex;
            delta.position=value;
            deltas.push_back(delta);
        }else {
            it->position=value;
            if(it->position==MorphVec3{} && it->normal==MorphVec3{} &&
               it->tangent==MorphVec3{})deltas.erase(it);
        }
        try { (void)SparseMorphLibrary::compile(vertices_,next); }
        catch(...) {return false;}
        commit(std::move(next));
        return true;
    }
    // One undoable brush stroke in source mesh coordinates.
    // Radius and offset are intentionally bounded for imported scans.
    // One undoable stroke with optional X-axis bilateral symmetry.
    // Mirroring is in the source mesh's local coordinates (X=0 plane).
    bool brush(std::size_t target,std::span<const MorphVec3> source,
               std::uint32_t center,float radius,MorphVec3 offset,
               bool mirrorX=false) {
        if(target>=targets_.size() || source.empty() ||
           source.size()>vertices_ || center>=source.size() ||
           !std::isfinite(radius) || radius<=0 || radius>10.0f)
            return false;
        for(float v:offset)if(!std::isfinite(v)||std::fabs(v)>10.0f)return false;
        for(const auto& p:source)
            for(float v:p)if(!std::isfinite(v))return false;
        auto next=targets_;
        auto& deltas=next[target].deltas;
        const auto& pivot=source[center];
        const MorphVec3 reflected{-pivot[0],pivot[1],pivot[2]};
        const MorphVec3 reflectedOffset{-offset[0],offset[1],offset[2]};
        const float radiusSquared=radius*radius;
        std::size_t changed=0;
        for(std::size_t i=0;i<source.size();++i) {
            const auto& position=source[i];
            auto falloffAt=[&](const MorphVec3& point) {
                float distanceSquared=0;
                for(int axis=0;axis<3;++axis) {
                    const float d=position[std::size_t(axis)]-point[std::size_t(axis)];
                    distanceSquared+=d*d;
                }
                if(distanceSquared>=radiusSquared)return 0.0f;
                const float t=std::sqrt(distanceSquared)/radius;
                return (1.0f-t)*(1.0f-t)*(1.0f+2.0f*t);
            };
            const float direct=falloffAt(pivot);
            // Suppress a double stroke when the sculpt center is on the
            // symmetry plane. Otherwise blend mirrored contributions.
            const float mirrored=(mirrorX&&std::fabs(pivot[0])>1e-6f)
                ? falloffAt(reflected) : 0.0f;
            if(direct<=0.0f && mirrored<=0.0f)continue;
            auto it=std::find_if(deltas.begin(),deltas.end(),
                [&](const MorphDelta& d){return d.vertex==i;});
            if(it==deltas.end()) {
                MorphDelta delta;
                delta.vertex=std::uint32_t(i);
                deltas.push_back(delta);
                it=std::prev(deltas.end());
            }
            for(int axis=0;axis<3;++axis) {
                const auto ax=std::size_t(axis);
                const float delta=offset[ax]*direct+
                                  reflectedOffset[ax]*mirrored;
                // The symmetry plane must stay on X=0 even when the brush
                // centre itself lies exactly on the plane.
                if(mirrorX && axis==0 && std::fabs(position[0])<1e-6f)
                    continue;
                it->position[ax]+=delta;
            }
            ++changed;
        }
        if(!changed)return true;
        try { (void)SparseMorphLibrary::compile(vertices_,next); }
        catch(...) {return false;}
        commit(std::move(next));
        return true;
    }
    bool undo() {
        if(undo_.empty())return false;
        if(redo_.size()==kMaxHistory)redo_.erase(redo_.begin());
        redo_.push_back(std::move(targets_));
        targets_=std::move(undo_.back());
        undo_.pop_back();
        return true;
    }
    bool redo() {
        if(redo_.empty())return false;
        if(undo_.size()==kMaxHistory)undo_.erase(undo_.begin());
        undo_.push_back(std::move(targets_));
        targets_=std::move(redo_.back());
        redo_.pop_back();
        return true;
    }
    SparseMorphLibrary compile() const {
        return SparseMorphLibrary::compile(vertices_,targets_);
    }
    Json toJson(std::size_t originalVertexCount) const {
        (void)compile();
        if(originalVertexCount==0 || originalVertexCount>vertices_)
            throw std::invalid_argument("VIVID morph export: invalid source vertex count");
        for(const auto& t:targets_)for(const auto& delta:t.deltas)
            if(delta.vertex>=originalVertexCount)
                throw std::invalid_argument("VIVID morph export: generated LOD vertex cannot be authored");
        Json root=Json::object();
        root.set("version",Json::number(std::int64_t(1)));
        root.set("vertices",Json::number(std::int64_t(originalVertexCount)));
        Json targets=Json::array();
        for(const auto& t:targets_) {
            Json target=Json::object();
            target.set("name",Json::string(t.name));
            Json deltas=Json::array();
            for(const auto& d:t.deltas) {
                Json delta=Json::object();
                delta.set("vertex",Json::number(std::int64_t(d.vertex)));
                const auto write=[](const MorphVec3& value) {
                    Json array=Json::array();
                    for(float x:value)array.push(Json::number(double(x),7));
                    return array;
                };
                delta.set("position",write(d.position));
                delta.set("normal",write(d.normal));
                delta.set("tangent",write(d.tangent));
                deltas.push(std::move(delta));
            }
            target.set("deltas",std::move(deltas));
            targets.push(std::move(target));
        }
        root.set("targets",std::move(targets));
        return root;
    }
private:
    void commit(std::vector<MorphTargetInput> next) {
        if(undo_.size()==kMaxHistory)undo_.erase(undo_.begin());
        undo_.push_back(std::move(targets_));
        targets_=std::move(next);
        redo_.clear();
    }
    static constexpr std::size_t kMaxHistory=32;
    std::size_t vertices_=0;
    std::vector<MorphTargetInput> targets_;
    std::vector<std::vector<MorphTargetInput>> undo_,redo_;
};
} // namespace aiverse::vivid
