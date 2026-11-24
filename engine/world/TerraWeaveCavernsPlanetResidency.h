#pragma once
#include "engine/world/TerraWeaveCavernsPlanetNativeOwner.h"
#include "engine/view/game/GameView.h"
#include <memory>
#include <map>

namespace aiverse::terraweave::caverns {
// Game/world owner holds this registry; no new scheduler or physics world.
// One owner per body, now accepting a complete disjoint multi-chart plan.
// Never open a second native owner for another chart on the same body.
class CavePlanetResidency {
    using Owners=std::map<std::string,std::unique_ptr<CavePlanetNativeOwner>>;
public:
    CavePlanetResidency():owners_(std::make_shared<Owners>()){}
    CavePlanetResidency(const CavePlanetResidency&)=delete;
    CavePlanetResidency& operator=(const CavePlanetResidency&)=delete;
    CavePlanetNativeOwner& open(CavePlanetFrame frame){
        const auto id=frame.anchor().bodyId;
        if(owners_->size()>=game::Surface::kMaxBodies||owners_->contains(id))
            throw std::invalid_argument("Caverns planetary owner capacity or duplicate body");
        for(const auto& [name,owner]:*owners_){
            (void)name;if(owner->frame().body()==frame.body())
                throw std::invalid_argument("Caverns planetary body index already owned");
        }
        auto candidate=std::make_unique<CavePlanetNativeOwner>(std::move(frame));
        return *owners_->emplace(id,std::move(candidate)).first->second;
    }
    CavePlanetNativeOwner* find(const std::string& body)const noexcept{
        const auto it=owners_->find(body);return it==owners_->end()?nullptr:it->second.get();
    }
    bool recoveryRequired()const noexcept{
        for(const auto& [id,owner]:*owners_){(void)id;if(owner->recoveryRequired())return true;}
        return false;
    }
    size_t residentBodies()const noexcept{return owners_->size();}
    size_t collect(gfx::Device& device){
        size_t count=0;for(auto& [id,owner]:*owners_){(void)id;count+=owner->collect(device);}return count;
    }
    void attach(game::GameView& view){
        std::weak_ptr<Owners> weak=owners_;
        view.setPlanetCaveDraw([weak,&view](render::Renderer3D& renderer,
                const render::SphereViewFrame& frame,double seconds){
            const auto owners=weak.lock();if(!owners)return;
            for(const auto& [id,owner]:*owners){(void)id;if(owner->recoveryRequired())return;}
            const auto& world=view.planets().world();
            for(const auto& [id,owner]:*owners){
                const int body=world.find(id);
                if(body<0||body!=owner->frame().body()||!owner->ready())continue;
                owner->drawAtBodyCenter(renderer,frame,world.center(body,seconds));
            }
        });
    }
    static void detach(game::GameView& view){view.setPlanetCaveDraw({});}
    bool close(const std::string& id,gfx::Device& device,game::PlanetRenderer& planet,
            physics::Scene& box,world::SpatialWorld& spatial,
            const CavePlanetNativeOwner::Exclusive& exclusive,
            const std::function<bool()>& restoreOriginalsAndRetireNavGi){
        const auto it=owners_->find(id);if(it==owners_->end())return true;
        if(!it->second->close(device,planet,box,spatial,exclusive,restoreOriginalsAndRetireNavGi))return false;
        owners_->erase(it);return true;
    }
    // Collision binding has already coordinated close/restore. Do not close twice.
    bool eraseClosed(const std::string& id){
        const auto it=owners_->find(id);if(it==owners_->end())return true;
        if(!it->second->closed())return false;
        owners_->erase(it);return true;
    }
    bool empty()const noexcept{return owners_->empty();}
    // The host MUST close every owner before destruction, or keep this
    // registry alive in its explicit recovery state. No implicit GPU free.
private:
    std::shared_ptr<Owners> owners_;
};
} // namespace aiverse::terraweave::caverns
