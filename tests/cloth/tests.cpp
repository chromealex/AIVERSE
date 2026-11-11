#include "engine/cloth/Cloth.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace aiverse::cloth;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    Settings s;s.cells=16;auto grid=std::make_shared<Grid>(s);std::vector<Cloth> a,b;
    for(int i=0;i<16;++i){a.emplace_back(grid);b.emplace_back(grid);a.back().windScale=b.back().windScale=.5f+i*.1f;}
    Batch serial(1),parallel(4);Capsule sphere{{1.5f,-1.5f,.2f},{1.5f,-1.5f,.2f},.5f};Environment env;env.capsules={&sphere,1};
    for(int t=0;t<360;++t){env.wind.z=3+std::sin(t*.05f);serial.step(a,1.f/60,env);parallel.step(b,1.f/60,env);}
    for(size_t k=0;k<a.size();++k){require(a[k].healthy(),"nonfinite simulation");
        for(size_t i=0;i<a[k].positions().size();++i){auto p=a[k].positions()[i],q=b[k].positions()[i];require(p.x==q.x&&p.y==q.y&&p.z==q.z,"worker result mismatch");
            if(i<=size_t(s.cells))require(p.x==grid->rest[i].x&&p.y==0&&p.z==0,"pin moved");
            else {require(p.y>=env.floor+s.thickness-1e-5f,"floor penetration");require(aiverse::render::length(p-sphere.a)>=sphere.radius+s.thickness-1e-4f,"sphere penetration");}
        }
        require(a[k].maxStretch()<1.3f,"excessive stretch");
    }
    // A rest patch under zero forces must not gain energy.
    Cloth rest(grid);Environment still;still.gravity={};still.wind={};still.floor=-10;
    for(int i=0;i<120;++i)rest.step(1.f/60,still);
    for(size_t i=0;i<rest.positions().size();++i)require(aiverse::render::length(rest.positions()[i]-grid->rest[i])<1e-5f,"rest drift");
    Capsule capsule{{1.5f,-1.1f,.1f},{1.5f,-2.2f,.1f},.5f};Environment contact;contact.capsules={&capsule,1};Cloth drape(grid);
    for(int i=0;i<180;++i)drape.step(1.f/60,contact);
    for(size_t i=size_t(s.cells+1);i<drape.positions().size();++i){auto p=drape.positions()[i];
        Vec closest{capsule.a.x,std::clamp(p.y,capsule.b.y,capsule.a.y),capsule.a.z};
        require(aiverse::render::length(p-closest)>=capsule.radius+s.thickness-1e-4f,"capsule penetration");
    }
    auto old=a[0].positions()[40];a[0].active=false;serial.step(a,1.f/60,env);auto now=a[0].positions()[40];require(old.x==now.x&&old.y==now.y&&old.z==now.z,"inactive cloth moved");
    a[1].movePins({1,0,0});a[1].step(1.f/60,env);require(a[1].positions()[0].x==1,"pin motion missing");a[1].reset();require(a[1].positions()[0].x==0,"reset failed");
    a[1].step(std::numeric_limits<float>::quiet_NaN(),env);require(a[1].healthy(),"NaN dt corrupted state");
    // Sharing must match independent solves, including split, divergent history and reset/rejoin.
    std::vector<Cloth> reference,shared;
    for(int i=0;i<4;++i){reference.emplace_back(grid,Vec{float(i)*4,0,0});shared.emplace_back(grid,Vec{float(i)*4,0,0});}
    Batch unshared(1),sharing(4,true);std::vector<Environment> inputs(4);
    for(int t=0;t<90;++t){
        if(t==5)inputs[1].wind.x=4;
        if(t==6)inputs[1].wind.x=0; // identical forces again, but different state: must stay separate
        if(t==10||t==30){for(auto& c:reference)c.reset();for(auto& c:shared)c.reset();}
        if(t==20){reference[2].movePins({.1f,0,0});shared[2].movePins({.1f,0,0});}
        if(t==40)inputs[1].capsules={&capsule,1};
        if(t==50){reference[3].active=false;shared[3].active=false;}
        if(t==60){reference[3].active=true;shared[3].active=true;}
        unshared.step(reference,1.f/60,env,inputs);sharing.step(shared,1.f/60,env,inputs);
        if(t<5||t==10||t==30)require(sharing.stats().simulatedObjects==1&&sharing.stats().copiedObjects==3,"identical instances did not share");
        if(t==5||t==6||t==20||t==40)require(sharing.stats().simulatedObjects==2,"different state/input incorrectly shared");
        for(size_t i=0;i<shared.size();++i)for(size_t j=0;j<shared[i].positions().size();++j){auto p=shared[i].positions()[j],q=reference[i].positions()[j];require(p.x==q.x&&p.y==q.y&&p.z==q.z,"shared result differs from independent solve");}
    }
    bool rejected=false;try{s.cells=0;Grid invalid(s);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"invalid grid accepted");
    std::cout<<"cloth: pins, collisions, stability, worker equality, shared groups/splits, freeze, reset and validation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
