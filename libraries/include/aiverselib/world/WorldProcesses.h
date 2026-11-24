#pragma once
// Experimental integer-only world policies. ECS authority/save/scheduling belongs to the caller.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>
#include <utility>
namespace aiverse::lib::worldprocess {
using I=std::int64_t;
struct Edge{std::size_t a=0,b=0;I strength=0;};
inline std::vector<bool> supported(std::size_t n,const std::vector<bool>& anchor,const std::vector<Edge>& bonds){
 if(anchor.size()!=n)return {};
 std::vector<std::vector<std::size_t>> adj(n);
 for(auto e:bonds)if(e.a<n&&e.b<n&&e.a!=e.b&&e.strength>0){adj[e.a].push_back(e.b);adj[e.b].push_back(e.a);}
 auto out=anchor;std::queue<std::size_t> q;
 for(std::size_t i=0;i<n;++i)if(out[i])q.push(i);
 while(!q.empty()){auto u=q.front();q.pop();for(auto v:adj[u])if(!out[v]){out[v]=true;q.push(v);}}
 return out;
}
struct Flow{std::size_t from=0,to=0;I cap=0;};
inline bool diffuse(std::vector<I>& mass,const std::vector<Flow>& flows){
 for(I m:mass)if(m<0)return false;
 for(auto f:flows)if(f.from>=mass.size()||f.to>=mass.size()||f.cap<0)return false;
 std::vector<I> change(mass.size(),0),remaining=mass;
 // Input connections must be in a stable, caller-defined order.
 for(auto f:flows){if(f.from==f.to)continue;
  I head=mass[f.from]-mass[f.to];if(head<=1)continue;
  I amount=std::min({head/2,f.cap,remaining[f.from]});
  if(change[f.from]<std::numeric_limits<I>::min()+amount||
     change[f.to]>std::numeric_limits<I>::max()-amount)return false;
  change[f.from]-=amount;change[f.to]+=amount;remaining[f.from]-=amount;
 }
 for(std::size_t i=0;i<mass.size();++i)if(change[i]>0&&mass[i]>std::numeric_limits<I>::max()-change[i])return false;
 for(std::size_t i=0;i<mass.size();++i)mass[i]+=change[i];
 return true;
}
// Shared multi-species diffusion over a single stable directed edge list.
// Every species reads its own start-of-tick mass. Incoming mass cannot
// finance outgoing transfers until the next tick. Zero-flow edges/fields
// never allocate scratch. Validate all species before any output write.
struct DiffusionActivity {
    std::size_t directedFlows=0,activeFlows=0,transfers=0,activeSpecies=0;
};
template<std::size_t N>
inline bool diffuseSpecies(std::array<std::vector<I>,N>& mass,
        const std::vector<Flow>& flows,DiffusionActivity* activity=nullptr){
    static_assert(N>0);
    const auto count=mass[0].size();
    for(const auto& field:mass){
        if(field.size()!=count)return false;
        for(I m:field)if(m<0)return false;
    }
    for(auto f:flows)if(f.from>=count||f.to>=count||f.cap<0)return false;
    struct Scratch {std::vector<I> change,remaining;bool active=false;};
    std::array<Scratch,N> scratch{};
    DiffusionActivity stats{};
    stats.directedFlows=flows.size();
    for(auto f:flows){
        if(f.from==f.to||!f.cap)continue;
        bool moved=false;
        for(std::size_t s=0;s<N;++s){
            const auto& field=mass[s];
            const I head=field[f.from]-field[f.to];
            if(head<=1)continue;
            auto& work=scratch[s];
            const I available=work.active?work.remaining[f.from]:field[f.from];
            const I amount=std::min({head/2,f.cap,available});
            if(!amount)continue;
            if(!work.active){
                work.change.assign(count,0);
                work.remaining=field;
                work.active=true;++stats.activeSpecies;
            }
            if(work.change[f.from]<std::numeric_limits<I>::min()+amount||
               work.change[f.to]>std::numeric_limits<I>::max()-amount)return false;
            work.change[f.from]-=amount;
            work.change[f.to]+=amount;
            work.remaining[f.from]-=amount;
            moved=true;++stats.transfers;
        }
        if(moved)++stats.activeFlows;
    }
    // The scalar reference checks overflow at the end of each species.
    // Check every species before mutating even one input field.
    for(std::size_t s=0;s<N;++s)if(scratch[s].active)
        for(std::size_t i=0;i<count;++i){
            const I delta=scratch[s].change[i];
            if(delta>0&&mass[s][i]>std::numeric_limits<I>::max()-delta)return false;
        }
    for(std::size_t s=0;s<N;++s)if(scratch[s].active)
        for(std::size_t i=0;i<count;++i)
            mass[s][i]+=scratch[s].change[i];
    if(activity)*activity=stats;
    return true;
}
inline bool relax(I& pos,I rest,I stiffness){
 if(stiffness<0||stiffness>1000)return false;
 if((rest>0&&pos<std::numeric_limits<I>::min()+rest)||
    (rest<0&&pos>std::numeric_limits<I>::max()+rest))return false;
 I d=rest-pos,step=(d/1000)*stiffness+((d%1000)*stiffness)/1000;
 if((step>0&&pos>std::numeric_limits<I>::max()-step)||
    (step<0&&pos<std::numeric_limits<I>::min()-step))return false;
 pos+=step;return true;
}
struct AcousticEdge{std::size_t from=0,to=0;I loss=0;};
inline std::vector<I> audibility(std::size_t n,std::size_t source,I power,const std::vector<AcousticEdge>& edges){
 if(source>=n||power<0)return {};
 std::vector<std::vector<std::pair<std::size_t,I>>> adj(n);
 for(auto e:edges){if(e.from>=n||e.to>=n||e.loss<0)return {};adj[e.from].push_back({e.to,e.loss});}
 std::vector<I> best(n,-1);best[source]=power;
 std::priority_queue<std::pair<I,std::size_t>> q;q.push({power,source});
 while(!q.empty()){auto [p,u]=q.top();q.pop();if(p!=best[u])continue;
  for(auto [v,loss]:adj[u])if(loss<=p&&p-loss>best[v]){best[v]=p-loss;q.push({best[v],v});}
 }
 return best;
}
inline std::vector<I> assignSlots(const std::vector<std::vector<std::size_t>>& preferences,std::size_t slots){
 std::vector<bool> used(slots,false);std::vector<I> result(preferences.size(),-1);
 for(std::size_t a=0;a<preferences.size();++a)for(auto slot:preferences[a])if(slot<slots&&!used[slot]){
  used[slot]=true;result[a]=static_cast<I>(slot);break;
 }
 return result;
}
inline bool erode(I& depth,I wear){if(depth<0||wear<0)return false;depth-=std::min(depth,wear);return true;}
inline std::vector<bool> energize(std::size_t n,const std::vector<bool>& sources,
                                  const std::vector<std::pair<std::size_t,std::size_t>>& wires){
 if(sources.size()!=n)return {};
 std::vector<std::vector<std::size_t>> adj(n);
 for(auto [a,b]:wires){if(a>=n||b>=n)return {};adj[a].push_back(b);}
 auto live=sources;std::queue<std::size_t> q;for(std::size_t i=0;i<n;++i)if(live[i])q.push(i);
 while(!q.empty()){auto u=q.front();q.pop();for(auto v:adj[u])if(!live[v]){live[v]=true;q.push(v);}}
 return live;
}
inline bool settle(std::vector<I>& cells,std::size_t width,std::size_t height,I capacity){
 if(width==0||height==0||width>cells.size()/height||width*height!=cells.size()||capacity<0)return false;
 for(I v:cells)if(v<0||v>capacity)return false;
 for(std::size_t y=height-1;y>0;--y)for(std::size_t x=0;x<width;++x){
  auto src=(y-1)*width+x,dst=y*width+x;I moved=std::min(cells[src],capacity-cells[dst]);
  cells[src]-=moved;cells[dst]+=moved;
 }
 return true;
}
inline bool age(I& durability,I lossPerTick,I ticks){
 if(durability<0||lossPerTick<0||ticks<0)return false;
 I elapsed=(lossPerTick==0||ticks<=durability/lossPerTick)?lossPerTick*ticks:durability;
 durability-=elapsed;return true;
}
inline std::vector<bool> shareKnowledge(std::vector<bool> known,
                         const std::vector<std::pair<std::size_t,std::size_t>>& contacts){
 auto start=known;
 for(auto [src,dst]:contacts){if(src>=known.size()||dst>=known.size())return {};if(start[src])known[dst]=true;}
 return known;
}
} // namespace aiverse::lib::worldprocess
