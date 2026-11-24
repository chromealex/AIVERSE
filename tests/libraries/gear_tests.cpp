// gear-tiers: absorption by zone, wear and breaking, equip / replace, capacity, ordering, JSON, persistence.
#include "engine/systems/Gear.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
    try{
        auto c=readGearCatalog(Json::parse(R"([{"id":1,"slot":0,"tier":1,"durability":80,"reduction":300,"zones":1},
            {"id":2,"slot":0,"tier":3,"durability":230,"reduction":550,"zones":1},{"id":3,"slot":1,"tier":2,"durability":150,"reduction":400,"zones":2},
            {"id":4,"slot":2,"tier":2,"durability":1,"capacity":6,"wear":0}])"));
        CHECK(c.size()==4&&valid(c));
        GearLoadout l;CHECK(equipGear(c,l,freshGear(c.at(1)))->def==0);CHECK(equipGear(c,l,freshGear(c.at(3)))->def==0);
        auto h=*absorbHit(c,l,1,100);CHECK(h.damage==70&&h.absorbed==30&&h.worn==30&&l.slots.at(0).durability==50);   // helmet only
        h=*absorbHit(c,l,2,100);CHECK(h.damage==60&&l.slots.at(1).durability==110);
        h=*absorbHit(c,l,4,100);CHECK(h.damage==100&&!h.absorbed);                                                     // legs: nothing covers
        h=*absorbHit(c,l,1,500);CHECK(h.absorbed==50&&h.damage==450&&h.broken==std::vector<uint32_t>{0}&&!l.slots.contains(0));   // capped by durability, breaks
        auto old=*equipGear(c,l,freshGear(c.at(2)));CHECK(old.def==0);
        auto lvl1=GearPiece{1,80};CHECK(!betterGear(c,lvl1,&l.slots.at(0)));GearPiece worn{2,10};CHECK(betterGear(c,GearPiece{2,230},&worn));CHECK(!betterGear(c,GearPiece{3,150},&worn));
        CHECK(equipGear(c,l,freshGear(c.at(4)))&&gearCapacity(c,l)==6);
        auto none=*absorbHit(c,l,1,0);CHECK(none.damage==0&&none.absorbed==0);CHECK(!absorbHit(c,l,1,-1));CHECK(!equipGear(c,l,GearPiece{9,1}));CHECK(!equipGear(c,l,GearPiece{1,81}));
        auto back=*equipGear(c,l,GearPiece{1,40});CHECK(back.def==2&&back.durability==230);
        std::string bytes;{Archive a(&bytes);auto copy=l;io(a,copy);}GearLoadout r;{Archive a(bytes);io(a,r);CHECK(a.ok()&&!a.remaining());}
        CHECK(r==l&&gearHash(r)==gearHash(l)&&valid(c,r));
        auto fails=[&](const char* text,const char* needle){try{readGearCatalog(Json::parse(text));}catch(const JsonError& e){return std::string(e.what()).find(needle)!=std::string::npos;}return false;};
        CHECK(fails(R"([{"id":1,"bogus":2}])","gear[0]: unknown key 'bogus'"));CHECK(fails(R"([{"id":1},{"id":1}])","gear[1].id: duplicate"));
        CHECK(fails(R"([{"id":1,"reduction":1200}])","gear[0].reduction: must be in 0..1000"));CHECK(fails(R"([{"slot":1}])","gear[0].id: required"));
    }catch(const std::exception& e){std::fprintf(stderr,"gear tests failed: %s\n",e.what());return 1;}
    std::puts("gear-tiers tests passed");return 0;
}
