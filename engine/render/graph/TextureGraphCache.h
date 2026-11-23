// Explicit bounded process-local result cache. No project files, GPU handles or global state.
#pragma once
#include "TextureGraph.h"
#include <list>
#include <limits>
namespace aiverse::render::texture_graph {
class EvaluationCache {
    struct Entry {std::string key;std::vector<Image> images;size_t bytes=0;};
    std::list<Entry> entries_;
    size_t bytes_=0;uint64_t hits_=0,misses_=0,evictions_=0;
    std::string project_;
    static void increment(uint64_t& n){if(n!=std::numeric_limits<uint64_t>::max())++n;}
    static std::string key(const Program& p,const std::vector<size_t>& targets,int size){
        Json value=Json::object(),nodes=Json::array(),selected=Json::array();
        value.set("evaluator",Json::number(int64_t(1)));value.set("resolution",Json::number(int64_t(size)));
        for(const auto& n:p.nodes){
            Json row=Json::object(),inputs=Json::array();
            row.set("op",Json::string(n.info->id));
            // Sort property names; preserve exact number literals to avoid rounded aliases.
            std::map<std::string,Json> sorted;for(const auto& [k,v]:n.parameters.members())sorted.emplace(k,v);
            Json properties=Json::object();for(const auto& [k,v]:sorted)properties.set(k,v);
            row.set("properties",properties);
            for(auto i:n.inputs)inputs.push(Json::number(int64_t(i)));row.set("inputs",inputs);nodes.push(row);
        }
        for(auto t:targets)selected.push(Json::number(int64_t(t)));
        value.set("nodes",nodes);value.set("targets",selected);return value.dump(-1);
    }
public:
    static constexpr size_t maxBytes=32*1024*1024,maxEntries=16;
    void clear(){entries_.clear();bytes_=0;hits_=misses_=evictions_=0;}
    void selectProject(const std::string& p){if(p!=project_){clear();project_=p;}}
    std::vector<Image> evaluate(const Program& p,const std::vector<size_t>& targets,int resolution,bool& hit){
        const int size=resolution?resolution:p.size;
        // Validate even on hit; keys include actual resolved program, not a lossy hash.
        require(!targets.empty()&&targets.size()<=9&&size>=8&&size<=p.size,"invalid cache evaluation");
        for(auto t:targets)require(t<p.nodes.size(),"invalid cache target");
        auto k=key(p,targets,size);
        for(auto it=entries_.begin();it!=entries_.end();++it)if(it->key==k){
            auto result=it->images;entries_.splice(entries_.begin(),entries_,it);
            hit=true;increment(hits_);return result;
        }
        hit=false;increment(misses_);
        auto result=evaluateMany(p,targets,size);
        size_t bytes=k.size();for(const auto& image:result)bytes+=image.rgba.size()+image.name.size()+sizeof(Image);
        if(bytes<=maxBytes){
            while(!entries_.empty()&&(entries_.size()>=maxEntries||bytes_>maxBytes-bytes)){
                bytes_-=entries_.back().bytes;entries_.pop_back();increment(evictions_);
            }
            entries_.push_front({std::move(k),result,bytes});bytes_+=bytes;
        }
        return result;
    }
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("entries",Json::number(int64_t(entries_.size())));
        r.set("bytes",Json::number(int64_t(bytes_)));r.set("maxBytes",Json::number(int64_t(maxBytes)));
        r.set("hits",Json::string(std::to_string(hits_)));r.set("misses",Json::string(std::to_string(misses_)));
        r.set("evictions",Json::string(std::to_string(evictions_)));return r;
    }
};
} // namespace aiverse::render::texture_graph
