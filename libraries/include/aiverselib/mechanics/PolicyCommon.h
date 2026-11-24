#pragma once
#include "engine/mechanics/Namespace.h"
// Shared helpers for deterministic policy packages (nav-links, volume-nav, structure-destruction,
// portals-rewind, item-affixes, quest-journal, wounds-disease, herds-taming, colonist-relations,
// utility-networks, railways, city-policy, transit-schedules, tactics-levels, diplomacy, hero-match, watercraft,
// secret-state): strictly increasing ticks, ascending unique ids, bounded values and state hashing over the
// saved bytes. Policies never read engine state; the owner reports facts and applies the returned outputs.
#include "engine/mechanics/Persistence.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Hash.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace aiverse::lib::mechanics {
inline constexpr Fixed kPolicyCoordLimit=Fixed::fromInt(100000),kPolicyMaxSpeed=Fixed::fromInt(1000);
inline constexpr std::size_t kPolicyMaxObjects=4096,kPolicyMaxCommands=4096;
// Ticks strictly increase once a state is initialized; a repeated or older tick is rejected without side effects.
inline bool policyTickOk(bool initialized,uint32_t last,uint32_t tick){return !initialized||tick>last;}
inline bool policyDtOk(Fixed dt){return dt>Fixed{}&&dt<=1_fx;}
inline bool policyBounded(Vec3 v,Fixed limit=kPolicyCoordLimit){return bounded(v,limit);}
template<class T> bool policySortedUnique(const std::vector<T>& v){for(std::size_t i=1;i<v.size();++i)if(!(v[i-1]<v[i]))return false;return true;}
template<class K,class V> bool policyPositiveKeys(const std::map<K,V>& m){return m.empty()||m.begin()->first>0;}
// Checksum of a saveable state: FNV-1a over the versioned save bytes (0 if the state is invalid).
template<class State> uint64_t policyHash(const State& s){auto bytes=save(s);if(bytes.empty())return 0;Hasher h;h.str(bytes);return h.value();}
inline Fixed policyMilli(int64_t milli){return Fixed::fromRatio(milli,1000);}
inline int64_t policyClampMilli(int64_t v){return std::clamp<int64_t>(v,0,1000);}
// Horizontal unit vector (x/z) or zero.
inline Vec3 policyFlat(Vec3 v){v.y={};return fxNormalize(v);}
}
