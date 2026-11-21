// VIVID genre-independent skin weight authoring.
// Edits refer to model-global vertex IDs and *named* joints, never transient
// palette slots. Validate all owning primitive palettes before publication.
#pragma once
#include "engine/render/Model.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class SkinWeightAuthoring final {
public:
    struct Edit {
        std::uint32_t vertex=0;
        std::string joint;   // exact Model::Node name
        float weight=0.0f;   // requested share of 255 normalized bytes
        bool operator==(const Edit&) const = default;
    };
    struct Result {
        std::size_t verticesChanged=0;
        std::size_t primitivesValidated=0;
    };
    bool set(std::uint32_t vertex,std::string joint,float weight) {
        if(strokeStart_ || joint.empty()||joint.size()>128 ||
           joint.find_first_of("\n\r")!=std::string::npos ||
           !std::isfinite(weight)||weight<0||weight>1 ||
           (edits_.size()>=65536 && !edits_.count(vertex)))
            return false;
        auto next=edits_;
        next[vertex]={vertex,std::move(joint),weight};
        publish(std::move(next));
        return true;
    }
    bool erase(std::uint32_t vertex) {
        if(strokeStart_ || !edits_.count(vertex))return false;
        auto next=edits_;
        next.erase(vertex);
        publish(std::move(next));
        return true;
    }
    void clear() {if(!strokeStart_ && !edits_.empty())publish({});}
    bool canUndo() const noexcept {return !undo_.empty();}
    bool canRedo() const noexcept {return !redo_.empty();}
    bool undo() {
        if(strokeStart_ || undo_.empty())return false;
        if(redo_.size()==kHistory)redo_.erase(redo_.begin());
        redo_.push_back(std::move(edits_));
        edits_=std::move(undo_.back());
        undo_.pop_back();
        return true;
    }
    bool redo() {
        if(strokeStart_ || redo_.empty())return false;
        if(undo_.size()==kHistory)undo_.erase(undo_.begin());
        undo_.push_back(std::move(edits_));
        edits_=std::move(redo_.back());
        redo_.pop_back();
        return true;
    }
    const std::map<std::uint32_t,Edit>& edits() const noexcept {return edits_;}
    // Clone any authored edit when seam splitting duplicates a model-global
    // vertex. This is only a candidate-side derivative; source authoring IDs
    // and undo history retain their original imported vertex identities.
    template<class CloneRange>
    SkinWeightAuthoring withSplitClones(const CloneRange& clones) const {
        SkinWeightAuthoring next=*this;
        if(strokeStart_)throw std::invalid_argument(
            "VIVID skin weights: cannot split while a paint gesture is active");
        for(const auto& clone:clones) {
            const auto found=edits_.find(clone.source);
            if(found==edits_.end())continue;
            if(next.edits_.size()>=65536 || next.edits_.count(clone.target))
                throw std::invalid_argument(
                    "VIVID skin weights: cloned edit budget/identity conflict");
            auto copy=found->second;
            copy.vertex=clone.target;
            next.edits_.emplace(clone.target,std::move(copy));
        }
        next.undo_.clear();
        next.redo_.clear();
        return next;
    }
    // A drag gesture is one reversible history operation, regardless of
    // how many ray hits were sampled. In-progress edits are used only by
    // the editor and never applied to the uploaded Model until mouse-up.
    bool beginStroke() {
        if(strokeStart_)return false;
        strokeStart_=edits_;
        strokeChanged_=false;
        return true;
    }
    bool strokeActive() const noexcept{return strokeStart_.has_value();}
    bool endStroke(bool commit=true) {
        if(!strokeStart_)return false;
        const bool changed=strokeChanged_ && edits_!=*strokeStart_;
        if(!commit) {
            edits_=std::move(*strokeStart_);
        }else if(changed) {
            if(undo_.size()==kHistory)undo_.erase(undo_.begin());
            undo_.push_back(std::move(*strokeStart_));
            redo_.clear();
        }
        strokeStart_.reset();
        strokeChanged_=false;
        return commit&&changed;
    }
    bool strokeBrush(const render::Model& model,std::size_t meshIndex,
                     std::uint32_t center,const std::string& joint,
                     float target,float radius,float strength,bool mirrorX=false) {
        if(!strokeStart_)return false;
        const bool result=brush(model,meshIndex,center,joint,
                                target,radius,strength,mirrorX,false);
        if(result)strokeChanged_=true;
        return result;
    }

    // Read the currently presented weight for one named node. The caller
    // supplies a skinned mesh owning the global source vertex. This is also
    // used by the brush preview to show the exact quantized U8 share.
    static float weightAt(const render::Model& model,std::size_t meshIndex,
                          std::uint32_t vertex,const std::string& joint) {
        if(meshIndex>=model.meshes.size() || vertex>=model.vertices.size())
            throw std::invalid_argument("VIVID skin brush: missing mesh/vertex");
        int nodeId=-1;
        for(std::size_t i=0;i<model.nodes.size();++i)
            if(model.nodes[i].mesh==int(meshIndex) && model.nodes[i].skin>=0) {
                if(nodeId>=0)throw std::invalid_argument(
                    "VIVID skin brush: mesh has multiple skinned owners");
                nodeId=int(i);
            }
        if(nodeId<0)throw std::invalid_argument("VIVID skin brush: mesh is not skinned");
        const auto skinId=model.nodes[std::size_t(nodeId)].skin;
        if(skinId<0 || std::size_t(skinId)>=model.skins.size())
            throw std::invalid_argument("VIVID skin brush: invalid skin");
        const auto& skin=model.skins[std::size_t(skinId)];
        const int jointIndex=model.findNode(joint);
        if(jointIndex<0)throw std::invalid_argument("VIVID skin brush: unknown bone");
        bool referenced=false;
        std::vector<std::uint16_t> palette;
        for(const auto& primitive:model.meshes[meshIndex].primitives) {
            const std::size_t first=primitive.firstIndex;
            if(first>model.indices.size() ||
               primitive.indexCount>model.indices.size()-first)
                throw std::invalid_argument("VIVID skin brush: corrupt indices");
            bool contains=false;
            for(std::size_t i=first;i<first+primitive.indexCount;++i)
                if(model.indices[i]==vertex){contains=true;break;}
            if(!contains)continue;
            if(primitive.jointPalette.empty())
                throw std::invalid_argument("VIVID skin brush: rigid vertex");
            if(referenced && palette!=primitive.jointPalette)
                throw std::invalid_argument("VIVID skin brush: conflicting seam palettes");
            palette=primitive.jointPalette;
            referenced=true;
        }
        if(!referenced)throw std::invalid_argument("VIVID skin brush: vertex not on mesh");
        int destination=-1;
        for(std::size_t i=0;i<palette.size();++i) {
            if(palette[i]>=skin.joints.size())
                throw std::invalid_argument("VIVID skin brush: bad palette");
            if(skin.joints[palette[i]]==jointIndex)destination=int(i);
        }
        if(destination<0)
            throw std::invalid_argument("VIVID skin brush: selected bone absent in palette");
        unsigned sum=0;
        const auto& v=model.vertices[vertex];
        for(int i=0;i<4;++i) {
            if(v.weights[i] && v.joints[i]>=palette.size())
                throw std::invalid_argument("VIVID skin brush: invalid influence slot");
            if(v.weights[i] && v.joints[i]==destination)sum+=v.weights[i];
        }
        return float(sum)/255.0f;
    }

    // A stroke is one undo step. Local-space (mesh) radius, smooth cubic
    // falloff and optional bilateral X mirror. Uses explicit mesh vertex IDs
    // so disconnected surfaces and unrelated meshes cannot be painted.
    // One name/target per vertex; all failed operations preserve history.
    bool brush(const render::Model& model,std::size_t meshIndex,
               std::uint32_t center,const std::string& joint,
               float target,float radius,float strength,bool mirrorX=false,
               bool recordHistory=true) {
        if(meshIndex>=model.meshes.size() || center>=model.vertices.size() ||
           !std::isfinite(target)||target<0||target>1 ||
           !std::isfinite(radius)||radius<=0||radius>10 ||
           !std::isfinite(strength)||strength<=0||strength>1)return false;
        const auto& pivot=model.vertices[center].pos;
        const render::float3 reflected{-pivot.x,pivot.y,pivot.z};
        // Evaluate only vertices owned by this mesh, not the entire asset.
        std::vector<std::uint32_t> ids;
        for(const auto& primitive:model.meshes[meshIndex].primitives) {
            const std::size_t first=primitive.firstIndex;
            if(first>model.indices.size() ||
               primitive.indexCount>model.indices.size()-first)return false;
            for(std::size_t i=first;i<first+primitive.indexCount;++i) {
                if(model.indices[i]>=model.vertices.size())return false;
                ids.push_back(model.indices[i]);
            }
        }
        std::sort(ids.begin(),ids.end());
        ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        if(!std::binary_search(ids.begin(),ids.end(),center))return false;
        // Avoid unbounded per-stroke CPU work on huge imported meshes.
        // Only candidates inside brush influence need palette evaluation.
        std::vector<std::uint32_t> nearby;
        nearby.reserve(std::min<std::size_t>(ids.size(),4096));
        for(auto id:ids) {
            const auto p=model.vertices[id].pos;
            const auto a=render::length(p-pivot);
            const auto b=mirrorX?render::length(p-reflected):radius;
            if(a<radius || b<radius) {
                if(nearby.size()>=4096)return false;
                nearby.push_back(id);
            }
        }
        if(nearby.empty())return false;
        auto next=edits_;
        bool changed=false;
        for(auto id:nearby) {
            const auto& p=model.vertices[id].pos;
            auto falloff=[&](const render::float3& point) {
                const auto d=p-point;
                const float dist=render::length(d);
                if(!std::isfinite(dist)||dist>=radius)return 0.f;
                const float t=dist/radius;
                return (1.f-t)*(1.f-t)*(1.f+2.f*t);
            };
            const float direct=falloff(pivot);
            const float mirrored=mirrorX&&std::fabs(pivot.x)>1e-6f
                ?falloff(reflected):0.f;
            const float influence=std::min(1.f,strength*std::max(direct,mirrored));
            if(influence<=0)continue;
            try {
                const auto old=weightAt(model,meshIndex,id,joint);
                const float previous=next.count(id)&&next[id].joint==joint
                    ?next[id].weight:old;
                const float value=std::clamp(
                    previous+(target-previous)*influence,0.f,1.f);
                if(next.size()>=65536 && !next.count(id))return false;
                next[id]={id,joint,value};
                changed=true;
            }catch(const std::invalid_argument&) {
                return false; // entire stroke rejected, no partial edits
            }
        }
        if(!changed)return false;
        if(recordHistory) {
            if(strokeStart_)return false;
            publish(std::move(next));
        }else edits_=std::move(next);
        return true;
    }

    Json toJson() const {
        Json root=Json::object();
        root.set("version",Json::number(std::int64_t(1)));
        Json entries=Json::array();
        for(const auto& [id,edit]:edits_) {
            Json item=Json::object();
            item.set("vertex",Json::number(std::int64_t(id)));
            item.set("joint",Json::string(edit.joint));
            item.set("weight",Json::number(double(edit.weight),9));
            entries.push(std::move(item));
        }
        root.set("edits",std::move(entries));
        return root;
    }
    static SkinWeightAuthoring fromJson(const Json& root) {
        if(!root.isObject()||root["version"].integer(-1)!=1 ||
           !root["edits"].isArray()||root["edits"].size()>65536)
            throw std::invalid_argument("VIVID skin weights: invalid authoring schema");
        SkinWeightAuthoring authoring;
        for(const auto& e:root["edits"].items()) {
            if(!e.isObject()||!e["vertex"].isNumber()||
               !e["joint"].isString()||!e["weight"].isNumber())
                throw std::invalid_argument("VIVID skin weights: malformed edit");
            const auto id=e["vertex"].integer(-1);
            if(id<0||id>std::numeric_limits<std::uint32_t>::max() ||
               authoring.edits_.count(std::uint32_t(id)))
                throw std::invalid_argument("VIVID skin weights: invalid or repeated edit");
            const auto joint=e["joint"].str();
            const float weight=float(e["weight"].toDouble());
            if(joint.empty()||joint.size()>128 ||
               joint.find_first_of("\n\r")!=std::string::npos ||
               !std::isfinite(weight)||weight<0||weight>1)
                throw std::invalid_argument("VIVID skin weights: invalid joint/weight");
            // Import builds a single immutable snapshot, not 65k undo
            // records or quadratic copies of the entire edit map.
            authoring.edits_.emplace(std::uint32_t(id),
                Edit{std::uint32_t(id),joint,weight});
        }
        return authoring;
    }

    // Requires pre-upload private Model. Multi-node and multi-primitive
    // ownership is legal ONLY when all references to an edited vertex
    // resolve identical skin/joint palette slots. A shared seam with
    // conflicting palettes must first be split by a mesh authoring tool.
    // All writes are delayed until every edit is independently validated.
    Result apply(render::Model& model) const {
        if(model.vb.valid() || model.ib.valid() || model.geometryPoolPage ||
           model.geometry || model.vertices.empty())
            throw std::invalid_argument("VIVID skin weights: requires private pre-upload CPU mesh");
        struct Context {
            std::size_t skin=0;
            std::vector<std::uint16_t> palette;
        };
        std::map<std::uint32_t,Context> contexts;
        Result stats;
        for(const auto& node:model.nodes) {
            if(node.mesh<0 || node.skin<0)continue;
            if(std::size_t(node.mesh)>=model.meshes.size() ||
               std::size_t(node.skin)>=model.skins.size())
                throw std::invalid_argument("VIVID skin weights: invalid model mesh/skin reference");
            const auto skinId=std::size_t(node.skin);
            const auto& skin=model.skins[skinId];
            for(const auto& primitive:model.meshes[std::size_t(node.mesh)].primitives) {
                const auto start=std::size_t(primitive.firstIndex);
                if(start>model.indices.size() ||
                   primitive.indexCount>model.indices.size()-start)
                    throw std::invalid_argument("VIVID skin weights: invalid primitive range");
                if(primitive.jointPalette.empty())continue;
                ++stats.primitivesValidated;
                for(std::size_t i=start;i<start+primitive.indexCount;++i) {
                    const auto id=model.indices[i];
                    if(!edits_.count(id))continue;
                    if(id>=model.vertices.size())
                        throw std::invalid_argument("VIVID skin weights: vertex index out of bounds");
                    for(const auto j:primitive.jointPalette)
                        if(j>=skin.joints.size() ||
                           skin.joints[j]<0 ||
                           std::size_t(skin.joints[j])>=model.nodes.size())
                            throw std::invalid_argument("VIVID skin weights: bad palette joint");
                    auto [it,inserted]=contexts.try_emplace(id,Context{
                        skinId,primitive.jointPalette});
                    if(!inserted && (it->second.skin!=skinId ||
                                     it->second.palette!=primitive.jointPalette))
                        throw std::invalid_argument(
                            "VIVID skin weights: shared vertex has conflicting palettes; split seam first");
                }
            }
        }
        std::vector<std::pair<std::uint32_t,render::ModelVertex>> staged;
        staged.reserve(edits_.size());
        for(const auto& [vertex,edit]:edits_) {
            if(vertex>=model.vertices.size() || !contexts.count(vertex))
                throw std::invalid_argument("VIVID skin weights: edited vertex is not skinned");
            const auto& ctx=contexts.at(vertex);
            const auto& skin=model.skins[ctx.skin];
            const int jointNode=model.findNode(edit.joint);
            if(jointNode<0)
                throw std::invalid_argument("VIVID skin weights: unknown named joint");
            int destination=-1;
            for(std::size_t slot=0;slot<ctx.palette.size();++slot)
                if(skin.joints[ctx.palette[slot]]==jointNode) {
                    if(slot>255 || destination>=0)
                        throw std::invalid_argument("VIVID skin weights: ambiguous palette joint");
                    destination=int(slot);
                }
            if(destination<0)
                throw std::invalid_argument("VIVID skin weights: selected bone not in primitive palette");
            const auto& source=model.vertices[vertex];
            std::map<int,double> values;
            for(std::size_t i=0;i<4;++i) {
                if(!source.weights[i])continue;
                const int slot=source.joints[i];
                if(slot<0||std::size_t(slot)>=ctx.palette.size())
                    throw std::invalid_argument("VIVID skin weights: source influence outside palette");
                values[slot]+=source.weights[i]/255.0;
            }
            if(values.empty())
                throw std::invalid_argument("VIVID skin weights: source weights empty");
            const double requested=edit.weight;
            double others=0;
            for(const auto& [slot,value]:values)if(slot!=destination)others+=value;
            for(auto& [slot,value]:values)
                if(slot!=destination)value=(others>0)
                    ? value*(1.0-requested)/others : 0.0;
            if(others==0 && requested<1.0)
                throw std::invalid_argument(
                    "VIVID skin weights: no other joint to absorb remaining weight");
            values[destination]=requested;
            std::vector<std::pair<int,double>> sorted;
            for(const auto& [slot,value]:values)if(value>0)sorted.push_back({slot,value});
            std::sort(sorted.begin(),sorted.end(),
                [](const auto& a,const auto& b) {
                    return a.second!=b.second ? a.second>b.second : a.first<b.first;
                });
            if(sorted.size()>4) {
                // A newly authored bone must not vanish merely because
                // the original vertex already had four influences. Drop
                // the smallest OTHER influence, then re-normalize.
                const auto selected=std::find_if(sorted.begin(),sorted.end(),
                    [&](const auto& entry){return entry.first==destination;});
                if(selected!=sorted.end() &&
                   std::distance(sorted.begin(),selected)>=4)
                    std::iter_swap(selected,sorted.begin()+3);
                sorted.resize(4);
            }
            double total=0;
            for(const auto& pair:sorted)total+=pair.second;
            if(total<=0)throw std::invalid_argument("VIVID skin weights: no remaining influence");
            struct Quant {int slot;int bytes;double remainder;};
            std::vector<Quant> quant;
            int allocated=0;
            for(const auto& [slot,value]:sorted) {
                const double exact=value*255.0/total;
                const int floorValue=int(std::floor(exact));
                quant.push_back({slot,floorValue,exact-floorValue});
                allocated+=floorValue;
            }
            while(allocated<255) {
                auto best=std::max_element(quant.begin(),quant.end(),
                    [](const Quant& a,const Quant& b) {
                        return a.remainder!=b.remainder
                            ? a.remainder<b.remainder : a.slot>b.slot;
                    });
                ++best->bytes;best->remainder=-1; ++allocated;
            }
            render::ModelVertex next=source;
            for(int i=0;i<4;++i) {next.joints[i]=0;next.weights[i]=0;}
            for(std::size_t i=0;i<quant.size();++i) {
                next.joints[i]=std::uint8_t(quant[i].slot);
                next.weights[i]=std::uint8_t(quant[i].bytes);
            }
            staged.push_back({vertex,next});
        }
        // Zero-allocation publication after complete preflight.
        for(const auto& [vertex,updated]:staged)model.vertices[vertex]=updated;
        stats.verticesChanged=staged.size();
        return stats;
    }
private:
    void publish(std::map<std::uint32_t,Edit> next) {
        if(undo_.size()==kHistory)undo_.erase(undo_.begin());
        undo_.push_back(std::move(edits_));
        edits_=std::move(next);
        redo_.clear();
    }
    static constexpr std::size_t kHistory=32;
    std::map<std::uint32_t,Edit> edits_;
    std::vector<std::map<std::uint32_t,Edit>> undo_,redo_;
    std::optional<std::map<std::uint32_t,Edit>> strokeStart_;
    bool strokeChanged_=false;
};
} // namespace aiverse::vivid
