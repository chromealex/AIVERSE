#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace aiverse::lib::app {
struct MatchSample { std::uint64_t seed=0,ticks=0; std::string first,second,winner; };
struct BalanceRow { std::size_t played=0,wins=0,losses=0,draws=0; };
// Host analysis only. Mirrored matches should use the same seed and swapped starting slots.
inline bool addSample(std::map<std::string,BalanceRow>& report,const MatchSample& s) {
    if(s.first.empty()||s.second.empty()||s.first==s.second||(!s.winner.empty()&&s.winner!=s.first&&s.winner!=s.second))return false;
    auto& a=report[s.first];auto& b=report[s.second];++a.played;++b.played;
    if(s.winner.empty()){++a.draws;++b.draws;}else if(s.winner==s.first){++a.wins;++b.losses;}else{++b.wins;++a.losses;}
    return true;
}
struct MatchCase { std::string first,second;std::uint64_t seed; };
inline std::vector<MatchCase> roundRobin(const std::vector<std::string>& factions,const std::vector<std::uint64_t>& seeds) {
    std::map<std::string,bool> seen;
    for(const auto& f:factions)if(f.empty()||!seen.emplace(f,true).second)return {};
    std::vector<MatchCase> out;
    for(std::size_t i=0;i<factions.size();++i)for(std::size_t j=i+1;j<factions.size();++j)for(auto seed:seeds){out.push_back({factions[i],factions[j],seed});out.push_back({factions[j],factions[i],seed});}
    return out;
}
}
