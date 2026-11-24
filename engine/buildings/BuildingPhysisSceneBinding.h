#pragma once
// Opt-in authoritative PHYSIS adapter. Reuses the game's actual Box3D Scene
// and exact SpatialWorld collider IDs. No second world/step or genre host.
// All calls run on the existing physics owner's structural barrier.
#include "engine/buildings/BuildingWorldRuntime.h"
#include "engine/physics/Scene.h"
#include "engine/data/Json.h"
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aiverse::building {
class BuildingPhysisSceneBinding {
public:
    // The project reserves this category for generated building bodies
    // and includes it in character/rigid body collision masks. The
    // obstruction query ignores only this instance's owned body handles,
    // not other buildings. Static environment can be excluded explicitly.
    explicit BuildingPhysisSceneBinding(physics::Scene& scene,uint64_t category=physics::kCategoryKinematic)
        :scene_(&scene),category_(category){}
    BuildingPhysisSceneBinding(const BuildingPhysisSceneBinding&)=delete;
    BuildingPhysisSceneBinding& operator=(const BuildingPhysisSceneBinding&)=delete;
    // Explicit retire() while the Scene is live; no unsafe destructor IO.
    using ResolveActor=std::function<physics::BodyHandle(Entity)>;

    bool synchronize(const std::map<uint32_t,world::Bounds>& shapes,
                     const std::set<uint32_t>& movable,std::string* error=nullptr){
        if(!scene_||!category_||(category_&(category_-1))||
           shapes.size()>65535)return fail(error,"Invalid PHYSIS building publication");
        for(uint32_t id:movable)if(!shapes.contains(id))return fail(error,"Unknown moving shape");
        for(const auto& [id,entry]:bodies_){
            (void)id;
            if(!scene_->valid(entry.handle))
                return fail(error,"PHYSIS building body lost ownership");
        }
        std::map<uint32_t,Entry> next;
        std::vector<physics::BodyHandle> created;
        struct Update{physics::BodyHandle handle;world::Bounds oldBounds,newBounds;};
        std::vector<Update> updates;
        auto rollback=[&](){
            for(auto h:created)scene_->destroy(h);
        };
        for(const auto& [id,bounds]:shapes){
            const auto extent=bounds.max-bounds.min;
            const auto center=(bounds.min+bounds.max)/2_fx;
            const bool moving=movable.contains(id);
            if(!id||extent.x<=Fixed{}||extent.y<=Fixed{}||extent.z<=Fixed{}){
                rollback();return fail(error,"Invalid PHYSIS building bounds");
            }
            const auto old=bodies_.find(id);
            if(old!=bodies_.end()&&old->second.movable==moving&&
               old->second.bounds.max-old->second.bounds.min==extent){
                auto entry=old->second;entry.bounds=bounds;
                next.emplace(id,entry);
                if(old->second.bounds.min!=bounds.min)
                    updates.push_back({entry.handle,old->second.bounds,bounds});
                continue;
            }
            physics::BodyDesc desc;
            desc.shape=physics::ShapeKind::Box;
            desc.motion=moving?physics::Motion::Kinematic:physics::Motion::Static;
            desc.size=asFloat(extent);desc.position=asFloat(center);
            // Static bodies must remain visible to the canonical character
            // query mask; the host can reserve extra categories separately.
            desc.category=moving?category_:physics::kCategoryStatic;
            desc.userData=id;
            const auto handle=scene_->create(desc);
            if(!handle){rollback();return fail(error,"PHYSIS building shape creation failed");}
            created.push_back(handle);next.emplace(id,Entry{handle,bounds,moving});
        }
        // All creations succeed before any existing shape is moved.
        size_t moved=0;
        for(const auto& update:updates){
            if(!scene_->setTransform(update.handle,
                 asFloat((update.newBounds.min+update.newBounds.max)/2_fx),b3Quat_identity)){
                for(size_t i=0;i<moved;++i)scene_->setTransform(updates[i].handle,
                   asFloat((updates[i].oldBounds.min+updates[i].oldBounds.max)/2_fx),b3Quat_identity);
                rollback();return fail(error,"PHYSIS shape update rejected");
            }
            // These are already committed fixed-tick poses, not future
            // velocity targets. Prevent Box3D carrying riders a second time.
            if(scene_->motion(update.handle)==physics::Motion::Kinematic)
                scene_->setLinearVelocity(update.handle,{0,0,0});
            ++moved;
        }
        size_t retired=0;
        for(const auto& [id,old]:bodies_){
            const auto it=next.find(id);
            if(it==next.end()||it->second.handle!=old.handle){scene_->destroy(old.handle);++retired;}
        }
        bodies_=std::move(next);
        lastCreated_=created.size();lastMoved_=moved;lastRetired_=retired;
        if(publications_!=UINT64_MAX)++publications_;
        if(error)error->clear();return true;
    }
    bool retire(std::string* error=nullptr){return synchronize({}, {},error);}
    size_t bodyCount()const noexcept{return bodies_.size();}
    Json telemetry()const{
        Json j=Json::object();
        j.set("successfulPublications",Json::string(std::to_string(publications_)));
        j.set("bodyCount",Json::number(int64_t(bodies_.size())));
        j.set("lastCreated",Json::number(int64_t(lastCreated_)));
        j.set("lastMoved",Json::number(int64_t(lastMoved_)));
        j.set("lastRetired",Json::number(int64_t(lastRetired_)));return j;
    }
    physics::BodyHandle body(uint32_t colliderId)const{
        const auto it=bodies_.find(colliderId);
        return it==bodies_.end()?physics::kNoBody:it->second.handle;
    }
    // Query the existing physical actor/cargo bodies. Never count the
    // building's own static/kinematic shapes as its obstruction.
    bool occupied(const world::Bounds& first,const world::Bounds& second,
                  physics::QueryFilter filter={})const{
        const auto check=[&](const world::Bounds& bounds){
            const auto hits=scene_->overlapBox(asFloat((bounds.min+bounds.max)/2_fx),
                   asFloat((bounds.max-bounds.min)/2_fx),b3Quat_identity,filter);
            for(auto h:hits){
                bool owned=false;
                for(const auto& [id,entry]:bodies_){(void)id;if(entry.handle==h){owned=true;break;}}
                if(!owned)return true;
            }
            return false;
        };
        return check(first)||check(second);
    }
    // Map an existing ECS entity to its existing body/proxy, and publish
    // the absolute canonical ECS feet/origin, never an additional delta.
    // Offset is body origin minus Transform.pos. Host must not also
    // apply character motor ground carry while a lift passenger is bound.
    bool synchronizeRider(Entity actor,Vec3 position,const ResolveActor& resolve,
                          Vec3 offset={},std::string* error=nullptr){
        if(!resolve)return fail(error,"Missing PHYSIS rider resolver");
        const auto handle=resolve(actor);
        if(!scene_->valid(handle))return fail(error,"PHYSIS rider body unavailable");
        if(!scene_->setTransform(handle,asFloat(position+offset),scene_->rotation(handle)))
            return fail(error,"PHYSIS rider transform rejected");
        if(scene_->motion(handle)==physics::Motion::Kinematic)
            scene_->setLinearVelocity(handle,{0,0,0});
        return true;
    }
    // Binding lifetime belongs to the native host, and must cover all
    // runtime callbacks including unload and restoration.
    void configure(BuildingWorldRuntime::Definition& def,
                   physics::QueryFilter filter,ResolveActor resolve={},Vec3 riderOffset={}){
        def.physicalPublication=[this](const auto& shapes,const auto& movable,std::string* error){
            return synchronize(shapes,movable,error);
        };
        def.extraObstruction=[this,filter](const auto& first,const auto& second,uint32_t,uint32_t){
            return occupied(first,second,filter);
        };
        if(resolve)def.riderPublication=[this,resolve=std::move(resolve),riderOffset](
                    const World&,Entity actor,Vec3 position,std::string* error){
            return synchronizeRider(actor,position,resolve,riderOffset,error);
        };
    }
private:
    uint64_t publications_=0;
    size_t lastCreated_=0,lastMoved_=0,lastRetired_=0;
    struct Entry{physics::BodyHandle handle=physics::kNoBody;world::Bounds bounds{};bool movable=false;};
    static b3Vec3 asFloat(Vec3 v){
        return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};
    }
    static bool fail(std::string* error,const std::string& why){if(error)*error=why;return false;}
    physics::Scene* scene_;
    uint64_t category_;
    std::map<uint32_t,Entry> bodies_;
};
} // namespace aiverse::building
