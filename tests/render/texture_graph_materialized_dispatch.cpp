#include "engine/render/graph/TextureGraphMaterializedDispatch.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace aiverse::render::texture_graph::gpu;
struct Handle {int id=0;bool valid()const{return id!=0;}bool operator==(const Handle&)const=default;};
struct Device {
    bool healthy=true,beginOk=true,dispatchOk=true,metalOk=true,throwBind=false,active=false;
    int begins=0,ends=0,dispatches=0;uint32_t count=0;
    std::vector<std::pair<int,int>> bindings;
    bool gpuHealthy()const{return healthy;}
    bool beginCompute(Handle){++begins;active=beginOk;return active;}
    void setComputeBuffer(int slot,Handle h){if(throwBind)throw std::runtime_error("bind");bindings.emplace_back(slot,h.id);}
    bool dispatchChecked(uint32_t n){++dispatches;count=n;return dispatchOk;}
    void endCompute(){++ends;active=false;}
    bool computeDispatchSucceeded()const{return metalOk;}
};
static void check(bool ok){if(!ok)std::abort();}
int main(){
    Device d;const Handle p{1},out{10};const std::array<Handle,3> in{{{20},{30},{20}}};
    const auto inputs=std::span<const Handle>(in);
    auto call=[&](bool metal=true){return dispatchMaterializedPass(d,p,out,inputs,64,metal);};
    check(call()&&d.begins==1&&d.ends==1&&d.dispatches==1&&d.count==64&&!d.active);
    check((d.bindings==std::vector<std::pair<int,int>>{{0,10},{1,20},{2,30},{3,20}}));
    const std::array<Handle,1> alias{{out}},invalid{{{}}};
    check(!dispatchMaterializedPass(d,p,out,std::span<const Handle>(alias),64,true));
    check(!dispatchMaterializedPass(d,p,out,std::span<const Handle>(invalid),64,true));
    check(!dispatchMaterializedPass(d,Handle{},out,inputs,64,true));
    check(!dispatchMaterializedPass(d,p,out,inputs,0,true)&&d.begins==1);
    d.beginOk=false;check(!call()&&d.ends==1);d.beginOk=true;
    d.dispatchOk=false;check(!call()&&!d.active&&d.ends==2);d.dispatchOk=true;
    d.metalOk=false;check(!call()&&call(false)&&d.ends==4);
    d.throwBind=true;bool threw=false;try{(void)call();}catch(const std::runtime_error&){threw=true;}
    check(threw&&!d.active&&d.ends==5);
    d.healthy=false;check(!call()&&d.ends==5);
    std::cout<<"PASS: ordered multi-input bindings, duplicates, alias/invalid refusal, encode failure, Metal status, exception cleanup\n";
}
