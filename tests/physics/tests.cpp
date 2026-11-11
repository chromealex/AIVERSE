#include "engine/physics/World.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace aiverse;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    require(argc==2,"scene path required");auto scene=Json::parseFile(argv[1]);
    physics::World w(scene), parallel(scene,4);
    for(int i=0;i<180;++i){w.step();parallel.step();require(w.trajectoryHash()==parallel.trajectoryHash(),"worker trajectories differ");}
    auto saved=w.save();auto atSave=w.trajectoryHash();
    for(int i=0;i<120;++i)w.step();auto expected=w.trajectoryHash();
    std::string error;auto restored=w.restore(saved,&error);require(restored,error.c_str());require(w.trajectoryHash()==atSave,"snapshot state differs");
    for(int i=0;i<120;++i)w.step();require(w.trajectoryHash()==expected,"snapshot continuation differs");
    auto bad=saved;bad.back()^=1;require(!w.restore(bad),"corruption accepted");require(w.trajectoryHash()==expected,"failed restore mutated world");
    require(!w.restore(saved.substr(0,40)),"truncation accepted");
    require(!w.impulse("ground",{1,0,0})&&!w.impulse("missing",{0,0,0}),"invalid impulse accepted");
    require(!w.impulse("ball",{std::numeric_limits<float>::quiet_NaN(),0,0}),"NaN impulse accepted");
    require(w.impulse("ball",{0,3000,0}),"impulse failed");
    auto y=b3Body_GetPosition(w.find("ball")->body).y;w.step();require(b3Body_GetPosition(w.find("ball")->body).y>y,"impulse did not lift sphere");
    for(int i=0;i<600;++i)w.step();
    for(auto& o:w.objects())if(o.dynamic){auto p=b3Body_GetPosition(o.body);require(std::isfinite(p.y)&&p.y>0,"body fell through ground");}
    auto a=b3Body_GetPosition(w.find("anchor")->body),b=b3Body_GetPosition(w.find("pendulum")->body);
    auto dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;require(std::abs(std::sqrt(dx*dx+dy*dy+dz*dz)-2.828427f)<.05f,"distance joint drift");
    auto hit=b3World_CastRayClosest(w.nativeId(),{10,10,7},{0,-20,0},b3DefaultQueryFilter());require(hit.hit,"ground ray missed");
    for(auto text:{R"({"tickRate":60.5,"bodies":[]})",R"({"bodies":[{"id":"a","size":[0,1,1]}]})",R"({"bodies":[{"id":"a"},{"id":"a"}]})",R"({"bodies":[],"joints":[{"a":"x","b":"y"}]})"}){
        bool rejected=false;try{physics::World invalid(Json::parse(text));}catch(const JsonError&){rejected=true;}require(rejected,"invalid scene accepted");
    }
    std::cout<<"physics: collision, joint, ray, impulse, worker replay, snapshot continuation and validation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"physics: "<<e.what()<<'\n';return 1;}}
