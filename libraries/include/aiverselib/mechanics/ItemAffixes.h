#pragma once
#include "engine/mechanics/Namespace.h"
// item-affixes: Diablo/Path of Exile style item generation and modification as data.
// - Affix pools: prefix/suffix, exclusive groups, tiers, weights, item-level gates, slot masks, value ranges (integers).
// - rollItem: seeded generation of an item instance (rarity = number of prefixes/suffixes); the same seed and data always
//   give the same item. rerollValues / rerollAffix / addAffix / removeAffix are crafting operations with their own seeds.
// - Sockets of colours with gems; socketGem validates colour and count. itemStats sums affix and gem values per stat id
//   (feed them into attribute-modifiers as Add stages).
// - Elemental reactions: an aura per element on a target with expiry; applying an element that has a reaction with an
//   existing aura emits the reaction (damage multiplier, consumed auras) - Genshin-style, data-driven.
// - migrateItem: versioned item migration (renamed or removed affixes, value rescale) for content updates.
// Record kinds 205 (ItemInstance) and 206 (AuraState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct AffixDef {int64_t id=0,group=0,stat=0;bool prefix=true;int32_t tier=1;uint32_t weight=100,minItemLevel=1,slotMask=~0u;int64_t minValue=1,maxValue=1;};
struct ItemBaseDef {int64_t id=0;uint32_t slotMask=1;int32_t maxPrefixes=3,maxSuffixes=3;std::vector<uint8_t> socketColors;   // allowed colours for each socket slot (0 = any)
    std::vector<std::pair<int64_t,int64_t>> implicitStats;};
struct GemDef {int64_t id=0,stat=0,value=0;uint8_t color=1;};
struct AffixData {std::map<int64_t,AffixDef> affixes;std::map<int64_t,ItemBaseDef> bases;std::map<int64_t,GemDef> gems;uint32_t version=1;};
struct RolledAffix {int64_t affix=0,value=0;auto operator<=>(const RolledAffix&)const=default;};
struct ItemInstance {int64_t id=0,base=0;uint32_t itemLevel=1,version=1;uint64_t seed=0;std::vector<RolledAffix> affixes;std::vector<int64_t> sockets;  // gem id per socket, 0 = empty
    auto operator<=>(const ItemInstance&)const=default;};

inline bool valid(const AffixDef& a){return a.id>0&&a.group>=0&&a.stat>0&&a.tier>=1&&a.tier<=100&&a.weight>0&&a.weight<=1000000&&a.minItemLevel>=1&&a.minValue<=a.maxValue
    &&a.minValue>-1000000000&&a.maxValue<1000000000;}
