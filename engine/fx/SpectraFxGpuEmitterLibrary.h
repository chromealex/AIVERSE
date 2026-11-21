// SpectraFX project-owned hybrid emitter asset library.
// Content convention: fx/gpu/<safe-name>.gpu.json.
// Loading is transactional and retains the last known-good asset when a
// hot-reload fails. The library owns configuration only, never GPU resources.
// A cached asset is served without file IO; it is re-read (and re-hashed)
// only after invalidate()/invalidateAll(), e.g. when the editor saves it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "SpectraFxGpuEmitterAsset.h"

namespace aiverse::fx {

class SpectraFxGpuEmitterLibrary {
public:
    using FileReader = std::function<bool(const std::string&,std::string&)>;
    struct LoadResult {
        bool valid=false;
        bool reloaded=false;
        bool fallback=false;
        uint64_t revision=0;
        std::string error;
    };
    struct Entry {
        SpectraFxHybridEmitter::Config config;
        uint64_t revision=1;
        uint64_t contentHash=0;
        bool stale=false; // set by invalidate: next load re-reads the file
    };

    explicit SpectraFxGpuEmitterLibrary(FileReader reader)
        : reader_(std::move(reader)) {}

    static bool validName(const std::string& name) {
        if(name.empty() || name.size()>64) return false;
        for(unsigned char c:name) {
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-')) return false;
        }
        return true;
    }
    static std::string pathFor(const std::string& name) {
        return validName(name)?"fx/gpu/"+name+".gpu.json":std::string{};
    }

    LoadResult load(const std::string& name) {
        LoadResult result;
        if(!validName(name)) {
            result.error="SpectraFX GPU asset name invalid";
            return result;
        }
        const auto old=cache_.find(name);
        // Hot path (every Play cue): no file read or hash for a cached asset.
        if(old!=cache_.end() && !old->second.stale) {
            result.valid=true;
            result.revision=old->second.revision;
            return result;
        }
        auto fallback=[&](std::string error) {
            result.error=std::move(error);
            if(old!=cache_.end()) {
                // Keep serving the last known-good asset without retrying
                // the failing read on every cue; a new invalidate retries.
                old->second.stale=false;
                result.valid=true;
                result.fallback=true;
                result.revision=old->second.revision;
            }
            return result;
        };
        std::string text;
        if(!reader_ || !reader_(pathFor(name),text))
            return fallback("Unable to read SpectraFX GPU emitter asset "+name);
        // Hash of authored bytes; checking on load supports project hot reload
        // without filesystem dependencies or engine-global mutable watchers.
        uint64_t hash=14695981039346656037ull;
        for(unsigned char c:text) {hash^=c;hash*=1099511628211ull;}
        if(old!=cache_.end() && old->second.contentHash==hash) {
            old->second.stale=false; // unchanged bytes keep their revision
            result.valid=true;
            result.revision=old->second.revision;
            return result;
        }
        SpectraFxHybridEmitter::Config next;
        std::string error;
        if(!SpectraFxGpuEmitterAsset::readText(text,next,&error))
            return fallback(error);
        const uint64_t revision=old==cache_.end()?1:old->second.revision+1;
        cache_[name]={std::move(next),revision,hash};
        result.valid=result.reloaded=true;
        result.revision=revision;
        return result;
    }

    const Entry* find(const std::string& name) const {
        const auto it=cache_.find(name);
        return it==cache_.end()?nullptr:&it->second;
    }
    void invalidate(const std::string& name) {
        const auto it=cache_.find(name);
        if(it!=cache_.end()) it->second.stale=true;
    }
    void invalidateAll() {
        for(auto& pair:cache_) pair.second.stale=true;
    }
    void clear() {cache_.clear();}
    size_t size() const {return cache_.size();}

private:
    FileReader reader_;
    std::unordered_map<std::string,Entry> cache_;
};

} // namespace aiverse::fx
