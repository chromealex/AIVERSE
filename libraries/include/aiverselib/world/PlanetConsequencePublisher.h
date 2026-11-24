// Native projection of the optional durable consequence owner. Uses
// existing Box3D and PHYSIS force bridge. One selected body per host.
#pragma once
#include "aiverselib/world/PlanetConsequenceOwner.h"
#include "engine/physics/PhysisBox3D.h"
namespace aiverse::lib::worldprocess {
class PlanetConsequencePublisher {
public:
    using Key=lib::worldprocess::ConsequenceKey;
    using Record=lib::worldprocess::ConsequenceBody;
    using Checkpoint=lib::worldprocess::ConsequenceCheckpoint;
    size_t count()const{return entries_.size();}
    physics::BodyHandle handle(const Key& key)const{
        const auto it=entries_.find(key);return it==entries_.end()?physics::kNoBody:it->second;
    }
    bool publish(physics::Scene& scene,const World& world,const game::Surface& surface,std::string_view body){
        using namespace lib::worldprocess;
        if(scene_&&scene_!=&scene)return false;
        const auto* resource=world.resourceConst<PlanetConsequenceResource>();
        if(!resource)return entries_.empty();
        const int index=surface.findBody(std::string(body));
        if(index<0||resource->bodies.size()>kMaxConsequences)return false;
        const auto offset=game::Surface::anchor(index);
        std::map<Key,physics::BodyHandle> next;
        std::vector<const Record*> additions;
        for(const auto& record:resource->bodies){
            if(record.body!=body)continue;
            const auto frame=surfaceFrame(&surface,record.motion.position);
            if(!validConsequence(record)||!frame||frame->body!=index||next.contains(record.key))return false;
            const auto old=handle(record.key);
            if(old&&!scene.valid(old))return false;
            next.emplace(record.key,old);
            if(!old)additions.push_back(&record);
        }
        if(additions.size()>scene.maxBodies()-scene.bodyCount())return false;
        std::vector<physics::BodyHandle> created;created.reserve(additions.size());
        const auto discard=[&](){for(auto h:created)scene.destroy(h);};
        for(const auto* record:additions){
            physics::BodyDesc d;d.motion=physics::Motion::Dynamic;
            d.shape=record->shape==ConsequenceShape::Box?physics::ShapeKind::Box:
                record->shape==ConsequenceShape::Sphere?physics::ShapeKind::Sphere:physics::ShapeKind::Capsule;
            d.size=physics::physisB3(record->dimensions);
            d.radius=d.size.x;d.halfHeight=d.size.y;
            d.density=float(record->density.toDouble());d.friction=float(record->friction.toDouble());
            const auto& m=record->motion;const auto& q=m.rotation;
            d.position=physics::physisB3(m.position-offset);
            d.rotation={{float(q.x.toDouble()),float(q.y.toDouble()),float(q.z.toDouble())},float(q.w.toDouble())};
            d.linearVelocity=physics::physisB3(m.linear);d.angularVelocity=physics::physisB3(m.angular);
            const auto h=scene.create(d);
            if(h==physics::kNoBody){discard();return false;}
            created.push_back(h);
            if(!m.awake&&!scene.setAwake(h,false)){discard();return false;}
            next.at(record->key)=h;
        }
        for(const auto& [key,h]:entries_)if(!next.contains(key)&&scene.valid(h))scene.destroy(h);
        entries_=std::move(next);scene_=&scene;offset_=offset;return true;
    }
    bool applyGravity(physics::Scene& scene,const World& world)const{
        if(scene_&&scene_!=&scene)return false;
        for(const auto& [key,h]:entries_)if(!scene.valid(h)||scene.motion(h)!=physics::Motion::Dynamic)return false;
        for(const auto& [key,h]:entries_)if(!physics::applyPhysisToBody(scene,world,h,{},offset_))return false;
        return true;
    }
    bool capture(const physics::Scene& scene,const game::Surface& surface,std::string_view body,
            std::vector<Checkpoint>& output)const{
        if(scene_&&scene_!=&scene)return false;
        std::vector<Checkpoint> next;next.reserve(entries_.size());
        for(const auto& [key,h]:entries_){
            if(!scene.valid(h))return false;
            Checkpoint state;state.key=key;auto& m=state.motion;
            if(!physics::physisVec3(scene.position(h),m.position)||
               !physics::physisVec3(scene.linearVelocity(h),m.linear)||
               !physics::physisVec3(scene.angularVelocity(h),m.angular))return false;
            m.position=m.position+offset_;m.awake=scene.awake(h);
            const auto q=scene.rotation(h);
            if(!physics::physisFixed(q.v.x,m.rotation.x)||!physics::physisFixed(q.v.y,m.rotation.y)||
               !physics::physisFixed(q.v.z,m.rotation.z)||!physics::physisFixed(q.s,m.rotation.w))return false;
            const auto normalized=fxQuaternionNormalize(m.rotation);
            if(!normalized)return false;
            m.rotation=*normalized;
            const auto frame=lib::worldprocess::surfaceFrame(&surface,m.position);
            if(!frame||surface.bodies[size_t(frame->body)].id!=body)return false;
            next.push_back(state);
        }
        output=std::move(next);return true;
    }
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(const auto& [key,h]:entries_)if(scene.valid(h))scene.destroy(h);
        entries_.clear();scene_=nullptr;offset_={};return true;
    }
private:
    std::map<Key,physics::BodyHandle> entries_;
    physics::Scene* scene_=nullptr;
    Vec3 offset_{};
};
} // namespace aiverse::lib::worldprocess
