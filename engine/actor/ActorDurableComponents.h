#pragma once
#include "ActorModule.h"
#include "engine/vehicles/DurableVehicleComponents.h"
#include "engine/world/UnifiedEntitySection.h"
#include <set>

namespace aiverse::actor {
// Portable ECS projection of materialized region actors. The normal native
// save/network serializers remain unchanged. This is not a full match save:
// projectiles, match resources and host config require separate policies.
class ActorDurableComponents {
    using Codecs=world::DurableComponentCodecs;
    using Context=Codecs::Context;
    template<class T,class Fn> static bool fieldCodec(Codecs& codecs,const char* id,Fn fn){
        return codecs.addContextual<T>(id,1,[fn](Archive& a,T& value,const World&,const Context&){
            fn(a,value);return a.ok();
        });
    }
public:
    static bool registerCodecs(Codecs& c){
        return fieldCodec<CharacterId>(c,"aiverse.actor.character.id",[](Archive& a,CharacterId& v){
            a(v.slot,v.team,v.life,v.kills);if(!v.slot||!v.life)a.fail();
        })&&fieldCodec<CharacterBody>(c,"aiverse.actor.character.body",[](Archive& a,CharacterBody& v){
            a(v.pos,v.velocity,v.yaw,v.pitch,v.grounded,v.crouched,v.prone,v.climbing,v.frozen,v.vehicle);
        })&&fieldCodec<CharacterHealth>(c,"aiverse.actor.character.health",[](Archive& a,CharacterHealth& v){
            a(v.health,v.armor,v.downed,v.respawnAt,v.bleedUntil,v.blindUntil,v.lastDamage);
        })&&fieldCodec<CharacterWeapons>(c,"aiverse.actor.character.weapons",[](Archive& a,CharacterWeapons& v){
            a(v.weaponIndex,v.ammo,v.reserve,v.storedAmmo,v.storedReserve,v.shots,v.hits,v.switchUntil,
                v.fireHeld,v.reloadUntil,v.nextFire,v.recoil,v.aiming,v.owned,v.attachments);
        })&&fieldCodec<CharacterInventory>(c,"aiverse.actor.character.inventory",[](Archive& a,CharacterInventory& v){
            if(a.writing()&&v.bag.size()>24){a.fail();return;}
            uint32_t n=a.count(v.bag.size(),8);
            if(!a.ok()||n>24){a.fail();return;}
            if(!a.writing())v.bag.resize(n);
            for(auto& item:v.bag)a(item);
            a(v.money);
        })&&fieldCodec<CharacterControl>(c,"aiverse.actor.character.control",[](Archive& a,CharacterControl& v){
            a(v.input,v.lastInputTick,v.lastSequence);
        })&&fieldCodec<CharacterAction>(c,"aiverse.actor.character.action",[](Archive& a,CharacterAction& v){
            a(v.activeAction,v.actionUntil,v.actionTarget,v.actionOrigin);
            if(uint32_t(v.activeAction)>uint32_t(Action::Vault))a.fail();
        })&&fieldCodec<CharacterMelee>(c,"aiverse.actor.character.melee",[](Archive& a,CharacterMelee& v){
            auto& m=v.melee;
            a(m.attack,m.queued,m.started,m.queuedAt,m.lastTick,m.initialized);
            if(a.writing()&&m.hitTargets.size()>255){a.fail();return;}
            uint32_t n=a.count(m.hitTargets.size(),8);
            if(!a.ok()||n>255){a.fail();return;}
            if(!a.writing())m.hitTargets.resize(n);
            for(auto& id:m.hitTargets)a(id);
            a(v.parryUntil,v.staggerUntil,v.lockTarget,v.blockHeld,v.meleeHasTip,v.meleeTip);
        })&&fieldCodec<BotMemory>(c,"aiverse.actor.bot_memory",[](Archive& a,BotMemory& v){a(v.bytes);})
          &&fieldCodec<LocomotionBody>(c,"aiverse.actor.locomotion",[](Archive& a,LocomotionBody& v){
            io(a,v);if(!valid(v))a.fail();
        })&&c.addTyped<lib::systems::GearLoadout>("aiverse.actor.gear")
          &&c.addTyped<Loot>("aiverse.actor.loot")
          &&c.addTyped<Vehicle>("aiverse.actor.vehicle")
          &&c.addTyped<world::RegionDormant>("aiverse.world.region_dormant")
          &&vehicles::registerDurableVehicleComponents(c)
          &&c.addRebuilder(rebuildDerived);
    }