inline bool valid(const AffixData& d){
    for(auto& [id,a]:d.affixes)if(id!=a.id||!valid(a))return false;
    for(auto& [id,b]:d.bases)if(id!=b.id||id<=0||b.maxPrefixes<0||b.maxSuffixes<0||b.maxPrefixes>8||b.maxSuffixes>8||b.socketColors.size()>12)return false;
    for(auto& [id,g]:d.gems)if(id!=g.id||id<=0||g.stat<=0||g.color==0)return false;
    return d.version>0;
}
inline int64_t rollValue(Random& r,const AffixDef& a){return a.minValue+int64_t(r.below(uint32_t(a.maxValue-a.minValue+1)));}
inline std::vector<int64_t> eligibleAffixes(const AffixData& d,const ItemBaseDef& b,const ItemInstance& item,bool prefix){
    std::set<int64_t> groups;for(auto& x:item.affixes){auto& a=d.affixes.at(x.affix);if(a.group)groups.insert(a.group);}
    std::vector<int64_t> out;for(auto& [id,a]:d.affixes)if(a.prefix==prefix&&(a.slotMask&b.slotMask)&&a.minItemLevel<=item.itemLevel&&(!a.group||!groups.contains(a.group))
        &&std::none_of(item.affixes.begin(),item.affixes.end(),[&](auto& x){return x.affix==id;}))out.push_back(id);
    return out;
}
inline std::optional<int64_t> pickWeighted(Random& r,const AffixData& d,const std::vector<int64_t>& ids){
    uint64_t total=0;for(auto id:ids)total+=d.affixes.at(id).weight;if(!total||total>UINT32_MAX)return std::nullopt;
    uint32_t x=r.below(uint32_t(total));for(auto id:ids){auto w=d.affixes.at(id).weight;if(x<w)return id;x-=w;}return std::nullopt;
}
inline int32_t countAffixes(const AffixData& d,const ItemInstance& it,bool prefix){int32_t n=0;for(auto& x:it.affixes)n+=d.affixes.at(x.affix).prefix==prefix;return n;}
inline void sortAffixes(ItemInstance& it){std::sort(it.affixes.begin(),it.affixes.end());}
inline bool valid(const AffixData& d,const ItemInstance& it){
    auto b=d.bases.find(it.base);if(it.id<=0||b==d.bases.end()||it.version!=d.version||it.sockets.size()>b->second.socketColors.size())return false;
    std::set<int64_t> groups;for(auto& x:it.affixes){auto a=d.affixes.find(x.affix);if(a==d.affixes.end()||x.value<a->second.minValue||x.value>a->second.maxValue)return false;
        if(a->second.group&&!groups.insert(a->second.group).second)return false;}
    if(countAffixes(d,it,true)>b->second.maxPrefixes||countAffixes(d,it,false)>b->second.maxSuffixes)return false;
    for(std::size_t i=0;i<it.sockets.size();++i)if(it.sockets[i]){auto g=d.gems.find(it.sockets[i]);auto want=b->second.socketColors[i];if(g==d.gems.end()||(want&&g->second.color!=want))return false;}
    return std::is_sorted(it.affixes.begin(),it.affixes.end());
}
// Adds one random affix of the given kind; false when none is eligible or the kind is full.
inline bool addAffix(const AffixData& d,ItemInstance& it,bool prefix,uint64_t seed){
    auto b=d.bases.find(it.base);if(b==d.bases.end())return false;
    if(countAffixes(d,it,prefix)>=(prefix?b->second.maxPrefixes:b->second.maxSuffixes))return false;
    Random r(seed);auto pick=pickWeighted(r,d,eligibleAffixes(d,b->second,it,prefix));if(!pick)return false;
    it.affixes.push_back({*pick,rollValue(r,d.affixes.at(*pick))});sortAffixes(it);return true;
}
inline std::optional<ItemInstance> rollItem(const AffixData& d,int64_t itemId,int64_t base,uint32_t itemLevel,int32_t prefixes,int32_t suffixes,uint64_t seed){
    if(!valid(d)||itemId<=0||!d.bases.contains(base)||itemLevel<1||prefixes<0||suffixes<0)return std::nullopt;
    ItemInstance it{itemId,base,itemLevel,d.version,seed,{},std::vector<int64_t>(d.bases.at(base).socketColors.size(),0)};Random r(seed,0x9e3779b97f4a7c15ull);
    for(int32_t i=0;i<prefixes;++i)if(!addAffix(d,it,true,uint64_t(r.next())<<32|r.next()))break;
    for(int32_t i=0;i<suffixes;++i)if(!addAffix(d,it,false,uint64_t(r.next())<<32|r.next()))break;
    return it;
}
inline bool rerollValues(const AffixData& d,ItemInstance& it,uint64_t seed){if(!valid(d,it))return false;Random r(seed);for(auto& x:it.affixes)x.value=rollValue(r,d.affixes.at(x.affix));return true;}
inline bool removeAffix(const AffixData& d,ItemInstance& it,int64_t affix){if(!valid(d,it))return false;auto n=it.affixes.size();
    std::erase_if(it.affixes,[&](auto& x){return x.affix==affix;});return it.affixes.size()!=n;}
// Replace one affix by a new random one of the same kind (prefix/suffix).
inline bool rerollAffix(const AffixData& d,ItemInstance& it,int64_t affix,uint64_t seed){
    if(!valid(d,it))return false;auto a=d.affixes.find(affix);if(a==d.affixes.end())return false;auto copy=it;
    if(!removeAffix(d,copy,affix)||!addAffix(d,copy,a->second.prefix,seed))return false;it=std::move(copy);return true;}
inline bool socketGem(const AffixData& d,ItemInstance& it,std::size_t socket,int64_t gem){
    if(!valid(d,it)||socket>=it.sockets.size()||it.sockets[socket])return false;auto g=d.gems.find(gem);if(g==d.gems.end())return false;
    auto want=d.bases.at(it.base).socketColors[socket];if(want&&want!=g->second.color)return false;it.sockets[socket]=gem;return true;}
