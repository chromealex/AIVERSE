// Explicit foundation samples from the existing structural owner.
// No new ECS component, solver or terrain authority. Input must be recorded
// with the authoritative terrain command; never infer supports from graphics.
#pragma once
#include "FractureEcs.h"
#include "engine/game/Surface.h"
#include "MorphPhysisGravity.h"
#include <optional>
#include <span>

namespace aiverse::fracture {
struct PlanetFoundation {
    Entity member=kNullEntity;
    std::string bodyId;
    std::vector<Vec3> feet; // canonical fixed world positions of structural contacts
    uint16_t minimumContacts=1;
    Fixed maximumGap=Fixed::fromRatio(1,8);
};
struct PlanetFoundationChange {
    std::vector<Entity> lostAnchors;
    std::vector<Entity> newlyDetached;
    // Structural owners retain pose, not GameCore's automatic ground reattachment.
    std::vector<uint64_t> preservePose;
};
inline constexpr size_t kMaxFoundationSamples=65536;
inline constexpr size_t kMaxFeetPerFoundation=64;

// Test only loss of radial terrain support. Raised ground is still support;
// penetration and stress are handled by the native physics/structural owner.
// A lost anchor stays lost until an explicit repair; graph propagation uses
// the existing FRACTURE solver and outbox. No duplicate gravity or damage.
inline std::optional<PlanetFoundationChange> reconcilePlanetFoundations(
        World& world,const game::Surface& previous,const game::Surface& next,
        std::span<const PlanetFoundation> foundations,uint32_t completedTick){
    if(!previous.tab||previous.tab!=next.tab||completedTick==UINT32_MAX||
       foundations.size()>kMaxMembers||world.countExisting<Member>()>kMaxMembers||
       world.countExisting<Bond>()>kMaxBonds)return {};
    const World& read=world;
    PlanetFoundationChange result;
    size_t total=0;
    std::vector<uint64_t> seen;
    for(const auto& foundation:foundations){
        const auto* member=read.tryGetConst<Member>(foundation.member);
        if(!member||!read.alive(foundation.member)||foundation.bodyId.empty()||
           foundation.feet.empty()||foundation.feet.size()>kMaxFeetPerFoundation||
           foundation.minimumContacts<1||foundation.minimumContacts>foundation.feet.size()||
           foundation.maximumGap<Fixed{}||foundation.maximumGap>Fixed::fromInt(4)||
           foundation.feet.size()>kMaxFoundationSamples-total)return {};
        total+=foundation.feet.size();
        seen.push_back(foundation.member.packed());
        result.preservePose.push_back(foundation.member.packed());
        const int body=previous.findBody(foundation.bodyId);
        if(body<0||!previous.bodies[size_t(body)].hasSurface())return {};
        size_t beforeContacts=0,afterContacts=0;
        for(const auto& foot:foundation.feet){
            const auto frame=lib::worldprocess::surfaceFrame(&previous,foot);
            if(!frame||frame->body!=body)return {};
            const auto altitude=previous.altitude(foot);
            const auto beforeGap=altitude-previous.heightAt(foot);
            const auto afterGap=altitude-next.heightAt(foot);
            // Require meaningful initial contact, not a deeply buried point.
            if(fxAbs(beforeGap)<=foundation.maximumGap)++beforeContacts;
            if(afterGap<=foundation.maximumGap)++afterContacts;
        }
        if(!member->anchored||member->broken||member->detached)continue;
        if(beforeContacts<foundation.minimumContacts)return {};
        if(afterContacts<foundation.minimumContacts)result.lostAnchors.push_back(foundation.member);
    }
    std::sort(seen.begin(),seen.end());
    if(std::adjacent_find(seen.begin(),seen.end())!=seen.end())return {};
    // Preserve already detached members too, even if their foundation is
    // not in this command. Never project a falling body back onto terrain.
    read.eachExistingConst<Member>([&](Entity e,const Member& m){
        if(read.alive(e)&&(m.detached||m.broken))result.preservePose.push_back(e.packed());
    });
    if(!result.lostAnchors.empty()){
        World staged=read;
        std::sort(result.lostAnchors.begin(),result.lostAnchors.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(Entity root:result.lostAnchors)staged.get<Member>(root).anchored=false;
        if(!advance(staged,completedTick))return {};
        staged.eachExistingConst<Member>([&](Entity e,const Member& m){
            const auto* old=read.tryGetConst<Member>(e);
            if(m.detached&&old&&!old->detached){
                result.newlyDetached.push_back(e);
                result.preservePose.push_back(e.packed());
            }
        });
        std::sort(result.newlyDetached.begin(),result.newlyDetached.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        world=std::move(staged);
    }
    std::sort(result.preservePose.begin(),result.preservePose.end());
    result.preservePose.erase(std::unique(result.preservePose.begin(),result.preservePose.end()),result.preservePose.end());
    return result;
}
} // namespace aiverse::fracture
