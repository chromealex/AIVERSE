#pragma once
#include "engine/mechanics/Namespace.h"
// secret-state: hidden information for authoritative multiplayer (cards, fog-of-war objects, sealed bids, simultaneous
// orders). The server keeps the true state; clients only receive projections.
// - Secret zones: every object (card, tile, token) lives in a zone with a visibility rule (owner only, everyone,
//   nobody, a list of seats). projectForSeat returns per-seat opaque handles instead of ids for hidden objects, and
//   reshuffling a zone re-keys the handles, so a client cannot follow a card through a shuffle; reveal events tell each
//   seat what became visible. Every projection carries a version so a reconnecting client asks for a full snapshot of
//   its seat (`snapshotForSeat`) and then applies versions in order.
// - Commit/reveal rounds for simultaneous decisions: each seat submits a commitment (hash of value and salt); reveals
//   are checked against it; the round resolves when all seats revealed or the deadline passed (missing = default).
//   The default hash is FNV-1a (fine against curious clients under an authoritative server); pass a cryptographic hash
//   when players must not trust the server either.
// Integer ids, ascending order; record kinds 222 (SecretState) and 223 (CommitRound).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum class Visibility:uint8_t {Owner,Everyone,Nobody,Seats};
struct SecretZone {int64_t id=0,owner=0;Visibility visibility=Visibility::Owner;std::vector<int32_t> seats;bool ordered=true;};   // seats: for Visibility::Seats
struct SecretState {std::map<int64_t,SecretZone> zones;std::map<int64_t,int64_t> objectZone;std::map<int64_t,std::vector<int64_t>> zoneObjects;   // zone -> ordered objects
    std::map<int64_t,uint64_t> zoneEpoch;uint64_t secret=0,version=0;};          // secret: server-only key for handles (never sent)
struct SeatObject {uint64_t handle=0;int64_t object=0;bool visible=false;};       // object 0 when hidden
struct SeatZoneView {int64_t zone=0;std::vector<SeatObject> objects;std::size_t count=0;};
struct SeatView {int32_t seat=0;uint64_t version=0;std::vector<SeatZoneView> zones;};
struct RevealEvent {int32_t seat=0;int64_t object=0,zone=0;};
inline bool canSee(const SecretZone& z,int32_t seat){switch(z.visibility){case Visibility::Everyone:return true;case Visibility::Nobody:return false;
    case Visibility::Owner:return z.owner==seat;default:return std::find(z.seats.begin(),z.seats.end(),seat)!=z.seats.end();}}
inline uint64_t secretHandle(const SecretState& s,int64_t zone,int64_t object,int32_t seat){
    Hasher h;h.u64(s.secret);h.i64(zone);h.u64(s.zoneEpoch.contains(zone)?s.zoneEpoch.at(zone):0);h.i64(object);h.i32(seat);return h.value()|1;}
inline bool addZone(SecretState& s,SecretZone z){if(z.id<=0||s.zones.contains(z.id)||uint8_t(z.visibility)>3)return false;std::sort(z.seats.begin(),z.seats.end());s.zones[z.id]=z;s.zoneObjects[z.id];++s.version;return true;}
inline bool placeObject(SecretState& s,int64_t object,int64_t zone,std::vector<RevealEvent>* reveals=nullptr,std::span<const int32_t> seats={}){
    if(object<=0||!s.zones.contains(zone))return false;
    std::optional<int64_t> from;if(auto it=s.objectZone.find(object);it!=s.objectZone.end()){from=it->second;auto& v=s.zoneObjects[it->second];v.erase(std::find(v.begin(),v.end(),object));}
    s.objectZone[object]=zone;s.zoneObjects[zone].push_back(object);++s.version;
    if(reveals)for(auto seat:seats){bool before=from&&canSee(s.zones.at(*from),seat),after=canSee(s.zones.at(zone),seat);if(after&&!before)reveals->push_back({seat,object,zone});}
    return true;
}
// Deterministic shuffle from a server seed; re-keys the zone so old handles no longer match.
inline bool shuffleZone(SecretState& s,int64_t zone,uint64_t seed){auto it=s.zoneObjects.find(zone);if(it==s.zoneObjects.end())return false;Random r(seed);auto& v=it->second;
    for(std::size_t i=v.size();i>1;--i)std::swap(v[i-1],v[r.below(uint32_t(i))]);++s.zoneEpoch[zone];++s.version;return true;}