inline std::optional<int64_t> unsocketGem(ItemInstance& it,std::size_t socket){if(socket>=it.sockets.size()||!it.sockets[socket])return std::nullopt;auto g=it.sockets[socket];it.sockets[socket]=0;return g;}
inline std::map<int64_t,int64_t> itemStats(const AffixData& d,const ItemInstance& it){
    std::map<int64_t,int64_t> out;if(!valid(d,it))return out;
    for(auto& [stat,v]:d.bases.at(it.base).implicitStats)out[stat]+=v;for(auto& x:it.affixes)out[d.affixes.at(x.affix).stat]+=x.value;
    for(auto g:it.sockets)if(g){auto& gem=d.gems.at(g);out[gem.stat]+=gem.value;}return out;
}
// Content migration from `fromVersion` to the data's version.
struct ItemMigration {uint32_t fromVersion=1;std::map<int64_t,int64_t> renamed;std::set<int64_t> removed;std::map<int64_t,std::pair<int64_t,int64_t>> rescale;};  // affix -> (num, den)
inline std::optional<ItemInstance> migrateItem(const AffixData& d,ItemInstance it,const ItemMigration& m){
    if(it.version!=m.fromVersion)return std::nullopt;std::vector<RolledAffix> next;
    for(auto x:it.affixes){if(m.removed.contains(x.affix))continue;if(auto r=m.renamed.find(x.affix);r!=m.renamed.end())x.affix=r->second;
        if(auto s=m.rescale.find(x.affix);s!=m.rescale.end()&&s->second.second)x.value=x.value*s->second.first/s->second.second;
        auto a=d.affixes.find(x.affix);if(a==d.affixes.end())continue;x.value=std::clamp(x.value,a->second.minValue,a->second.maxValue);next.push_back(x);}
    it.affixes=std::move(next);it.version=d.version;sortAffixes(it);
    for(auto& g:it.sockets)if(g&&!d.gems.contains(g))g=0;
    // drop affixes that now violate group/count limits, keeping the lowest ids
    ItemInstance clean=it;clean.affixes.clear();
    for(auto& x:it.affixes){clean.affixes.push_back(x);if(!valid(d,clean))clean.affixes.pop_back();}
    if(!valid(d,clean))return std::nullopt;return clean;
}
inline void io(Archive& a,RolledAffix& x){a(x.affix,x.value);}
inline void io(Archive& a,ItemInstance& it){a(it.id,it.base,it.itemLevel,it.version,it.seed,it.affixes,it.sockets);}
inline uint16_t stateKind(const ItemInstance&){return 205;}
inline bool valid(const ItemInstance& it){return it.id>0&&it.base>0&&it.version>0&&it.affixes.size()<=16&&it.sockets.size()<=12&&std::is_sorted(it.affixes.begin(),it.affixes.end());}

// ---- Elemental auras and reactions.
struct ReactionDef {int64_t id=0,trigger=0,aura=0;int64_t multiplierMilli=1000;bool consumeAura=true;};   // trigger element hitting an existing aura
struct ElementDef {int64_t id=0;uint32_t auraTicks=600;bool leavesAura=true;};
struct ReactionData {std::map<int64_t,ElementDef> elements;std::vector<ReactionDef> reactions;};   // first matching reaction wins (data order)
struct AuraState {std::map<int64_t,std::map<int64_t,uint32_t>> auras;uint32_t lastTick=0;bool initialized=false;};  // target -> element -> expires
struct ElementHit {int64_t target=0,element=0;};
struct ReactionEvent {int64_t target=0,reaction=0,element=0,aura=0,multiplierMilli=1000;};
inline bool valid(const ReactionData& d){for(auto& [id,e]:d.elements)if(id!=e.id||id<=0||e.auraTicks>1000000)return false;
    for(auto& r:d.reactions)if(r.id<=0||!d.elements.contains(r.trigger)||!d.elements.contains(r.aura)||r.multiplierMilli<0)return false;return true;}
// Hits are applied in the given order (the owner sorts by its own deterministic damage order).
inline std::optional<std::vector<ReactionEvent>> applyElements(const ReactionData& d,AuraState& state,uint32_t tick,std::span<const ElementHit> hits){
    if(!valid(d)||!policyTickOk(state.initialized,state.lastTick,tick)||hits.size()>kPolicyMaxCommands)return std::nullopt;
    for(auto& h:hits)if(h.target<=0||!d.elements.contains(h.element))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;std::vector<ReactionEvent> out;
    for(auto& [t,m]:s.auras)std::erase_if(m,[&](auto& e){return e.second<=tick;});std::erase_if(s.auras,[](auto& e){return e.second.empty();});
    for(auto& h:hits){auto& aura=s.auras[h.target];bool reacted=false;
        for(auto& r:d.reactions)if(r.trigger==h.element&&aura.contains(r.aura)){out.push_back({h.target,r.id,h.element,r.aura,r.multiplierMilli});if(r.consumeAura)aura.erase(r.aura);reacted=true;break;}
        if(!reacted&&d.elements.at(h.element).leavesAura)aura[h.element]=tick+d.elements.at(h.element).auraTicks;
        if(aura.empty())s.auras.erase(h.target);}
    state=std::move(s);return out;
}
inline void io(Archive& a,AuraState& s){a(s.auras,s.lastTick,s.initialized);}
inline uint16_t stateKind(const AuraState&){return 206;}
inline bool valid(const AuraState& s){return policyPositiveKeys(s.auras);}
}
