#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for weapon state.
// FeedState, ChargeState and LoadoutState are dynamic state: store each in its own NativeState key
// and bind the immutable FeedConfig/ChargeConfig/frame/attachment definition versions in the save
// envelope. Per-call outcomes (penetration, explosion, launch, feed/charge steps) are hashable so
// owners can fold the committed outcome into their simulation checksum.
#include "Ballistics.h"
#include "WeaponMods.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,FeedState& s){a(s.loaded,s.heat,s.lastTick,s.lastShot,s.phaseStart,s.nextInsert,s.spin,s.phase,s.initialized,s.fired);}
inline void io(Archive& a,ChargeState& s){a(s.started,s.lastTick,s.charging,s.latched,s.initialized);}
inline void io(Archive& a,LoadoutState& s){a(s.slots);}
// Weapon state kinds use 16..31 so other genre packages can grow below them.
inline uint16_t stateKind(const FeedState&){return 16;}
inline uint16_t stateKind(const ChargeState&){return 17;}
inline uint16_t stateKind(const LoadoutState&){return 18;}

inline void io(Archive& a,PenetrationOutcome& o){a(o.collider,o.material,o.thickness,o.damageIn,o.damageOut,o.speedOut,o.powerOut,o.passed);}
inline void io(Archive& a,PenetrationResult& r){a(r.layers,r.damage,r.speed,r.power,r.stop);}
inline void io(Archive& a,ExplosionHit& h){a(h.id,h.distance,h.falloff,h.exposure,h.damage,h.impulse,h.direction);}
inline void io(Archive& a,LaunchSolution& s){a(s.velocity,s.elevation,s.time);}
inline void io(Archive& a,LaunchSolutions& s){a(s.count,s.low,s.high);}
inline void io(Archive& a,InterceptSolution& s){a(s.found,s.converged,s.iterations,s.aimPoint,s.launch);}
inline void io(Archive& a,FeedStep& s){a(s.fired,s.dry,s.reloadStarted,s.reloadCompleted,s.reloadInterrupted,s.overheated,s.recovered,s.consumed,s.loadedAdded);}
inline void io(Archive& a,ChargeRelease& r){a(r.heldTicks,r.tier,r.fraction,r.scale,r.automatic,r.belowMin);}
inline void io(Archive& a,ChargeStep& s){
    bool has=s.release.has_value();a(s.started,s.cancelled,has);
    if(!a.writing())s.release=has?std::optional<ChargeRelease>(ChargeRelease{}):std::nullopt;
    if(has)io(a,*s.release);
}
// Folds the canonical Archive encoding (same bytes as the save body) into a checksum.
template<class T> void hashWeapon(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t weaponHash(const T& value){Hasher h;hashWeapon(h,value);return h.value();}
}
