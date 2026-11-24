#pragma once
// turn-search: generic game-tree search for any turn-based rules (board games, tactics, cards with open information,
// puzzles with an opponent). The rules adapter is a type with:
//   using State = ...; using Action = ...;             // Action: totally ordered (operator<) for deterministic ordering
//   std::vector<Action> actions(const State&) const;    // legal actions of the side to move
//   State apply(const State&, const Action&) const;
//   int side(const State&) const;                       // 0 or 1: who moves
//   std::optional<int64_t> terminal(const State&) const;// final score for side 0, nullopt while running
//   int64_t evaluate(const State&) const;               // heuristic score for side 0
// alphaBeta: iterative deepening negamax with alpha-beta, a node budget and deterministic move order (previous best
// first, then ascending actions). mcts: UCT with seeded random rollouts and a playout budget (integer statistics).
// Both return the chosen action plus an explanation: the score of every root action, the principal variation and the
// depth/nodes reached, so a game can show why the bot played a move. Header-only, no engine state.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "engine/core/Random.h"
namespace aiverse::lib::bots {
template<class Action> struct SearchExplanation {std::vector<std::pair<Action,int64_t>> rootScores;std::vector<Action> principal;uint32_t depth=0;uint64_t nodes=0;bool exhausted=false;};
template<class Action> struct SearchResult {Action action{};int64_t score=0;SearchExplanation<Action> why;};
inline constexpr int64_t kSearchWin=int64_t(1)<<40;

namespace detail_search {
template<class R> struct AB {
    const R& rules;uint64_t budget;uint64_t nodes=0;bool out=false;
    using S=typename R::State;using A=typename R::Action;
    int64_t sign(const S& s)const{return rules.side(s)==0?1:-1;}
    int64_t negamax(const S& s,uint32_t depth,int64_t alpha,int64_t beta,std::vector<A>& pv){
        if(++nodes>budget){out=true;return 0;}
        if(auto t=rules.terminal(s))return sign(s)**t;
        if(depth==0)return sign(s)*rules.evaluate(s);
        auto acts=rules.actions(s);std::sort(acts.begin(),acts.end());if(acts.empty())return sign(s)*rules.evaluate(s);
        int64_t best=std::numeric_limits<int64_t>::min()/2;std::vector<A> bestPv;
        for(auto& a:acts){std::vector<A> child;auto next=rules.apply(s,a);
            int64_t v=rules.side(next)==rules.side(s)?negamax(next,depth-1,alpha,beta,child):-negamax(next,depth-1,-beta,-alpha,child);
            if(out)return 0;if(v>best){best=v;bestPv.assign(1,a);bestPv.insert(bestPv.end(),child.begin(),child.end());}
            alpha=std::max(alpha,v);if(alpha>=beta)break;}
        pv=std::move(bestPv);return best;
    }
};
}
// Score is from the point of view of the side to move at `root`.
template<class R> std::optional<SearchResult<typename R::Action>> alphaBeta(const R& rules,const typename R::State& root,uint32_t maxDepth,uint64_t nodeBudget){
    using A=typename R::Action;auto acts=rules.actions(root);if(acts.empty()||maxDepth==0)return std::nullopt;std::sort(acts.begin(),acts.end());
    SearchResult<A> result;result.action=acts.front();detail_search::AB<R> ab{rules,nodeBudget};
    std::optional<A> previous;
    for(uint32_t depth=1;depth<=maxDepth;++depth){
        std::vector<std::pair<A,int64_t>> scores;std::vector<A> order=acts;
        if(previous){auto it=std::find(order.begin(),order.end(),*previous);if(it!=order.end())std::rotate(order.begin(),it,it+1);}
        int64_t best=std::numeric_limits<int64_t>::min()/2;A bestA=order.front();std::vector<A> bestPv;
        for(auto& a:order){std::vector<A> pv;auto next=rules.apply(root,a);
            int64_t v=rules.side(next)==rules.side(root)?ab.negamax(next,depth-1,std::numeric_limits<int64_t>::min()/2,std::numeric_limits<int64_t>::max()/2,pv)
                                                         :-ab.negamax(next,depth-1,std::numeric_limits<int64_t>::min()/2,std::numeric_limits<int64_t>::max()/2,pv);
            if(ab.out)break;scores.push_back({a,v});if(v>best||(v==best&&a<bestA)){best=v;bestA=a;bestPv.assign(1,a);bestPv.insert(bestPv.end(),pv.begin(),pv.end());}}
        if(ab.out){result.why.exhausted=true;break;}
        std::sort(scores.begin(),scores.end(),[](auto& x,auto& y){return x.first<y.first;});
        result.action=bestA;result.score=best;result.why.rootScores=scores;result.why.principal=bestPv;result.why.depth=depth;previous=bestA;
        if(std::abs(best)>=kSearchWin/2)break;                 // forced result found
    }
    result.why.nodes=ab.nodes;return result;
}

// ---- Monte Carlo tree search (UCT). Rollouts pick uniformly random actions up to rolloutDepth, then evaluate.
// scoreScale converts terminal/evaluate scores to a win probability in milli: p = 500 + clamp(score*1000/scoreScale, -500, 500).
template<class R> std::optional<SearchResult<typename R::Action>> mcts(const R& rules,const typename R::State& root,uint32_t playouts,uint64_t seed,uint32_t rolloutDepth=64,
    int64_t scoreScale=1000,int64_t explorationMilli=1414){
    using A=typename R::Action;using S=typename R::State;
    struct Node {S state;std::vector<A> untried;std::vector<std::pair<A,std::unique_ptr<Node>>> kids;Node* parent=nullptr;int64_t wins=0,visits=0;int side=0;};   // wins: milli for parent's side
    auto rootActs=rules.actions(root);if(rootActs.empty()||!playouts)return std::nullopt;
    auto make=[&](const S& s,Node* p){auto n=std::make_unique<Node>();n->state=s;n->untried=rules.actions(s);std::sort(n->untried.begin(),n->untried.end());std::reverse(n->untried.begin(),n->untried.end());
        n->parent=p;n->side=rules.side(s);return n;};
    auto toMilli=[&](int64_t score0){auto p=500+std::clamp<int64_t>(score0*1000/std::max<int64_t>(1,scoreScale),-500,500);return p;};   // for side 0
    Random rng(seed);auto top=make(root,nullptr);uint64_t nodes=0;
    auto isqrt=[](uint64_t v){uint64_t r=0,bit=uint64_t(1)<<62;while(bit>v)bit>>=2;while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;};
    for(uint32_t i=0;i<playouts;++i){
        Node* n=top.get();
        while(n->untried.empty()&&!n->kids.empty()){                                             // select
            int64_t best=std::numeric_limits<int64_t>::min();Node* pick=nullptr;auto lnN=int64_t(64-__builtin_clzll(uint64_t(n->visits)|1))*693;   // ~ln(N)*1000
            for(auto& [a,k]:n->kids){auto exploit=k->wins/std::max<int64_t>(1,k->visits);auto explore=explorationMilli*int64_t(isqrt(uint64_t(lnN*1000/std::max<int64_t>(1,k->visits))))/1000;
                auto u=exploit+explore;if(u>best){best=u;pick=k.get();}}
            n=pick;}
        if(!n->untried.empty()&&!rules.terminal(n->state)){auto a=n->untried.back();n->untried.pop_back();n->kids.push_back({a,make(rules.apply(n->state,a),n)});n=n->kids.back().second.get();++nodes;}
        S s=n->state;int64_t score0=0;bool done=false;
        for(uint32_t d=0;d<rolloutDepth;++d){if(auto t=rules.terminal(s)){score0=*t;done=true;break;}auto acts=rules.actions(s);if(acts.empty())break;
            std::sort(acts.begin(),acts.end());s=rules.apply(s,acts[rng.below(uint32_t(acts.size()))]);}
        if(!done){if(auto t=rules.terminal(s))score0=*t;else score0=rules.evaluate(s);}
        auto p0=toMilli(score0);
        for(Node* x=n;x;x=x->parent){++x->visits;if(x->parent)x->wins+=x->parent->side==0?p0:1000-p0;}
    }
    SearchResult<A> r;int64_t bestVisits=-1;
    for(auto& [a,k]:top->kids){auto v=k->visits;r.why.rootScores.push_back({a,k->wins/std::max<int64_t>(1,v)});if(v>bestVisits||(v==bestVisits&&a<r.action)){bestVisits=v;r.action=a;r.score=k->wins/std::max<int64_t>(1,v);}}
    std::sort(r.why.rootScores.begin(),r.why.rootScores.end(),[](auto& x,auto& y){return x.first<y.first;});
    for(Node* n=top.get();n&&!n->kids.empty();){Node* best=nullptr;A ba{};int64_t bv=-1;for(auto& [a,k]:n->kids)if(k->visits>bv){bv=k->visits;best=k.get();ba=a;}r.why.principal.push_back(ba);n=best;}
    r.why.nodes=nodes;r.why.depth=uint32_t(r.why.principal.size());return r;
}
}
