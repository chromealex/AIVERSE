#pragma once
// Data-driven region gestures shared by the client and editor. Only emits normal
// ability commands; selection/preview never mutates simulation state.
#include "modules/rts/RtsModule.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <map>
#include <functional>
#include <vector>
namespace aiverse::game {
class RegionSwipe {
public:
    struct Anchor { Entity entity; int region=-1; uint8_t owner=0; render::float3 point{}; int count=0,landCells=0; std::string infoKey; int ability[3]={-1,-1,-1}; };
    struct Arrow { render::float3 from,to; double until=0; };
    struct PreviewUnit {Entity entity;Fixed x,z;int region=-1,cell=-1,orderRegion=-1;bool groupEligible=true;};
    bool enabled=false, dragging=false;
    int ratio=50;
    std::map<int,Anchor> anchors;
    std::vector<int> sources;
    std::vector<Arrow> arrows;
    render::float3 cursor{};
    void configure(const Json& manifest) { config_=manifest["controls"]; enabled=config_.getStr("mode")=="regionSwipe"; }
    void refresh(Simulation& sim,RtsModule& module,uint8_t me) {
        me_=me; tick_=sim.tick(); grid_=&module.state(sim).grid; anchors.clear(); infoAnchors_.clear();units_.clear();singleUnits_.clear();cacheTick_=UINT32_MAX;
        rebuildRegions(module.state(sim).trig.vars);
        const auto& rules=module.rules(); auto& world=sim.world();
        const auto tag=rules.tagMask(config_.getStr("anchorTag"));
        std::map<int,int> anchorCounts;
        world.each<Unit,Owner,Transform>([&](Entity e,const Unit& u,const Owner& o,const Transform& t){
            if (!(rules.types[u.type].tags&tag)||u.hp<=Fixed{})return;
            int r=region(float(t.pos.x.toDouble()),float(t.pos.z.toDouble())); if(r<0)return;
            Anchor a; a.entity=e;a.owner=o.player;a.region=r;a.point={float(t.pos.x.toDouble()),0.2f,float(t.pos.z.toDouble())};
            for(int i=0;i<3;++i)for(size_t j=0;j<rules.types[u.type].abilities.size();++j)
                if(rules.types[u.type].abilities[j].id==config_["dispatchAbilities"][size_t(i)].str())a.ability[i]=int(j);
            for(const auto& info:config_["regionInfo"].items())
                if(rules.types[u.type].tags&rules.tagMask(info.getStr("tag"))){a.infoKey=info.getStr("key");break;}
            const int original=int(t.pos.z.toInt())*grid_->w+int(t.pos.x.toInt());
            if(original>=0&&size_t(original)<grid_->region.size()){
                const int region=grid_->region[size_t(original)];auto it=infoAnchors_.find(region);
                if(it==infoAnchors_.end()||(a.owner==me_&&it->second.owner!=me_))infoAnchors_[region]=a;
            }
            ++anchorCounts[r];
            // Several player-owned anchors may represent shares of one region.
            auto existing=anchors.find(r);
            if(existing==anchors.end()||(a.owner==me_&&existing->second.owner!=me_))anchors[r]=a;
        });
        for(size_t c=0;c<regions_.size()&&c<grid_->territory.size();++c){
            auto it=anchors.find(regions_[c]);if(it!=anchors.end()&&grid_->territory[c]==it->second.owner)++it->second.landCells;
        }
        const auto sharedInfo=config_.getStr("sharedRegionInfoKey");
        if(!sharedInfo.empty())for(auto& [r,a]:anchors)if(anchorCounts[r]>1)a.infoKey=sharedInfo;
        const auto units=rules.tagMask(config_.getStr("unitTag"));
        const auto singleTag=rules.tagMask(config_.getStr("singleUnitTag"));
        const auto groupTag=rules.tagMask(config_["regionGroups"].getStr("unitTag"));
        reserveCounts_.clear();reserveOrigins_.clear();
        if(scriptedGroups_) {
            const auto& vars=module.state(sim).trig.vars;
            const auto countPrefix=config_["regionGroups"].getStr("reservePrefix"),originPrefix=config_["regionGroups"].getStr("originPrefix");
            for(auto& [source,anchor]:anchors) {
                const auto member=vars.find(config_["regionGroups"].getStr("groupPrefix")+std::to_string(source));
                if(member==vars.end()||member->second.toInt()!=source+1)continue;
                auto count=vars.find(countPrefix+std::to_string(source));
                if(countPrefix.empty()||count==vars.end())continue;
                reserveCounts_[source]=int(std::clamp<int64_t>(count->second.toInt(),0,1000000));anchor.count+=reserveCounts_[source];
                for(int target:originalRegionKeys_) {
                    auto origin=vars.find(originPrefix+std::to_string(source)+"_"+std::to_string(target));
                    if(!originPrefix.empty()&&origin!=vars.end())reserveOrigins_[{source,target}]=int(origin->second.toInt()-1);
                }
            }
        }
        world.each<Unit,Owner,Transform>([&](Entity e,const Unit& u,const Owner& o,const Transform& t){
            if(!(rules.types[u.type].tags&(units|singleTag))||u.hp<=Fixed{})return;
            auto it=anchors.find(region(float(t.pos.x.toDouble()),float(t.pos.z.toDouble())));
            if((rules.types[u.type].tags&units)&&it!=anchors.end()&&it->second.owner==o.player)++it->second.count;
            int orderRegion=-1;const auto prefix=config_.getStr("orderTargetCellVariablePrefix");
            if(!prefix.empty()){const auto& vars=module.state(sim).trig.vars;auto it=vars.find(prefix+std::to_string(e.index));if(it!=vars.end()){auto c=it->second.toInt()-1;if(c>=0&&size_t(c)<regions_.size())orderRegion=scriptedGroups_?canonicalRegions_[size_t(c)]:regions_[size_t(c)];}}
            if(o.player==me_){
                PreviewUnit unit{e,t.pos.x,t.pos.z,region(float(t.pos.x.toDouble()),float(t.pos.z.toDouble())),int(t.pos.z.toInt())*grid_->w+int(t.pos.x.toInt()),orderRegion,!groupTag||(rules.types[u.type].tags&groupTag)!=0};
                if(rules.types[u.type].tags&units)units_.push_back(unit);
                if(rules.types[u.type].tags&singleTag)for(size_t i=0;i<rules.types[u.type].abilities.size();++i)
                    if(rules.types[u.type].abilities[i].id==config_.getStr("singleUnitAbility")){singleUnits_.push_back({unit,int(i),(rules.types[u.type].layer&kLayerAir)!=0});break;}
            }
        });
        if(single_.valid()&&!singleUnit())cancel();
    }
    int region(float x,float z) const {
        if(!grid_||x<0||z<0||x>=grid_->w||z>=grid_->h)return -1;
        auto c=size_t(int(z)*grid_->w+int(x));return c<regions_.size()?regions_[c]:-1;
    }
    void begin(render::float3 p) {
        cancel();cursor=p;gestureStart_=p;gestureStarts_.clear();
        const float radius=std::max(0.0f,float(config_.getFx("singleUnitRadius",Fixed::fromInt(1)).toDouble()));
        float distance=radius*radius;
        for(const auto& u:singleUnits_){
            if(u.unit.region<0&&!(u.air&&config_.getBool("singleUnitAirRoutes",false)))continue;
            const float dx=float(u.unit.x.toDouble())-p.x,dz=float(u.unit.z.toDouble())-p.z,d=dx*dx+dz*dz;
            if(d<distance||(d==distance&&(!single_.valid()||u.unit.entity.index<single_.index))){single_=u.unit.entity;distance=d;}
        }
        if(const auto* u=singleUnit()){dragging=true;sources.push_back(u->unit.region);return;}
        int r=region(p.x,p.z);auto it=anchors.find(r);
        if(it!=anchors.end()&&it->second.owner==me_){dragging=true;sources.push_back(r);gestureStarts_[r]=p;}
    }
    void move(render::float3 p) { cursor=p;if(!dragging||single_.valid())return;int r=region(p.x,p.z);auto it=anchors.find(r);if(it!=anchors.end()&&it->second.owner==me_&&std::find(sources.begin(),sources.end(),r)==sources.end()){sources.push_back(r);gestureStarts_[r]=p;} }
    std::string infoKey() const {
        if(!config_.getBool("mergeConnectedTerritory",false)){auto it=anchors.find(region(cursor.x,cursor.z));return it==anchors.end()?std::string{}:it->second.infoKey;}
        if(!grid_||cursor.x<0||cursor.z<0||cursor.x>=grid_->w||cursor.z>=grid_->h)return {};
        const int c=int(cursor.z)*grid_->w+int(cursor.x);auto it=infoAnchors_.find(grid_->region[size_t(c)]);
        return it==infoAnchors_.end()?std::string{}:it->second.infoKey;
    }
    void cancel(){dragging=false;sources.clear();single_={};cacheTick_=UINT32_MAX;}
    bool valid() const {
        int target=region(cursor.x,cursor.z);
        if(!dragging||!grid_||!anchors.count(target)||(!sources.empty()&&sources.front()==target))return false;
        if(const auto* u=singleUnit()){
            if(u->unit.orderRegion==previewRegion(cursor.x,cursor.z))return false;
            if(u->air&&config_.getBool("singleUnitAirRoutes",false))return true;
        }
        // Every source must be connected through owned regions to the destination.
        std::map<int,std::vector<int>> edges;
        for(int z=0;z<grid_->h;++z)for(int x=0;x<grid_->w;++x){int a=region(float(x),float(z));if(a<0)continue;
            for(auto [dx,dz]:{std::pair{1,0},std::pair{0,1}}){int b=region(float(x+dx),float(z+dz));if(b>=0&&a!=b){edges[a].push_back(b);edges[b].push_back(a);}}
        }
        std::vector<int> queue{target};std::map<int,bool> seen{{target,true}};
        for(size_t i=0;i<queue.size();++i)for(int b:edges[queue[i]])if(!seen[b]&&anchors.count(b)&&anchors.at(b).owner==me_){seen[b]=true;queue.push_back(b);}
        for(int r:sources)if(!seen[r]||(scriptedGroups_&&sourceAnchor(r).owner!=me_))return false;
        return !sources.empty();
    }
    const std::vector<PreviewUnit>& preview() const {
        const int target=previewRegion(cursor.x,cursor.z);
        if(cacheTick_==tick_&&cacheTarget_==target&&cacheRatio_==ratio&&cacheSources_==sources&&cacheDragging_==dragging)return preview_;
        cacheTick_=tick_;cacheTarget_=target;cacheRatio_=ratio;cacheSources_=sources;cacheDragging_=dragging;preview_.clear();previewCount_=0;
        if(!dragging||!grid_||!anchors.count(region(cursor.x,cursor.z))||(scriptedGroups_&&!valid()))return preview_;
        if(const auto* u=singleUnit()){
            if(valid()){preview_.push_back(u->unit);previewCount_=1;}
            return preview_;
        }
        const int total=grid_->w*grid_->h;std::vector<int> distance(size_t(total),-1),queue;
        std::vector<std::pair<Fixed,Fixed>> edges;
        auto neighbors=[&](int c,auto fn){const int x=c%grid_->w,z=c/grid_->w;for(auto [dx,dz]:{std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1}}){int nx=x+dx,nz=z+dz;if(nx>=0&&nz>=0&&nx<grid_->w&&nz<grid_->h)fn(nz*grid_->w+nx);}};
        auto owned=[&](int c){return size_t(c)<grid_->territory.size()&&grid_->territory[size_t(c)]==me_;};
        const auto& targetRegions=scriptedGroups_?canonicalRegions_:regions_;
        for(int c=0;c<total;++c)if(targetRegions[size_t(c)]==target)neighbors(c,[&](int n){
            if(targetRegions[size_t(n)]<0||targetRegions[size_t(n)]==target)return;
            edges.push_back({Fixed::fromRatio(c%grid_->w+n%grid_->w+1,2),Fixed::fromRatio(c/grid_->w+n/grid_->w+1,2)});
            if(owned(n)&&distance[size_t(n)]<0){distance[size_t(n)]=0;queue.push_back(n);}
        });
        if(scriptedGroups_) {
            // The game ranks equal path distances against the entire authored
            // boundary, including coast and map-edge segments.
            edges.clear();
            for(int c=0;c<total;++c)if(targetRegions[size_t(c)]==target) {
                int x=c%grid_->w,z=c/grid_->w;
                for(auto [dx,dz]:{std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1}}) {
                    int nx=x+dx,nz=z+dz;
                    if(nx>=0&&nz>=0&&nx<grid_->w&&nz<grid_->h&&targetRegions[size_t(nz*grid_->w+nx)]==target)continue;
                    edges.push_back({Fixed::fromRatio(2*x+1+dx,2),Fixed::fromRatio(2*z+1+dz,2)});
                }
            }
        }
        for(size_t i=0;i<queue.size();++i)neighbors(queue[i],[&](int n){if(regions_[size_t(n)]>=0&&owned(n)&&distance[size_t(n)]<0){distance[size_t(n)]=distance[size_t(queue[i])]+1;queue.push_back(n);}});
        struct Rank {PreviewUnit unit;int distance;Fixed near;int count=1;bool reserve=false;};
        for(int source:sources){if(source==region(cursor.x,cursor.z))continue;std::vector<Rank> ranked;
            const auto route=reserveOrigins_.find({source,target});
            if(reserveCounts_.contains(source)&&(route==reserveOrigins_.end()||route->second<0||route->second>=total))continue;
            const auto start=arrowStart(source);const int originalSource=previewRegion(start.x,start.z);
            auto nearDistance=[&](Fixed ux,Fixed uz){Fixed near=Fixed::fromInt(1000000);for(auto [x,z]:edges){auto dx=x-ux,dz=z-uz;near=std::min(near,dx*dx+dz*dz);}return near;};
            for(const auto& u:units_)if(u.region==source&&u.orderRegion!=target&&u.cell>=0&&u.cell<total&&distance[size_t(u.cell)]>=0&&(!scriptedGroups_||u.groupEligible||canonicalRegions_[size_t(u.cell)]==originalSource)){
                Fixed near=Fixed::fromInt(1000000);for(auto [x,z]:edges){auto dx=x-u.x,dz=z-u.z;near=std::min(near,dx*dx+dz*dz);}ranked.push_back({u,distance[size_t(u.cell)],near});
            }
            auto count=reserveCounts_.find(source);auto origin=reserveOrigins_.find({source,target});
            if(count!=reserveCounts_.end()&&count->second>0&&origin!=reserveOrigins_.end()) {
                int c=origin->second;
                if(c>=0&&c<total&&owned(c)&&distance[size_t(c)]>=0) {
                    PreviewUnit virtualUnit;virtualUnit.x=Fixed::fromRatio(2*(c%grid_->w)+1,2);virtualUnit.z=Fixed::fromRatio(2*(c/grid_->w)+1,2);
                    ranked.push_back({virtualUnit,distance[size_t(c)],nearDistance(virtualUnit.x,virtualUnit.z),count->second,true});
                }
            }
            std::sort(ranked.begin(),ranked.end(),[](const Rank&a,const Rank&b){if(a.distance!=b.distance)return a.distance<b.distance;if(a.near!=b.near)return a.near<b.near;if(a.reserve!=b.reserve)return a.reserve;return a.unit.entity.packed()<b.unit.entity.packed();});
            int n=0;for(const auto& r:ranked)n+=r.count;
            int take=n>1?std::min(n-1,std::max(1,n*ratio/100)):n;previewCount_+=take;
            for(const auto& r:ranked){int selected=std::min(take,r.count);if(selected&&!r.reserve)preview_.push_back(r.unit);take-=selected;if(!take)break;}
        }return preview_;
    }
    std::vector<PreviewUnit> accepted() const {
        std::vector<PreviewUnit> result;if(!dragging)return result;
        const int target=previewRegion(cursor.x,cursor.z);if(target<0)return result;
        for(const auto& u:units_)if(u.orderRegion==target)result.push_back(u);
        for(const auto& u:singleUnits_)if(u.unit.orderRegion==target&&std::none_of(result.begin(),result.end(),[&](const PreviewUnit& v){return v.entity==u.unit.entity;}))result.push_back(u.unit);
        return result;
    }
    int count() const {preview();return previewCount_;}
    int previewRatio() const {return single_.valid()?100:ratio;}
    bool favorable() const {auto it=anchors.find(region(cursor.x,cursor.z));if(it==anchors.end())return false;const double strength=config_.getFx(it->second.owner?"previewDefence":"previewNeutralStrength",Fixed::fromInt(1)).toDouble();return it->second.owner==me_||double(count())*count()>double(it->second.count)*it->second.count*strength;}
    template<class Issue> void cancelFlow(Simulation& sim,RtsModule& module,render::float3 point,Issue issue) {
        Entity best;float distance=1.4f*1.4f;const auto tag=module.rules().tagMask(config_.getStr("unitTag"));
        sim.world().each<Unit,Owner,Transform>([&](Entity e,const Unit& u,const Owner& o,const Transform& t){
            if(o.player!=me_||u.hp<=Fixed{}||!(module.rules().types[u.type].tags&tag))return;
            float dx=float(t.pos.x.toDouble())-point.x,dz=float(t.pos.z.toDouble())-point.z,d=dx*dx+dz*dz;
            if(d<distance){best=e;distance=d;}
        });
        if(!best.valid())return;
        for(const auto& [r,a]:anchors)if(a.owner==me_){
            const auto& abilities=module.rules().types[sim.world().get<Unit>(a.entity).type].abilities;
            for(size_t i=0;i<abilities.size();++i)if(abilities[i].id==config_.getStr("cancelAbility")){
                Command c;c.type=kCmdAbility;c.units={a.entity};c.target=best;c.param=int(i);c.point=sim.world().get<Transform>(best).pos;issue(c);return;
            }
        }
    }
    template<class Issue> void release(double time,Issue issue) {
        if(dragging&&valid()&&singleUnit()){
            const auto& u=*singleUnit();Command c;c.type=kCmdAbility;c.units={u.unit.entity};c.param=u.ability;
            c.point={Fixed::fromRatio(int64_t(cursor.x*256),256),Fixed{},Fixed::fromRatio(int64_t(cursor.z*256),256)};
            c.target=anchors.at(region(cursor.x,cursor.z)).entity;issue(c);
            arrows.push_back({gestureStart_,cursor,time+1.4});
        }else if(dragging&&valid())for(int r:sources){if(r==region(cursor.x,cursor.z))continue;auto a=sourceAnchor(r);int i=ratio==35?0:ratio==50?1:2;if(a.ability[i]<0)continue;
            Command c;c.type=kCmdAbility;c.units={a.entity};c.param=a.ability[i];c.point={Fixed::fromRatio(int64_t(cursor.x*256),256),Fixed{},Fixed::fromRatio(int64_t(cursor.z*256),256)};c.target=anchors.at(region(cursor.x,cursor.z)).entity;issue(c);arrows.push_back({arrowStart(r),cursor,time+1.4});}
        cancel();
    }
    render::float3 controlFlagPoint(const Anchor& a) const {
        if(!grid_||grid_->territory.empty())return a.point;
        const int original=int(a.point.z)*grid_->w+int(a.point.x);
        if(original>=0&&size_t(original)<grid_->territory.size()&&grid_->territory[size_t(original)]==a.owner)return a.point;
        // The building stays fixed; the control flag follows the surviving land.
        float sx=0,sz=0;int count=0;
        for(size_t c=0;c<regions_.size()&&c<grid_->territory.size();++c)
            if(regions_[c]==a.region&&grid_->territory[c]==a.owner){sx+=float(c%grid_->w)+.5f;sz+=float(c/grid_->w)+.5f;++count;}
        if(!count)return a.point;
        sx/=count;sz/=count;float distance=1e30f;auto point=a.point;
        for(size_t c=0;c<regions_.size()&&c<grid_->territory.size();++c)
            if(regions_[c]==a.region&&grid_->territory[c]==a.owner){
                float x=float(c%grid_->w)+.5f,z=float(c/grid_->w)+.5f,d=(x-sx)*(x-sx)+(z-sz)*(z-sz);
                if(d<distance){distance=d;point={x,a.point.y,z};}
            }
        return point;
    }
    // Control markers follow land ownership independently of the fixed building icon.
    std::vector<render::float2> controlFlags(const render::RTSCamera& camera,float w,float h) const {
        std::vector<render::float2> out;
        if(!config_.getBool("showControlFlags",false))return out;
        for(const auto& [region,a]:anchors)if(a.owner==me_&&me_!=0&&(grid_->territory.empty()||a.landCells>0)){
            const auto point=controlFlagPoint(a);const bool moved=point.x!=a.point.x||point.z!=a.point.z;
            render::float2 q;if(camera.worldToScreen(point,w,h,q)&&q.x>=0&&q.x<w&&q.y>=0&&q.y<h)out.push_back({q.x+(moved?0:18),q.y-(moved?0:12)});
        }
        return out;
    }
    struct Stroke {render::float2 a,b;float width=5,alpha=1,highlight=0;bool shadow=false;};
    std::vector<Stroke> strokes(const render::RTSCamera& camera,float w,float h,double time,
                               const std::function<float(float,float)>& groundHeight={}){
        arrows.erase(std::remove_if(arrows.begin(),arrows.end(),[&](const Arrow&a){return a.until<=time;}),arrows.end());
        std::vector<Stroke> out;
        if(w<=0||h<=0)return out;
        auto draw=arrows;
        if(dragging){
            if(singleUnit())draw.push_back({gestureStart_,cursor,time+1.4});
            else for(int r:sources)if(anchors.count(r))draw.push_back({arrowStart(r),cursor,time+1.4});
        }
        const auto vp=camera.proj(w/h,false)*camera.view();
        auto project=[&](render::float3 p,render::float2& result){
            auto c=vp*render::float4{p.x,p.y,p.z,1};if(c.w<=.0001f)return false;
            result={(c.x/c.w*.5f+.5f)*w,(1-(c.y/c.w*.5f+.5f))*h};return true;
        };
        for(size_t index=0;index<draw.size();++index){
            const auto& a=draw[index];
            const float dx=a.to.x-a.from.x,dz=a.to.z-a.from.z,distance=std::hypot(dx,dz);
            if(distance<.01f)continue;
            const bool preview=index>=arrows.size();
            const float age=preview?0:float(std::clamp(time-(a.until-1.4),0.0,1.4));
            const float fade=float(std::clamp((a.until-time)/.4,0.0,1.0));
            const float alpha=fade*fade*(3-2*fade);
            const float sweep=float(std::fmod(time*.65,1.0));
            constexpr int kSamples=48;
            render::float2 paths[2][kSamples+1];
            bool visible=true;
            // Both paths share ground coordinates. Only the arrow rises into a vertical arch.
            const float arch=std::clamp(distance*.22f,.4f,7.0f);
            for(int i=0;i<=kSamples;++i){
                const float t=float(i)/kSamples,bend=4*t*(1-t);
                const float x=a.from.x+dx*t-dz*.045f*bend,z=a.from.z+dz*t+dx*.045f*bend;
                const float ground=groundHeight?groundHeight(x,z):0;
                visible &= project({x,ground+.025f,z},paths[0][i]);
                visible &= project({x,ground+.3f+arch*bend,z},paths[1][i]);
            }
            if(!visible)continue;
            for(int layer=0;layer<2;++layer){
                const bool shadow=layer==0;
                auto* path=paths[layer];float arc[kSamples+1]{};
                for(int i=1;i<=kSamples;++i)arc[i]=arc[i-1]+std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y);
                const float length=arc[kSamples];if(length<16)continue;
                auto point=[&](float d){
                    d=std::clamp(d,0.0f,length);int i=1;
                    while(i<kSamples&&arc[i]<d)++i;
                    float t=(d-arc[i-1])/std::max(.001f,arc[i]-arc[i-1]);
                    return render::float2{path[i-1].x+(path[i].x-path[i-1].x)*t,path[i-1].y+(path[i].y-path[i-1].y)*t};
                };
                const float head=std::min(25.0f,length*.3f),shaft=length-head;
                const float width=std::min(9.0f,length*.12f),opacity=alpha*(shadow?.13f:1.0f);
                auto prev=path[0];
                for(int i=1;i<=kSamples;++i){
                    auto next=point(std::min(arc[i],shaft));
                    const float along=std::min(arc[i],shaft)/std::max(1.0f,shaft);
                    const float delta=along-sweep;
                    const float sheen=.12f+.22f*along+.25f*std::exp(-delta*delta/0.012f);
                    const float taper=.55f+.45f*std::min(1.0f,along*5);
                    out.push_back({prev,next,width*taper,opacity,sheen,shadow});prev=next;
                    if(arc[i]>=shaft)break;
                }
                auto end=point(shaft),tip=path[kSamples];
                float tx=tip.x-end.x,ty=tip.y-end.y,tangent=std::hypot(tx,ty);
                if(tangent<.001f)continue;
                tx/=tangent;ty/=tangent;
                for(int i=0;i<=16;++i){
                    float t=float(i)/16,r=head*.42f*(1-t);
                    render::float2 c{end.x+(tip.x-end.x)*t,end.y+(tip.y-end.y)*t};
                    out.push_back({{c.x-ty*r,c.y+tx*r},{c.x+ty*r,c.y-tx*r},tangent/16+(shadow?0:1),opacity,.34f,shadow});
                }
            }
            if(!preview&&age<.6f){
                const float progress=age/.6f,radius=7+18*progress;
                const auto q=paths[0][kSamples];
                for(int i=0;i<24;++i){
                    float a0=float(i)*6.2831853f/24,a1=float(i+1)*6.2831853f/24;
                    out.push_back({{q.x+std::cos(a0)*radius,q.y+std::sin(a0)*radius},
                                   {q.x+std::cos(a1)*radius,q.y+std::sin(a1)*radius},2,alpha*(1-progress)*.65f,.55f});
                }
            }
        }return out;
    }
    void fit(render::RTSCamera& camera,float width,float height,bool reset=false){
        if(!grid_||width<=0||height<=0)return;
        const float distance=std::max(float(grid_->h)*std::sin(camera.pitch),float(grid_->w)*height/width)*.56f/std::tan(camera.fovY*.5f);
        camera.maxDistance=distance;camera.minDistance=distance/3;
        if(reset){camera.distance=distance;camera.target={grid_->w*.5f,0,grid_->h*.5f};}
        camera.distance=std::clamp(camera.distance,camera.minDistance,camera.maxDistance);
    }
