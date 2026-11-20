// LumaWeave per-world presentation owner. AIVERSE ECS may own many simulation
// Worlds and scenes; each world must have an INDEPENDENT light registry,
// GPU source/geometry revisions, temporal history, FX source IDs and material
// view tokens. Reusing a single driver across different worlds would allow
// temporal history / moving-light IDs to alias on equal local source keys.
// This pool does NOT mutate ECS Worlds or decide world lifecycle for a game.
#pragma once
#include "LumaWeaveNativeDriver.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

namespace aiverse::render {

class LumaWeaveWorlds {
public:
    static constexpr size_t kMaxWorlds=8;

    struct WorldStatus {
        uint64_t worldId=0;
        bool gpuInitialized=false;
        bool gpuPending=false;
    };

    LumaWeaveWorlds(gfx::Device& device,Renderer3D& renderer,PostProcess& existing)
        :device_(device),renderer_(renderer),existing_(existing) {}
    LumaWeaveWorlds(const LumaWeaveWorlds&)=delete;
    LumaWeaveWorlds& operator=(const LumaWeaveWorlds&)=delete;

    // One worldId identifies a PRESENTATION world incarnation. Never reuse
    // the same ID for a newly created ECS World while old callbacks are live.
    bool addWorld(uint64_t worldId) {
        if(!worldId || worlds_.contains(worldId) ||
           worlds_.size()>=kMaxWorlds)return false;
        auto owned=std::make_unique<Entry>(device_,renderer_,existing_);
        owned->gpuInitialized=owned->driver.initialize();
        // Failed Metal init is safe: the driver can still use the old mobile
        // LightSystem/post path. Do not force an unsupported GPU backend.
        worlds_.emplace(worldId,std::move(owned));
        return true;
    }

    size_t count() const{return worlds_.size();}
    bool contains(uint64_t worldId) const{return worlds_.contains(worldId);}

    WorldStatus status(uint64_t worldId) const {
        auto it=worlds_.find(worldId);
        if(it==worlds_.end())throw std::out_of_range("LumaWeave unknown world ID");
        return {worldId,it->second->gpuInitialized,
                it->second->driver.gpuPost().screen().busy()};
    }

    LumaWeaveNativeDriver& driver(uint64_t worldId) {
        auto it=worlds_.find(worldId);
        if(it==worlds_.end())throw std::out_of_range("LumaWeave unknown world ID");
        return it->second->driver;
    }

    template<class SubmitScene>
    LumaNativeViewResult frame(uint64_t worldId,const LumaNativeViewInput& input,
                               const gfx::PassDesc& out,SubmitScene&& draw) {
        // Prevent re-entrant frames on the same Renderer3D, even for different
        // worlds, because Renderer3D owns active passes, materials and shadows.
        if(inFrame_)throw std::logic_error("LumaWeave nested world frames");
        struct Guard {
            bool& inFrame;
            explicit Guard(bool& value):inFrame(value){inFrame=true;}
            ~Guard(){inFrame=false;}
        } guard(inFrame_);
        return driver(worldId).frame(input,out,std::forward<SubmitScene>(draw));
    }

    // Call once after Device::beginFrame() on the render owner thread,
    // including worlds whose cameras are currently inactive. Metal delivers
    // buffer-read callbacks at beginFrame; without this idle worlds can remain
    // falsely pending forever and never safely retire their GPU resources.
    struct CompletedWorld {
        uint64_t worldId=0;
        LumaWeaveScreenMetal::Completion completion{};
    };
    std::vector<CompletedWorld> pollAll() {
        if(inFrame_)throw std::logic_error("cannot poll during LumaWeave draw");
        std::vector<CompletedWorld> completed;
        for(auto& [id,entry]:worlds_) {
            const auto done=entry->driver.gpuPost().pollGpu();
            if(done.completed)completed.push_back({id,done});
        }
        return completed;
    }

    // Deletion is refused until an actual GPU command-buffer callback is
    // polled. The caller must keep Device/Renderer/PostProcess alive until then.
    bool removeWorld(uint64_t worldId) {
        if(inFrame_)return false;
        auto it=worlds_.find(worldId);
        if(it==worlds_.end())return false;
        // Completion may already be delivered for an inactive world's prior
        // frame. Observe its real RHI callback before the retirement guard.
        (void)it->second->driver.gpuPost().pollGpu();
        if(it->second->driver.gpuPost().screen().busy())return false;
        if(!it->second->driver.shutdown())return false;
        worlds_.erase(it);
        return true;
    }

    // Never partially tear down the pool: reject if any world is pending.
    bool shutdown() {
        if(inFrame_)return false;
        for(auto& [id,world]:worlds_) {
            (void)id;
            (void)world->driver.gpuPost().pollGpu();
            if(world->driver.gpuPost().screen().busy())return false;
        }
        for(auto& [id,world]:worlds_) {
            (void)id;
            if(!world->driver.shutdown())return false;
        }
        worlds_.clear();
        return true;
    }

private:
    struct Entry {
        LumaWeaveNativeDriver driver;
        bool gpuInitialized=false;
        Entry(gfx::Device& d,Renderer3D& r,PostProcess& p):driver(d,r,p){}
    };
    gfx::Device& device_;
    Renderer3D& renderer_;
    PostProcess& existing_;
    std::map<uint64_t,std::unique_ptr<Entry>> worlds_;
    bool inFrame_=false;
};

} // namespace aiverse::render
