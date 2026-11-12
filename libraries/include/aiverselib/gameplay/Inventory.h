#pragma once
#include "Diagnostics.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
struct ItemType { std::int64_t id=0,maxStack=1,weight=0;std::uint64_t slots=0; };
struct ItemStack { std::int64_t type=0,count=0; };
struct InventoryConfig { std::size_t slots=20;std::int64_t maxWeight=1000000; };
template<class Lookup>
Decision checkInventory(const std::vector<ItemStack>& items,const InventoryConfig& c,Lookup lookup) {
    if(c.maxWeight<0||c.slots>65536||items.size()>c.slots)return {Reason::Capacity};
    std::int64_t weight=0;
    for(const auto& stack:items) {
        auto type=lookup(stack.type);
        if(!type||type->maxStack<1||type->weight<0||stack.count<1||stack.count>type->maxStack)return {Reason::Invalid,stack.type};
        if(type->weight && stack.count>(c.maxWeight-weight)/type->weight)return {Reason::Capacity,stack.type,c.maxWeight,weight};
        weight+=stack.count*type->weight;
    }
    return {};
}
template<class Lookup>
Decision addItem(std::vector<ItemStack>& items,const InventoryConfig& c,ItemStack item,Lookup lookup) {
    auto before=checkInventory(items,c,lookup);if(!before)return before;
    auto type=lookup(item.type);if(!type||item.count<1||type->maxStack<1||type->weight<0)return {Reason::Invalid,item.type};
    auto next=items;auto remaining=item.count;
    for(auto& s:next)if(s.type==item.type){auto n=std::min(remaining,type->maxStack-s.count);s.count+=n;remaining-=n;}
    while(remaining>0) {if(next.size()>=c.slots)return {Reason::Capacity,item.type};auto n=std::min(remaining,type->maxStack);next.push_back({item.type,n});remaining-=n;}
    auto result=checkInventory(next,c,lookup);if(result)items=std::move(next);return result;
}
inline Decision removeItem(std::vector<ItemStack>& items,ItemStack item) {
    if(item.count<1)return {Reason::Invalid};
    auto remaining=item.count;
    for(const auto& s:items)if(s.type==item.type){if(s.count<1)return {Reason::Invalid};remaining-=std::min(remaining,s.count);}
    if(remaining)return {Reason::Missing,item.type,item.count,item.count-remaining};
    remaining=item.count;for(auto& s:items)if(s.type==item.type){auto n=std::min(remaining,s.count);s.count-=n;remaining-=n;}
    std::erase_if(items,[](auto s){return s.count==0;});return {};
}
template<class Lookup>
Decision transferItem(std::vector<ItemStack>& from,std::vector<ItemStack>& to,const InventoryConfig& targetConfig,ItemStack item,Lookup lookup) {
    if(&from==&to)return {Reason::Invalid};
    auto a=from,b=to;
    auto d=removeItem(a,item);
    if(!d)return d;
    d=addItem(b,targetConfig,item,lookup);
    if(!d)return d;
    from=std::move(a);
    to=std::move(b);
    return {};
}
inline bool canEquip(const ItemType& type,std::uint32_t slot) {return slot<64 && (type.slots&(std::uint64_t{1}<<slot));}
struct Equipment {std::uint32_t slot=0;std::int64_t type=0;};
template<class Lookup>
Decision equipItem(std::vector<ItemStack>& bag,std::vector<Equipment>& equipped,std::int64_t item,std::uint32_t slot,Lookup lookup){
    auto type=lookup(item);if(!type||!canEquip(*type,slot))return {Reason::Target,item};
    for(const auto& e:equipped)if(e.slot==slot)return {Reason::Busy,slot};
    auto next=bag;auto d=removeItem(next,{item,1});if(!d)return d;
    equipped.push_back({slot,item});bag=std::move(next);return {};
}
template<class Lookup>
Decision unequipItem(std::vector<ItemStack>& bag,std::vector<Equipment>& equipped,const InventoryConfig& config,std::uint32_t slot,Lookup lookup){
    auto it=std::find_if(equipped.begin(),equipped.end(),[&](auto e){return e.slot==slot;});
    if(it==equipped.end())return {Reason::Missing,slot};
    auto d=addItem(bag,config,{it->type,1},lookup);if(!d)return d;equipped.erase(it);return {};
}
template<class Lookup>
Decision craftItems(std::vector<ItemStack>& bag,const InventoryConfig& config,std::span<const ItemStack> input,std::span<const ItemStack> output,Lookup lookup){
    auto next=bag;for(auto item:input){auto d=removeItem(next,item);if(!d)return d;}
    for(auto item:output){auto d=addItem(next,config,item,lookup);if(!d)return d;}bag=std::move(next);return {};
}
}