    // Separate codec registration only: all values still live in the canonical
    // ECS World. Do not register this into a sealed rollback registry.
    static void registerResources(PersistentEcsRegistry& registry){
        registry.registerResource<State>("aiverse.actor.match_state",1);
        registry.registerResource<world::RegionMoveRequests>("aiverse.world.region_move_requests",
            world::RegionMoveRequests::kSchema);
        registry.registerResource<world::RegionDirtyQueue>("aiverse.world.region_dirty_queue",
            world::RegionDirtyQueue::kSchema);
        registry.addLoadValidator("aiverse.world.region_move_requests",world::validateRegionMoveRequests);
        registry.addLoadValidator("aiverse.actor.portable_resources",[](const World& w,std::string& error){
            const auto* state=w.resourceConst<State>();
            const auto* dirty=w.resourceConst<world::RegionDirtyQueue>();
            if(!state||!dirty||!dirty->entries.empty()||!w.resourceConst<world::RegionMoveRequests>()){
                error="missing actor resources or undrained runtime notifications";return false;
            }
            return true;
        });
    }
    static world::DurableResourceTransaction::Policy resourcePolicy(const PersistentEcsRegistry& registry){
        return {&registry,{"aiverse.actor.match_state","aiverse.world.region_dirty_queue",
            "aiverse.world.region_move_requests"}};
    }
    // A complete actor runtime cannot yet be reconstructed if it contains
    // projectile or race state. Reject rather than silently dropping it.
    static bool supportedWorld(const World& w){
        const auto* dirty=w.resourceConst<world::RegionDirtyQueue>();
        return !w.countExisting<Projectile>()&&!w.resourceConst<vehicles::RaceState>()&&
            w.resourceConst<State>()&&dirty&&dirty->entries.empty()&&w.resourceConst<world::RegionMoveRequests>();
    }

    // A region actor's slot index is derived from its restored components.
    // Never serialize CharacterIndex::bySlot as native Entity handles.
    static bool rebuildDerived(World& w,const world::RegionEntityLedger& ledger){
        world::PersistentEntityReferences refs;
        if(!refs.rebuild(w,ledger))return false;
        CharacterIndex index;
        bool ok=true;
        w.eachExistingConst<CharacterId>([&](Entity e,const CharacterId& id){
            if(!ok)return;
            const auto* tag=w.tryGetConst<world::RegionStableIdentity>(e);
            if(!tag||!id.slot||!w.has<CharacterBody>(e)||!w.has<CharacterHealth>(e)||
               !w.has<CharacterWeapons>(e)||!w.has<CharacterInventory>(e)||!w.has<CharacterControl>(e)||
               !w.has<CharacterAction>(e)||!w.has<CharacterMelee>(e)){ok=false;return;}
            if(index.bySlot.size()<id.slot)index.bySlot.resize(id.slot,kNullEntity);
            auto& slot=index.bySlot[id.slot-1];
            if(slot.valid()){ok=false;return;}slot=e;
        });
        if(!ok||std::any_of(index.bySlot.begin(),index.bySlot.end(),[](Entity e){return !e.valid();}))return false;
        std::set<uint32_t> objectIds;
        for(auto id:ledger.liveIds()){
            Entity e;
            if(!refs.resolve(w,id,e))return false; // this policy materializes all live actors
            const auto* character=w.tryGetConst<CharacterId>(e);
            const auto* car=w.tryGetConst<Vehicle>(e);
            const auto* loot=w.tryGetConst<Loot>(e);
            if(unsigned(character!=nullptr)+unsigned(car!=nullptr)+unsigned(loot!=nullptr)!=1)return false;
            if(car&&(!car->id||!objectIds.insert(car->id).second))return false;
            if(loot&&(!loot->id||!objectIds.insert(loot->id).second))return false;
        }
        w.eachExistingConst<vehicles::VehicleDrive>([&](Entity e,const vehicles::VehicleDrive& d){
            if(!ok)return;
            if(!w.has<Vehicle>(e)||!vehicles::valid(d)||!d.requests.empty()){ok=false;return;}
            if(d.driver.valid()){
                const auto* id=w.tryGetConst<CharacterId>(d.driver);
                const auto* back=w.tryGetConst<vehicles::VehicleOccupant>(d.driver);
                if(!id||id->slot!=d.player||!back||back->vehicle!=e){ok=false;return;}
            }
            for(const auto& [seat,slot]:d.seats.occupants){
                (void)seat;if(slot<=0||uint64_t(slot)>index.bySlot.size()){ok=false;return;}
            }
        });
        w.eachExistingConst<vehicles::VehicleOccupant>([&](Entity e,const vehicles::VehicleOccupant& back){
            const auto* drive=w.tryGetConst<vehicles::VehicleDrive>(back.vehicle);
            if(!w.has<CharacterId>(e)||!drive||drive->driver!=e||drive->type!=back.type)ok=false;
        });
        if(!ok)return false;
        w.setResource<CharacterIndex>(std::move(index));return true;
    }

    // Private shell creation, before component payloads are decoded.
    static bool spawnShell(World& w,world::RegionEntityId id,const world::RegionEntityRecord& record){
        if(!id||!record.live)return false;
        auto e=w.create();w.add<world::RegionStableIdentity>(e,world::RegionStableIdentity{id});return true;
    }
    // All live ledger identities are materialized under this explicit policy.
    // Retire old shells first, then create new shells so references are resolvable
    // before component application. Never call against a live Simulation.
    static bool materialize(World& w,const world::RegionEntityLedger& before,const world::RegionEntityLedger& after){
        world::PersistentEntityReferences old;
        if(!old.rebuild(w,before))return false;
        for(auto id:before.liveIds()){
            Entity e;if(!old.resolve(w,id,e))return false;
            const auto* next=after.find(id);
            if(!next||!next->live)w.destroy(e);
        }
        w.flush();
        for(auto id:after.liveIds()){
            const auto* prior=before.find(id);
            if(!prior||!prior->live){
                const auto* entry=after.find(id);
                if(!entry||!spawnShell(w,id,*entry))return false;
            }
        }
        world::PersistentEntityReferences checked;
        return checked.rebuild(w,after)&&checked.size()==after.liveCount();
    }
};
}