inline SeatView projectForSeat(const SecretState& s,int32_t seat){
    SeatView view{seat,s.version,{}};
    for(auto& [zid,z]:s.zones){SeatZoneView zv{zid,{},s.zoneObjects.at(zid).size()};bool see=canSee(z,seat);
        for(auto obj:s.zoneObjects.at(zid))zv.objects.push_back({secretHandle(s,zid,obj,seat),see?obj:0,see});
        if(!z.ordered&&!see)std::sort(zv.objects.begin(),zv.objects.end(),[](auto& a,auto& b){return a.handle<b.handle;});    // unordered hidden zones leak no order
        view.zones.push_back(std::move(zv));}
    return view;
}
inline SeatView snapshotForSeat(const SecretState& s,int32_t seat){return projectForSeat(s,seat);}
// Server-side lookup of a client's handle (a client plays "the card with handle H").
inline std::optional<int64_t> resolveHandle(const SecretState& s,int32_t seat,int64_t zone,uint64_t handle){auto it=s.zoneObjects.find(zone);if(it==s.zoneObjects.end())return std::nullopt;
    for(auto obj:it->second)if(secretHandle(s,zone,obj,seat)==handle)return obj;return std::nullopt;}
inline void io(Archive& a,SecretZone& z){a(z.id,z.owner,z.visibility,z.seats,z.ordered);}
inline void io(Archive& a,SecretState& s){a(s.zones,s.objectZone,s.zoneObjects,s.zoneEpoch,s.secret,s.version);}
inline uint16_t stateKind(const SecretState&){return 222;}
inline bool valid(const SecretState& s){for(auto& [o,z]:s.objectZone){auto it=s.zoneObjects.find(z);if(o<=0||it==s.zoneObjects.end()||std::find(it->second.begin(),it->second.end(),o)==it->second.end())return false;}
    std::size_t n=0;for(auto& [z,v]:s.zoneObjects){if(!s.zones.contains(z))return false;n+=v.size();}return n==s.objectZone.size();}

// ---- Commit / reveal.
using CommitHash=uint64_t(*)(int64_t value,uint64_t salt);
inline uint64_t fnvCommit(int64_t value,uint64_t salt){Hasher h;h.str("aiverse-commit");h.i64(value);h.u64(salt);return h.value();}
struct CommitRound {std::vector<int32_t> seats;std::map<int32_t,uint64_t> commits;std::map<int32_t,int64_t> reveals;uint32_t deadline=0;int64_t defaultValue=0;bool resolved=false;};
enum class CommitResult:uint8_t {Accepted,Rejected,BadReveal,Resolved};
inline CommitResult submitCommit(CommitRound& r,int32_t seat,uint64_t commitment){if(r.resolved||std::find(r.seats.begin(),r.seats.end(),seat)==r.seats.end()||r.commits.contains(seat))return CommitResult::Rejected;r.commits[seat]=commitment;return CommitResult::Accepted;}
// Reveals are accepted only after every seat committed (nobody can adapt to an early reveal).
inline CommitResult submitReveal(CommitRound& r,int32_t seat,int64_t value,uint64_t salt,CommitHash hash=fnvCommit){
    if(r.resolved||r.commits.size()!=r.seats.size()||!r.commits.contains(seat)||r.reveals.contains(seat))return CommitResult::Rejected;
    if(hash(value,salt)!=r.commits.at(seat))return CommitResult::BadReveal;r.reveals[seat]=value;return CommitResult::Accepted;}
// Resolves when all seats revealed or the deadline passed; missing seats get defaultValue. Returns values by seat.
inline std::optional<std::map<int32_t,int64_t>> resolveRound(CommitRound& r,uint32_t tick){
    if(r.resolved)return std::nullopt;if(r.reveals.size()<r.seats.size()&&tick<r.deadline)return std::nullopt;
    std::map<int32_t,int64_t> out;for(auto seat:r.seats)out[seat]=r.reveals.contains(seat)?r.reveals.at(seat):r.defaultValue;r.resolved=true;return out;}
inline void io(Archive& a,CommitRound& r){a(r.seats,r.commits,r.reveals,r.deadline,r.defaultValue,r.resolved);}
inline uint16_t stateKind(const CommitRound&){return 223;}
inline bool valid(const CommitRound& r){for(auto& [s,c]:r.commits)if(std::find(r.seats.begin(),r.seats.end(),s)==r.seats.end())return false;for(auto& [s,v]:r.reveals)if(!r.commits.contains(s))return false;return true;}
}
