#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::runtime {
struct StateDifference {std::string field;std::optional<std::int64_t> expected,actual;std::string reason="value";};
// NativeState-compatible adapter. Compare canonical ordered values, not hash collisions.
template<class State>
std::optional<StateDifference> firstStateDifference(const State& a,const State& b){
    auto values=[](const auto& left,const auto& right)->std::optional<StateDifference>{
        auto x=left.begin(),y=right.begin();
        while(x!=left.end()||y!=right.end()){
            if(y==right.end()||(x!=left.end()&&x->first<y->first))return StateDifference{"values/"+x->first,x->second,{}};
            if(x==left.end()||y->first<x->first)return StateDifference{"values/"+y->first,{},y->second};
            if(x->second!=y->second)return StateDifference{"values/"+x->first,x->second,y->second};
            ++x;++y;
        }
        return {};
    };
    if(auto d=values(a.values,b.values))return d;
    auto x=a.arrays.begin(),y=b.arrays.begin();
    while(x!=a.arrays.end()||y!=b.arrays.end()){
        if(y==b.arrays.end()||(x!=a.arrays.end()&&x->first<y->first))return StateDifference{"arrays/"+x->first,{},{},"missing actual array"};
        if(x==a.arrays.end()||y->first<x->first)return StateDifference{"arrays/"+y->first,{},{},"missing expected array"};
        const auto& l=x->second;const auto& r=y->second;
        for(std::size_t i=0;i<l.size()||i<r.size();++i){
            auto lv=i<l.size()?std::optional<std::int64_t>(l[i]):std::nullopt;
            auto rv=i<r.size()?std::optional<std::int64_t>(r[i]):std::nullopt;
            if(lv!=rv)return StateDifference{"arrays/"+x->first+"/"+std::to_string(i),lv,rv};
        }
        ++x;++y;
    }
    return {};
}
}