private:
    // Presentation domains join physically touching owner cells; the source map
    // remains immutable for gameplay/buildings. Neutral regions stay separate.
    std::vector<int> regions_;
    std::map<int,Anchor> infoAnchors_;
    std::map<int,render::float3> gestureStarts_;
    render::float3 gestureStart_{};
    render::float3 arrowStart(int region) const {
        auto it=gestureStarts_.find(region);
        return it==gestureStarts_.end()?anchors.at(region).point:it->second;
    }
    bool scriptedGroups_=false;
    std::vector<int> canonicalRegions_,originalRegionKeys_;
    std::map<int,int> reserveCounts_;
    std::map<std::pair<int,int>,int> reserveOrigins_;
    mutable int previewCount_=0;
    int previewRegion(float x,float z) const {
        const int r=region(x,z);if(r<0||!scriptedGroups_)return r;
        return canonicalRegions_[size_t(int(z)*grid_->w+int(x))];
    }
    const Anchor& sourceAnchor(int group) const {
        if(scriptedGroups_) {
            const auto p=arrowStart(group);const int c=int(p.z)*grid_->w+int(p.x);
            if(c>=0&&size_t(c)<grid_->region.size()) {
                auto it=infoAnchors_.find(grid_->region[size_t(c)]);
                if(it!=infoAnchors_.end())return it->second;
            }
        }
        return anchors.at(group);
    }
    void rebuildRegions(const std::map<std::string,Fixed>& vars){
        regions_.assign(grid_->region.begin(),grid_->region.end());
        scriptedGroups_=false;canonicalRegions_.clear();originalRegionKeys_.clear();
        const auto& groups=config_["regionGroups"];
        const auto enabled=vars.find(groups.getStr("enabledVar"));
        const auto prefix=groups.getStr("groupPrefix");
        if(!prefix.empty()&&enabled!=vars.end()&&enabled->second>Fixed{}) {
            scriptedGroups_=true;
            // Canonical region cells are stable independently of JSON region IDs.
            std::map<int,int> canonical;
            for(size_t c=0;c<grid_->region.size();++c)if(grid_->region[c]>=0)
                canonical.try_emplace(grid_->region[c],int(c));
            canonicalRegions_=regions_;
            for(const auto& [id,cell]:canonical)originalRegionKeys_.push_back(cell);
            for(size_t c=0;c<regions_.size();++c)if(regions_[c]>=0) {
                const int original=canonical.at(regions_[c]);regions_[c]=original;canonicalRegions_[c]=original;
                const auto it=vars.find(prefix+std::to_string(original));
                if(it==vars.end())continue;
                const auto target=it->second.toInt()-1;
                if(target<0||size_t(target)>=grid_->region.size()||grid_->region[size_t(target)]<0)continue;
                // Invalid/stale data must not combine different owners for input.
                if(grid_->territory.size()==regions_.size()&&grid_->territory[c]!=grid_->territory[size_t(target)])continue;
                regions_[c]=int(target);
            }
            return;
        }
        if(!config_.getBool("mergeConnectedTerritory",false)||grid_->territory.size()!=regions_.size())return;
        std::vector<bool> seen(regions_.size(),false);
        const int neutralOffset=int(regions_.size());
        for(size_t start=0;start<regions_.size();++start){
            if(seen[start]||grid_->region[start]<0)continue;
            const auto owner=grid_->territory[start];
            if(!owner){regions_[start]=neutralOffset+grid_->region[start];seen[start]=true;continue;}
            std::vector<int> queue{int(start)};seen[start]=true;
            for(size_t i=0;i<queue.size();++i){
                int c=queue[i],x=c%grid_->w,z=c/grid_->w;regions_[size_t(c)]=int(start);
                for(auto [dx,dz]:{std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1}}){
                    int nx=x+dx,nz=z+dz;if(nx<0||nz<0||nx>=grid_->w||nz>=grid_->h)continue;
                    const size_t n=size_t(nz*grid_->w+nx);
                    if(!seen[n]&&grid_->region[n]>=0&&grid_->territory[n]==owner){seen[n]=true;queue.push_back(int(n));}
                }
            }
        }
    }
    struct SingleUnit {PreviewUnit unit;int ability=-1;bool air=false;};
    Entity single_;
    std::vector<SingleUnit> singleUnits_;
    const SingleUnit* singleUnit() const {
        for(const auto& u:singleUnits_)if(u.unit.entity==single_)return &u;
        return nullptr;
    }
    std::vector<PreviewUnit> units_;
    uint32_t tick_=0;
    mutable uint32_t cacheTick_=UINT32_MAX;
    mutable int cacheTarget_=-1,cacheRatio_=0;
    mutable bool cacheDragging_=false;
    mutable std::vector<int> cacheSources_;
    mutable std::vector<PreviewUnit> preview_;
    Json config_; const MapGrid* grid_=nullptr;uint8_t me_=0;
};
}
