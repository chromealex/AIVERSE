#include "engine/world/ChunkScene.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::world;
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x); } while(0)

void scheduler() {
    ChunkStreamer stream({20,2,3});
    for(int x=-4;x<=4;++x)CHECK(stream.add({x,0,0},10));
    CHECK(!stream.add({0,0,0},10));CHECK(!stream.add({9,0,0},0));
    std::vector<Cell> loaded;std::set<Cell> owned;size_t peak=0;
    auto load=[&](Cell cell){loaded.push_back(cell);owned.insert(cell);peak=std::max(peak,owned.size());return true;};
    auto unload=[&](Cell cell){CHECK(owned.erase(cell)==1);};
    StreamObserver origin{{0,0,0},4};
    auto status=stream.update({&origin,1},{},1,load,unload);
    CHECK(loaded.front()==Cell{});CHECK(status.resident==1&&status.pending==1&&status.budgetBlocked==7);
    status=stream.update({&origin,1},{},1,load,unload);
    CHECK(status.resident==2&&status.reservedBytes==20&&!status.ready());
    Cell pin{4,0,0};status=stream.update({&origin,1},{&pin,1},1,load,unload);
    CHECK(stream.loaded().contains(pin)&&stream.loaded().contains(Cell{}));CHECK(peak==2);
    StreamObserver far{{-4,0,0},0};status=stream.update({&far,1},{},1,load,unload);
    CHECK(status.ready()&&owned.size()==1&&owned.contains(far.cell));
    status=stream.update({},{},0,load,unload);CHECK(status.ready()&&owned.empty()&&status.reservedBytes==0);
    Cell missing{99,0,0};status=stream.update({},{&missing,1},1,load,unload);CHECK(!status.ready());
}
void oversubscribed() {
    ChunkStreamer stream({20,1,1});
    stream.add({0,0,0},30);stream.add({1,0,0},10);stream.add({2,0,0},10);
    StreamObserver observer{{},2};int loads=0;
    auto load=[&](Cell){++loads;return true;};auto unload=[](Cell){};
    auto status=stream.update({&observer,1},{},10,load,unload);
    CHECK(loads==1&&status.budgetBlocked==2&&status.reservedBytes==10);
    Cell pins[]={{1,0,0},{2,0,0}};
    status=stream.update({},pins,10,load,unload);
    CHECK(!status.ready()&&status.resident==1&&status.budgetBlocked==1);
    ChunkStreamer zero({0,0,1});zero.add({},1);
    status=zero.update({&observer,1},{},10,load,unload);
    CHECK(status.budgetBlocked==1&&status.resident==0&&loads==1);
}
void failures() {
    ChunkStreamer stream({100,10,3});stream.add({0,0,0},10);stream.add({1,0,0},10);
    StreamObserver origin{{},1};int attempts=0;bool repaired=false;
    auto load=[&](Cell c){++attempts;return c.x!=0||repaired;};auto unload=[](Cell){};
    auto s=stream.update({&origin,1},{},1,load,unload);CHECK(s.failed==1&&s.resident==0);
    s=stream.update({&origin,1},{},1,load,unload);CHECK(s.resident==1&&attempts==2&&!s.ready());
    s=stream.update({&origin,1},{},1,load,unload);CHECK(attempts==2);
    repaired=true;s=stream.update({&origin,1},{},1,load,unload);CHECK(s.ready()&&s.failed==0&&attempts==3);
}
void multipleObservers() {
    ChunkStreamer stream({30,3,1});
    for(int x:{-100000,0,100000})stream.add({x,0,0},10);
    StreamObserver observers[]={{{-100000,0,0},0},{{100000,0,0},0},{{-100000,0,0},0}};
    int loads=0;auto s=stream.update(observers,{},10,[&](Cell){++loads;return true;},[](Cell){});
    CHECK(s.ready()&&s.wanted==2&&loads==2);
    StreamObserver extremes[]={{{std::numeric_limits<int32_t>::min(),0,0},4},{{std::numeric_limits<int32_t>::max(),0,0},4}};
    s=stream.update(extremes,{},10,[](Cell){return true;},[](Cell){});CHECK(s.resident==0);
}
void scene() {
    ChunkScene scene;
    scene.configure(Json::parse(R"({"cellSize":4096,"coordinates":"local","maxResidentBytes":96,"maxResidentChunks":2,"retryUpdates":1,"chunks":[{"cell":[100000,0,0],"file":"far.json","maxBoxes":1},{"cell":[-1,0,0],"file":"near.json","maxBoxes":1}]})"));
    auto read=[](const std::string&,std::string& bytes){bytes=R"({"boxes":[{"min":[0,0,0],"max":[1,1,1]}]})";return true;};
    Vec3 observers[]={{Fixed::fromInt(409600000),{},{}},{Fixed::fromRaw(-1),{},{}}};
    scene.update(observers,0,{},2,read);CHECK(scene.status().ready()&&scene.residentCount()==2);
    CHECK(scene.chunks().at({100000,0,0})[0].min.x==Fixed::fromInt(409600000));
    CHECK(scene.chunks().at({-1,0,0})[0].min.x== -4096_fx);
    CHECK(relativeTo(scene.chunks().at({100000,0,0})[0].max,{100000,0,0},4096).x==1_fx);
    bool rejected=false;try{scene.configure(Json::parse(R"({"cellSize":16.5})"));}catch(const JsonError&){rejected=true;}
    CHECK(rejected&&scene.residentCount()==2);
    scene.update(std::span<const Vec3>{},0,{},0,read);CHECK(scene.residentCount()==0);
    auto invalid=[](const std::string&,std::string& bytes){bytes=R"({"boxes":[{"min":[0,0,0],"max":[1,1,1]},{"min":[2,0,0],"max":[3,1,1]}]})";return true;};
    scene.update(observers,0,{},2,invalid);CHECK(scene.status().failed==2&&scene.residentCount()==0&&!scene.error().empty());
    scene.update(observers,0,{},2,read);CHECK(scene.status().ready()&&scene.error().empty());
    // Legacy absolute box coordinates are preserved.
    scene.configure(Json::parse(R"({"chunks":[{"cell":[0,0,0],"file":"old.json"}]})"));
    scene.update(Vec3{},0,1,read);CHECK(scene.chunks().begin()->second[0].max.x==1_fx);
}
void traversal() {
    ChunkStreamer stream({80,8,2});
    for(int x=-10000;x<=10000;++x)CHECK(stream.add({x,0,0},10));
    std::set<Cell> memory;size_t loads=0;
    for(int x=-10000;x<=10000;x+=5) {
        StreamObserver observer{{x,0,0},2};
        auto s=stream.update({&observer,1},{},5,[&](Cell c){memory.insert(c);++loads;return true;},[&](Cell c){memory.erase(c);});
        CHECK(s.ready()&&s.reservedBytes<=80&&memory.size()<=8);
    }
    CHECK(loads==20001);
}
int main() {
    try {scheduler();oversubscribed();failures();multipleObservers();scene();traversal();std::puts("World streaming: budgets, priorities, failures, local coordinates and 20001-cell traversal passed");}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
