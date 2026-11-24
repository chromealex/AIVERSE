#pragma once
// Procedural enterable buildings made of axis-aligned boxes, for any game whose collision is boxes (FPS
// module, AABB navmesh, top-down cover, voxel-free shooters): a rectangular footprint with 1..10 floors,
// outer walls with a door on a seeded side and windows per floor, an interior partition with a doorway,
// a switchback stairwell (two lanes: flights alternate +Z / -Z so no flight blocks the one below) with
// holes in the slabs above, a roof, loot spots in every
// room and the outside door point. Rise per step <= maxStepRise so a stepping motor walks up; every opening
// leaves room for an agent of the given size. Fixed math only; the same definition + seed gives the same
// boxes on every platform. Quarter-turn yaw keeps boxes axis-aligned.
#include "engine/core/Fixed.h"
#include "engine/core/Random.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::systems {
struct BuildingBox{Vec3 min,max;bool operator==(const BuildingBox&)const=default;};
struct BuildingDef{
    Fixed width=12_fx,depth=10_fx,floorHeight=Fixed::fromRatio(32,10),wall=Fixed::fromRatio(25,100),slab=Fixed::fromRatio(2,10);
    uint32_t floors=2;
    Fixed doorWidth=Fixed::fromRatio(16,10),doorHeight=Fixed::fromRatio(23,10);
    Fixed windowWidth=Fixed::fromRatio(12,10),windowHeight=Fixed::fromRatio(12,10),windowSill=Fixed::fromRatio(9,10),windowSpacing=4_fx;
    Fixed stairWidth=Fixed::fromRatio(14,10),stairRun=Fixed::fromRatio(3,10),maxStepRise=Fixed::fromRatio(25,100);
    bool partition=true,roof=true;
    Fixed agentRadius=Fixed::fromRatio(3,10),agentHeight=Fixed::fromRatio(18,10);
    bool operator==(const BuildingDef&)const=default;
};
struct BuildingLayout{
    std::vector<BuildingBox> boxes;std::vector<Vec3> lootSpots;Vec3 door{},doorOutside{};
    std::vector<std::pair<Vec3,Vec3>> stairs;   // bottom and top walking points of each flight
    std::vector<std::pair<Vec3,Vec3>> stairHalls;   // open floor beside each end of a flight, clear of the walls (nav goals)
    uint32_t doorSide=0;                        // 0 = -Z, 1 = +X, 2 = +Z, 3 = -X (before yaw)
};
namespace buildingDetail {
struct Opening{Fixed from,to,bottom,top;};
// A wall segment along one axis (length direction `along`, thickness across) split around its openings.
inline void wall(std::vector<BuildingBox>& out,bool alongX,Fixed start,Fixed end,Fixed across0,Fixed across1,Fixed y0,Fixed y1,std::vector<Opening> openings){
    std::sort(openings.begin(),openings.end(),[](const Opening& a,const Opening& b){return a.from<b.from;});
    auto box=[&](Fixed a0,Fixed a1,Fixed b0,Fixed b1){if(a1<=a0||b1<=b0)return;
        out.push_back(alongX?BuildingBox{{a0,b0,across0},{a1,b1,across1}}:BuildingBox{{across0,b0,a0},{across1,b1,a1}});};
    Fixed cursor=start;
    for(const auto& o:openings){box(cursor,o.from,y0,y1);box(o.from,o.to,y0,o.bottom);box(o.from,o.to,o.top,y1);cursor=o.to;}
    box(cursor,end,y0,y1);
}
// Openings spaced along a wall of length `len` (local 0..len), skipping [blockFrom, blockTo] (stairwell, door).
inline std::vector<Opening> windows(const BuildingDef& d,Fixed start,Fixed end,Fixed y,std::vector<std::pair<Fixed,Fixed>> blocked){
    std::vector<Opening> out;auto len=end-start;if(len<d.windowSpacing)return out;
    auto count=(len/d.windowSpacing).toInt();auto pitch=len/Fixed::fromInt(count);
    for(int64_t i=0;i<count;++i){
        auto c=start+pitch*Fixed::fromInt(i)+pitch/2;Opening o{c-d.windowWidth/2,c+d.windowWidth/2,y+d.windowSill,y+d.windowSill+d.windowHeight};
        bool ok=o.from>start+d.wall&&o.to<end-d.wall;
        for(auto [a,b]:blocked)ok&=o.to<=a-d.wall||o.from>=b+d.wall;
        if(ok)out.push_back(o);
    }
    return out;
}
inline Vec3 turn(Vec3 p,uint32_t yaw,Fixed w,Fixed dd){
    switch(yaw&3){case 1:return {dd-p.z,p.y,p.x};case 2:return {w-p.x,p.y,dd-p.z};case 3:return {p.z,p.y,w-p.x};default:return p;}
}
}
inline bool valid(const BuildingDef& d){
    auto in=[](Fixed v,Fixed lo,Fixed hi){return v>=lo&&v<=hi;};
    if(!in(d.width,6_fx,200_fx)||!in(d.depth,6_fx,200_fx)||d.floors<1||d.floors>10||!in(d.floorHeight,Fixed::fromRatio(5,2),8_fx)||!in(d.wall,Fixed::fromRatio(1,20),1_fx)
       ||!in(d.slab,Fixed::fromRatio(1,20),Fixed::fromRatio(1,2))||!in(d.maxStepRise,Fixed::fromRatio(1,20),1_fx)||!in(d.stairRun,Fixed::fromRatio(1,10),1_fx)
       ||!in(d.agentRadius,Fixed::fromRatio(1,10),1_fx)||!in(d.agentHeight,1_fx,3_fx))return false;
    auto agent=d.agentRadius*2+Fixed::fromRatio(1,10);
    if(d.doorWidth<agent||d.doorHeight<d.agentHeight+Fixed::fromRatio(1,10)||d.doorHeight>d.floorHeight-d.slab||d.stairWidth<agent)return false;
    if(d.windowWidth<=Fixed{}||d.windowHeight<=Fixed{}||d.windowSill+d.windowHeight>d.floorHeight-d.slab||d.windowSpacing<d.windowWidth+d.wall*2)return false;
    auto steps=((d.floorHeight)/d.maxStepRise).toInt()+1;auto run=d.stairRun*Fixed::fromInt(steps);
    auto landing=d.agentRadius*4+Fixed::fromRatio(1,2);   // free floor in front of and behind each flight (navmesh cells fit)
    if(run+landing*2+d.wall*2>d.depth||d.stairWidth*2+d.wall*2+agent>(d.partition?d.width/2:d.width))return false;
    return d.doorWidth+d.wall*4<fxMin(d.width,d.depth)/2;
}
// Boxes in world space: origin = the building's minimum corner after yaw (quarter turns about +Y).
inline std::optional<BuildingLayout> generateBuilding(const BuildingDef& d,uint64_t seed,Vec3 origin,uint32_t yaw=0){
    using namespace buildingDetail;
    if(!valid(d)||yaw>3)return {};
    Random g(seed,0xb11d);BuildingLayout L;std::vector<BuildingBox> b;
    auto W=d.width,D=d.depth,t=d.wall,H=d.floorHeight;
    L.doorSide=g.below(4);
    // Stairwell in the corner: lane 0 x in [t, t + w] climbs +Z (even floors), lane 1 next to it climbs -Z (odd).
    auto steps=uint32_t((H/d.maxStepRise).toInt()+1);auto rise=H/Fixed::fromInt(steps);auto run=d.stairRun*Fixed::fromInt(steps);
    Fixed sx0=t,sx1=t+d.stairWidth*2,sz0=t+d.agentRadius*4+Fixed::fromRatio(1,2),sz1=sz0+run;
    auto lane=[&](uint32_t f){Fixed x=t+d.stairWidth*Fixed::fromInt(f%2);return std::pair<Fixed,Fixed>{x,x+d.stairWidth};};
    // Door centre: seeded among positions 0.5 m apart that keep the doorway and an agent in front of it clear
    // of the corners, the stairwell (walls -Z and -X) and the partition (walls -Z and +Z).
    auto margin=d.doorWidth/2+t*2;auto agentFree=d.agentRadius*2+Fixed::fromRatio(1,10);
    std::vector<Fixed> spots;
    for(uint32_t turn=0;turn<4&&spots.empty();++turn){   // the seeded side first; a side with no room passes the door on
        if(turn)L.doorSide=(L.doorSide+1)%4;auto len=L.doorSide%2==0?W:D;
        for(Fixed c=margin;c<=len-margin;c+=Fixed::fromRatio(1,2)){
            auto lo=c-d.doorWidth/2-agentFree,hi=c+d.doorWidth/2+agentFree;bool ok=true;
            if(L.doorSide==0)ok&=lo>sx1;                       // stairwell spans x in [t, sx1] along the -Z wall
            if(L.doorSide==3)ok&=lo>sz1||hi<sz0-agentFree;     // ... and z in [sz0, sz1] along the -X wall
            if(d.partition&&L.doorSide%2==0)ok&=hi<W/2-t||lo>W/2+t;
            if(ok)spots.push_back(c);
        }
    }
    auto doorAt=spots[g.below(uint32_t(spots.size()))];
    for(uint32_t f=0;f<d.floors;++f){
        auto y=H*Fixed::fromInt(f);
        // Slab of this floor with the stairwell hole for the flight that arrives here.
        if(!f)b.push_back({{Fixed{},y,Fixed{}},{W,y+d.slab,D}});
        else{auto [h0,h1]=lane(f-1);b.push_back({{Fixed{},y,Fixed{}},{W,y+d.slab,sz0}});b.push_back({{Fixed{},y,sz1},{W,y+d.slab,D}});b.push_back({{h1,y,sz0},{W,y+d.slab,sz1}});b.push_back({{Fixed{},y,sz0},{h0,y+d.slab,sz1}});}
        auto y0=y+d.slab,y1=y+H;
        std::vector<std::pair<Fixed,Fixed>> stairZ{{sz0,sz1}};
        // Outer walls: -Z (0) and +Z (2) run along X; -X (3) and +X (1) along Z.
        for(uint32_t side=0;side<4;++side){
            bool alongX=side%2==0;auto len=alongX?W:D;std::vector<std::pair<Fixed,Fixed>> blocked;
            if(side==3)blocked=stairZ;
            if(side==0)blocked.push_back({sx0,sx1});
            std::vector<Opening> open=windows(d,Fixed{},len,y,blocked);
            if(!f&&side==L.doorSide){
                Opening door{doorAt-d.doorWidth/2,doorAt+d.doorWidth/2,y0,y0+d.doorHeight};
                std::vector<Opening> keep;for(auto o:open)if(o.to<=door.from-t||o.from>=door.to+t)keep.push_back(o);open=keep;open.push_back(door);
            }
            Fixed a0=side==0||side==3?Fixed{}:(alongX?D:W)-t,a1=a0+t;
            wall(b,alongX,Fixed{},len,a0,a1,y0,y1,open);
        }
        // Interior partition at x = W / 2 with a doorway at a seeded z clear of the outer walls.
        if(d.partition){
            auto room=D-t*4-d.doorWidth;auto dz=t*2+d.doorWidth/2+room*g.unit();
            wall(b,false,t,D-t,W/2-t/2,W/2+t/2,y0,y1,{Opening{dz-d.doorWidth/2,dz+d.doorWidth/2,y0,y0+d.doorHeight}});
        }
        // Flight from this floor to the next one.
        if(f+1<d.floors){
            auto [l0,l1]=lane(f);bool up=f%2==0;
            for(uint32_t i=0;i<steps;++i){auto z=up?sz0+d.stairRun*Fixed::fromInt(i):sz1-d.stairRun*Fixed::fromInt(i+1);b.push_back({{l0,y0,z},{l1,y0+rise*Fixed::fromInt(i+1),z+d.stairRun}});}
            auto cx=(l0+l1)/2;auto off=d.agentRadius*2;auto hx=sx1+d.agentRadius*3+Fixed::fromRatio(1,10);
            L.stairHalls.push_back(up?std::pair<Vec3,Vec3>{{hx,y0,sz0-off},{hx,y0+H,sz1+off}}:std::pair<Vec3,Vec3>{{hx,y0,sz1+off},{hx,y0+H,sz0-off}});
            L.stairs.push_back(up?std::pair<Vec3,Vec3>{{cx,y0,sz0-off},{cx,y0+H,sz1+off}}:std::pair<Vec3,Vec3>{{cx,y0,sz1+off},{cx,y0+H,sz0-off}});
        }
        // Loot spots: the middle of each room, clear of the stairwell.
        auto mid=(sz1+D)/2;
        if(d.partition){L.lootSpots.push_back({(W/2+sx1)/2+t,y0,mid});L.lootSpots.push_back({W*3/4,y0,D/2});}
        else{L.lootSpots.push_back({W/2+t,y0,D/2});}
    }
    if(d.roof){auto y=H*Fixed::fromInt(d.floors);b.push_back({{Fixed{},y,Fixed{}},{W,y+d.slab,D}});}
    // The door point on the threshold and one agent diameter outside.
    Vec3 door,outside;auto out=d.agentRadius*3;
    switch(L.doorSide){case 0:door={doorAt,d.slab,t/2};outside={doorAt,Fixed{},-out};break;case 1:door={W-t/2,d.slab,doorAt};outside={W+out,Fixed{},doorAt};break;
        case 2:door={doorAt,d.slab,D-t/2};outside={doorAt,Fixed{},D+out};break;default:door={t/2,d.slab,doorAt};outside={-out,Fixed{},doorAt};}
    auto place=[&](Vec3 p){return turn(p,yaw,W,D)+origin;};
    for(auto& x:b){auto p=place(x.min),q=place(x.max);L.boxes.push_back({{fxMin(p.x,q.x),fxMin(p.y,q.y),fxMin(p.z,q.z)},{fxMax(p.x,q.x),fxMax(p.y,q.y),fxMax(p.z,q.z)}});}
    for(auto& s:L.lootSpots)s=place(s);for(auto& s:L.stairs){s.first=place(s.first);s.second=place(s.second);}
    for(auto& s:L.stairHalls){s.first=place(s.first);s.second=place(s.second);}
    L.door=place(door);L.doorOutside=place(outside);
    return L;
}
// {"width":12,"depth":10,"floors":2,"floorHeight":3.2,"wall":0.25,"door":[1.4,2.3],"window":[1.2,1.2,0.9,4],
//  "stair":[1.4,0.3,0.25],"partition":true,"roof":true}  (metres; window = width, height, sill, spacing;
//  stair = width, run, max rise). Errors: JsonError("<path>.<key>: ...").
inline BuildingDef readBuildingDef(const Json& j,const std::string& path="building"){
    if(!j.isObject())throw JsonError(path+": expected object");
    static const char* names[]={"width","depth","floors","floorHeight","wall","slab","door","window","stair","partition","roof"};
    for(const auto& [k,v]:j.members()){(void)v;if(std::none_of(std::begin(names),std::end(names),[&](const char* n){return k==n;}))throw JsonError(path+": unknown key '"+k+"'");}
    BuildingDef d;
    auto num=[&](const Json& v,const std::string& p){bool ok=false;if(!v.isNumber())throw JsonError(p+": expected number");auto f=Fixed::parse(v.numberLiteral(),&ok);if(!ok)throw JsonError(p+": expected number");return f;};
    auto list=[&](const char* key,std::initializer_list<Fixed*> out){if(!j.has(key))return;const auto& a=j[key];if(!a.isArray()||a.size()!=out.size())throw JsonError(path+"."+key+": expected "+std::to_string(out.size())+" numbers");
        size_t i=0;for(auto* o:out){*o=num(a[i],path+"."+key+"["+std::to_string(i)+"]");++i;}};
    if(j.has("width"))d.width=num(j["width"],path+".width");if(j.has("depth"))d.depth=num(j["depth"],path+".depth");
    if(j.has("floorHeight"))d.floorHeight=num(j["floorHeight"],path+".floorHeight");if(j.has("wall"))d.wall=num(j["wall"],path+".wall");if(j.has("slab"))d.slab=num(j["slab"],path+".slab");
    if(j.has("floors")){auto f=num(j["floors"],path+".floors");if(f.raw%Fixed::kOne||f<1_fx||f>10_fx)throw JsonError(path+".floors: expected 1..10");d.floors=uint32_t(f.toInt());}
    list("door",{&d.doorWidth,&d.doorHeight});list("window",{&d.windowWidth,&d.windowHeight,&d.windowSill,&d.windowSpacing});list("stair",{&d.stairWidth,&d.stairRun,&d.maxStepRise});
    if(j.has("partition"))d.partition=j["partition"].boolean(true);if(j.has("roof"))d.roof=j["roof"].boolean(true);
    if(!valid(d))throw JsonError(path+": does not fit (footprint 6..200 m, floors 1..10, the stairwell, doors and windows must fit the walls and the agent)");
    return d;
}
}
