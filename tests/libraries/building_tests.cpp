// building-gen: openings clear for the agent, supported loot spots, walkable switchback stairs with headroom,
// determinism, quarter-turn yaw, invalid definitions and JSON errors.
#include "engine/systems/BuildingGen.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
bool overlap(const BuildingBox& a,const BuildingBox& b){return a.min.x<b.max.x&&b.min.x<a.max.x&&a.min.y<b.max.y&&b.min.y<a.max.y&&a.min.z<b.max.z&&b.min.z<a.max.z;}
bool clear(const BuildingLayout& l,const BuildingBox& q){for(const auto& b:l.boxes)if(overlap(b,q))return false;return true;}
BuildingBox agent(const BuildingDef& d,Vec3 p,Fixed half){auto e=Fixed::fromRatio(1,1000);return {{p.x-d.agentRadius,p.y+e,p.z-half},{p.x+d.agentRadius,p.y+d.agentHeight,p.z+half}};}
bool supported(const BuildingLayout& l,Vec3 p){for(const auto& b:l.boxes)if(b.max.y==p.y&&p.x>b.min.x&&p.x<b.max.x&&p.z>b.min.z&&p.z<b.max.z)return true;return false;}
Fixed volume(const BuildingLayout& l){Fixed v;for(const auto& b:l.boxes)v+=(b.max.x-b.min.x)*(b.max.y-b.min.y)*(b.max.z-b.min.z);return v;}
void check(const BuildingDef& d,uint64_t seed,Vec3 origin,uint32_t yaw){
    auto built=generateBuilding(d,seed,origin,yaw);if(!built)std::printf("no building: seed %llu %f x %f floors %u\n",(unsigned long long)seed,d.width.toDouble(),d.depth.toDouble(),d.floors);CHECK(built);auto l=*built;CHECK(l.stairs.size()==d.floors-1&&l.lootSpots.size()==d.floors*(d.partition?2u:1u));
    for(const auto& b:l.boxes)CHECK(b.min.x<b.max.x&&b.min.y<b.max.y&&b.min.z<b.max.z);
    CHECK(clear(l,agent(d,l.door,d.agentRadius)));CHECK(clear(l,agent(d,l.doorOutside,d.agentRadius)));
    for(auto p:l.lootSpots){CHECK(clear(l,agent(d,p,d.agentRadius)));CHECK(supported(l,p));}
    if(yaw)return;   // the stair walk below uses the unrotated axes (rotation is covered by the volume check)
    auto steps=uint32_t((d.floorHeight/d.maxStepRise).toInt()+1);auto rise=d.floorHeight/Fixed::fromInt(steps);CHECK(rise<=d.maxStepRise);
    for(const auto& [bottom,top]:l.stairs){
        CHECK(clear(l,agent(d,bottom,d.agentRadius)));CHECK(clear(l,agent(d,top,d.agentRadius)));CHECK(supported(l,top));
        auto dir=top.z>bottom.z?1:-1;
        for(uint32_t i=0;i<steps;++i){   // headroom above every step tread
            auto zc=(dir>0?bottom.z+d.agentRadius*2:bottom.z-d.agentRadius*2)+d.stairRun*Fixed::fromInt(int64_t(i)*dir)+d.stairRun/2*Fixed::fromInt(dir);
            Vec3 p{bottom.x,bottom.y+rise*Fixed::fromInt(i+1),zc};CHECK(supported(l,p));CHECK(clear(l,agent(d,p,d.stairRun/2-Fixed::fromRatio(1,100))));
        }
    }
}
}
int main(){
    try{
        BuildingDef d;check(d,1,{},0);
        for(uint64_t seed=1;seed<=40;++seed){BuildingDef t;t.floors=uint32_t(1+seed%4);t.width=Fixed::fromInt(int64_t(10+seed%9));t.depth=Fixed::fromInt(int64_t(9+seed%7));check(t,seed,{Fixed::fromInt(int64_t(seed)*50),Fixed::fromRatio(1,4),-30_fx},0);}
        BuildingDef big;big.floors=5;big.width=20_fx;big.depth=14_fx;big.partition=false;check(big,9,{},0);
        auto a=*generateBuilding(d,7,{},0),b=*generateBuilding(d,7,{},0);CHECK(a.boxes==b.boxes&&a.door==b.door);
        bool differs=false;for(uint64_t s=8;s<20;++s)differs|=generateBuilding(d,s,{},0)->door!=a.door;CHECK(differs);
        for(uint32_t yaw=1;yaw<4;++yaw){auto r=*generateBuilding(d,7,{100_fx,{},100_fx},yaw);CHECK(r.boxes.size()==a.boxes.size()&&fxAbs(volume(r)-volume(a))<Fixed::fromRatio(1,10));check(d,7,{100_fx,{},100_fx},yaw);}
        BuildingDef narrow;narrow.width=6_fx;narrow.depth=6_fx;CHECK(!generateBuilding(narrow,1,{},0));CHECK(!generateBuilding(d,1,{},4));
        auto j=readBuildingDef(Json::parse(R"({"width":16,"depth":12,"floors":3,"door":[1.5,2.4],"window":[1.0,1.1,1.0,3.5],"stair":[1.4,0.3,0.25]})"));
        CHECK(j.floors==3&&j.doorWidth==Fixed::fromRatio(3,2)&&j.windowSpacing==Fixed::fromRatio(7,2));check(j,3,{},0);
        auto fails=[](const char* t,const char* needle){try{readBuildingDef(Json::parse(t));}catch(const JsonError& e){return std::string(e.what()).find(needle)!=std::string::npos;}return false;};
        CHECK(fails(R"({"bogus":1})","building: unknown key 'bogus'"));CHECK(fails(R"({"floors":11})","building.floors: expected 1..10"));
        CHECK(fails(R"({"door":[1]})","building.door: expected 2 numbers"));CHECK(fails(R"({"width":7,"depth":6})","building: does not fit"));
        std::printf("building-gen: default house %zu boxes\n",a.boxes.size());
    }catch(const std::exception& e){std::fprintf(stderr,"building tests failed: %s\n",e.what());return 1;}
    std::puts("building-gen tests passed");return 0;
}
