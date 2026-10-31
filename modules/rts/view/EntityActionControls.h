#pragma once
// Read-only world controls for explicit entity point abilities. Hosts own pointer
// capture/rendering; only a successful release produces a normal simulation command.
#include "modules/rts/RtsModule.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace rts::game {
class EntityActionControls {
public:
    struct Item {
        Entity entity;
        std::string abilityId, label;
        int ability = -1;
        render::float3 position{};
        bool enabled = false;
        int64_t cost = 0, remaining = 0, duration = 0;
        float explicitProgress = -1;
        float progress() const {
            if(explicitProgress>=0)return explicitProgress;
            return duration > 0 ? std::clamp(1.f-float(remaining)/float(duration),0.f,1.f) : 1.f;
        }
    };
    struct ScreenItem { size_t index; float x,y,w=120,h=48; };
    std::vector<ScreenItem> screen(const render::RTSCamera& camera,float width,float height) const {
        std::vector<ScreenItem> out;
        if(width<=0 || height<=0)return out;
        for(size_t i=0;i<items_.size();++i){
            render::float2 p;
            if(camera.worldToScreen(items_[i].position,width,height,p) && p.x>=0 && p.x<width && p.y>=0 && p.y<height)
                out.push_back({i,p.x-60,p.y-64});
        }
        return out;
    }
    // Disabled hits are consumed too: they must not become a movement order.
    bool press(const render::RTSCamera& camera,float width,float height,float x,float y) {
        auto boxes=screen(camera,width,height);
        for(auto it=boxes.rbegin();it!=boxes.rend();++it)if(x>=it->x && x<it->x+it->w && y>=it->y && y<it->y+it->h){
            const auto& item=items_[it->index];begin(item.entity,item.abilityId);return true;
        }
        return false;
    }
    const std::vector<Item>& items() const { return items_; }
    bool active() const { return source_.valid(); }
    const Item* source() const {
        for (const auto& item : items_)
            if (item.entity==source_ && item.abilityId==ability_) return &item;
        return nullptr;
    }
    void cancel() { source_={}; ability_.clear(); }
    void refresh(Simulation& sim, RtsModule& module, uint8_t player, const Json& definitions) {
        items_.clear();width_=module.state(sim).grid.w;height_=module.state(sim).grid.h;
        if (player_!=player) cancel();
        player_=player;
        const auto& rules=module.rules();const auto& vars=module.state(sim).trig.vars;
        sim.world().each<Unit,Owner,Transform>([&](Entity e,const Unit& u,const Owner& owner,const Transform& t){
            if (player==0 || owner.player!=player || u.hp<=Fixed{}) return;
            for (const auto& def : definitions.items()) {
                if (!(rules.types[u.type].tags & rules.tagMask(def.getStr("tag")))) continue;
                const auto id=def.getStr("ability");
                for (size_t i=0;i<rules.types[u.type].abilities.size();++i) {
                    const auto& a=rules.types[u.type].abilities[i];
                    if (a.id!=id || a.target!=TargetKind::Point) continue;
                    auto read=[&](const char* key) {
                        const auto prefix=def.getStr(key);
                        if (prefix.empty()) return int64_t(0);
                        auto v=vars.find(prefix+std::to_string(e.packed()));
                        return v==vars.end()?int64_t(0):v->second.toInt();
                    };
                    Item item;item.entity=e;item.abilityId=id;item.label=def.getStr("label");item.ability=int(i);
                    item.position={float(t.pos.x.toDouble()),float(t.pos.y.toDouble()),float(t.pos.z.toDouble())};
                    // Missing readiness never turns a stale/unknown source into a usable button.
                    item.enabled=read("readyPrefix")>0;
                    item.cost=std::max<int64_t>(0,read("costPrefix"));
                    item.remaining=std::max<int64_t>(0,read("remainingPrefix"));
                    item.duration=std::max<int64_t>(0,read("durationPrefix"));
                    const auto progressPrefix=def.getStr("progressPrefix");
                    if(!progressPrefix.empty()) {
                        const auto v=vars.find(progressPrefix+std::to_string(e.packed()));
                        if(v!=vars.end())item.explicitProgress=std::clamp(float(v->second.toDouble()),0.f,1.f);
                    }
                    items_.push_back(std::move(item));break;
                }
            }
        });
        std::stable_sort(items_.begin(),items_.end(),[](const Item& a,const Item& b){
            return a.entity.index!=b.entity.index?a.entity.index<b.entity.index:a.abilityId<b.abilityId;
        });
        const auto* current=source();
        if (active() && (!current || !current->enabled)) cancel();
    }
    bool begin(Entity entity,const std::string& ability) {
        cancel();
        for (const auto& item:items_) if(item.entity==entity && item.abilityId==ability && item.enabled) {
            source_=entity;ability_=ability;return true;
        }
        return false;
    }
    std::optional<Command> release(render::float3 point,bool overWorld,bool dragged) {
        const auto* item=source();std::optional<Command> result;
        if(item && item->enabled && overWorld && dragged && std::isfinite(point.x) && std::isfinite(point.z)
            && point.x>=0 && point.z>=0 && point.x<width_ && point.z<height_) {
            Command c;c.type=kCmdAbility;c.units={item->entity};c.param=item->ability;
            c.point={Fixed::fromRatio(int64_t(point.x*256),256),Fixed{},Fixed::fromRatio(int64_t(point.z*256),256)};
            result=std::move(c);
        }
        cancel();return result;
    }
private:
    std::vector<Item> items_;
    Entity source_;
    std::string ability_;
    int width_=0,height_=0;
    uint8_t player_=0;
};
} // namespace rts::game
