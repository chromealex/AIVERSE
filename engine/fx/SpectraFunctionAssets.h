// SpectraFX reusable function libraries for typed graph v2.
// A pure graph compiler remains responsible for function validation.
// Library I/O is injected by native hosts; no filesystem, RHI or genre code.
#pragma once
#include "VfxGraph.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::spectra {

inline Json functionValidationGraph(const Json& functions) {
    Json graph=Json::parse(R"JSON({
      "version":2,"parameters":{},"nodes":[
        {"id":"sp","type":"Spawn","properties":{"burst":1}},
        {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"size":0.2}},
        {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}],
      "links":[{"from":"sp","to":"init"},{"from":"init","to":"out"}],
      "outputs":["out"]
    })JSON");
    graph.set("functions",functions);
    return graph;
}
inline bool validateFunctionLibrary(const Json& library,
                                    std::vector<std::string>& errors) {
    errors.clear();
    if(!library.isObject() || library.getInt("version")!=1 ||
       !library["functions"].isObject()) {
        errors.push_back("SpectraFX function library requires version 1 and functions object");
        return false;
    }
    if(library["functions"].size()>32) {
        errors.push_back("SpectraFX function library has more than 32 functions");
        return false;
    }
    for(const auto& [key,value]:library.members()) {
        (void)value;
        if(key!="version"&&key!="functions"&&key!="description")
            errors.push_back("unknown function-library root key '"+key+"'");
    }
    if(!errors.empty())return false;
    fx::EffectDef temporary;
    return fx::compileSpectraGraph(functionValidationGraph(library["functions"]),
                                   temporary,errors);
}

inline bool mergeFunctionLibrary(Json& graph,const Json& library,
                                 std::vector<std::string>& errors,
                                 bool replaceExisting=false) {
    if(!graph.isObject() || graph.getInt("version")!=2) {
        errors={"function import requires a version-2 graph"};
        return false;
    }
    if(!validateFunctionLibrary(library,errors))return false;
    Json merged=graph["functions"];
    if(merged.isNull())merged=Json::object();
    if(!merged.isObject()) {
        errors={"graph.functions must be an object"};
        return false;
    }
    for(const auto& [name,fn]:library["functions"].members()) {
        const Json* existing=merged.has(name)?&merged[name]:nullptr;
        if(existing && *existing!=fn && !replaceExisting) {
            errors={"function '"+name+"' already exists with a different signature/body"};
            return false;
        }
        merged.set(name,fn);
    }
    if(merged.size()>32) {
        errors={"merged function library exceeds 32 definitions"};
        return false;
    }
    // Check cross-library references and cycles using the same authoritative
    // graph-v2 validator while leaving the authored graph's unfinished nodes
    // untouched. This supports importing into an incomplete editor draft.
    Json checkLibrary=Json::object();
    checkLibrary.set("version",Json::number(int64_t(1)));
    checkLibrary.set("functions",merged);
    if(!validateFunctionLibrary(checkLibrary,errors))return false;
    graph.set("functions",std::move(merged));
    return true;
}

struct FunctionLoadResult {
    Json library;
    std::vector<std::string> errors;
    bool loaded = false, reloaded = false, lastGoodFallback = false;
};

class FunctionLibraryAssets final {
public:
    using Reader=std::function<bool(const std::string& path,std::string& content)>;
    explicit FunctionLibraryAssets(Reader reader):reader_(std::move(reader)) {}
    FunctionLoadResult load(const std::string& name) {
        FunctionLoadResult result;
        if(!fx::vfxgraph_detail::nameOk(name)) {
            result.errors.push_back("unsafe SpectraFX function-library name");
            return result;
        }
        std::string content;
        const std::string path="fx/functions/"+name+".vfxfn.json";
        const auto old=cache_.find(name);
        const auto fallback=[&]() {
            if(old!=cache_.end()) {
                result.library=old->second.library;
                result.loaded=true;
                result.lastGoodFallback=true;
            }
        };
        if(!reader_ || !reader_(path,content) || content.size()>(1u<<20)) {
            result.errors.push_back("cannot load function library "+path);
            fallback();
            return result;
        }
        const uint64_t checksum=hash(content);
        if(old!=cache_.end() && checksum==old->second.checksum) {
            result.library=old->second.library;
            result.loaded=true;
            return result;
        }
        Json library;
        try {library=Json::parse(content);}
        catch(const std::exception& e) {
            result.errors.push_back(std::string("invalid function-library JSON: ")+e.what());
            fallback();
            return result;
        }
        if(!validateFunctionLibrary(library,result.errors)) {
            fallback();
            return result;
        }
        cache_[name]={checksum,library};
        result.library=std::move(library);
        result.loaded=true;
        result.reloaded=true;
        return result;
    }
    void invalidate(const std::string& name) {cache_.erase(name);}
    void invalidate() {cache_.clear();}
    size_t size() const {return cache_.size();}
private:
    static uint64_t hash(const std::string& content) {
        uint64_t h=14695981039346656037ull;
        for(unsigned char c:content){h^=c;h*=1099511628211ull;}
        return h;
    }
    struct Entry { uint64_t checksum=0; Json library; };
    Reader reader_;
    std::map<std::string,Entry> cache_;
};

} // namespace aiverse::spectra
