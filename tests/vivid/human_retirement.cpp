// Whole-human retirement: model GPU resources must follow completed fence.
#include "engine/character/vivid/HumanRetirementQueue.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace aiverse;
using namespace aiverse::vivid;
void check(bool p,const char* msg){if(!p)throw std::runtime_error(msg);}
struct FenceDevice final:FakeDevice {
    std::uint64_t nextToken=1;
    gfx::ResourceRetirementStatus token=gfx::ResourceRetirementStatus::Pending;
    bool idleAllowed=true;
    int issued=0,completed=0,waits=0;
    std::uint64_t beginResourceRetirement() override {
        check(!inFrame,"retirement attempted before endFrame");
        ++issued;return nextToken++;
    }
    bool resourceRetirementComplete(std::uint64_t t)override {
        return t==1&&token==gfx::ResourceRetirementStatus::Complete;
    }
    gfx::ResourceRetirementStatus resourceRetirementStatus(std::uint64_t)override {
        return token;
    }
    void endResourceRetirement(std::uint64_t)override{++completed;}
    void abandonRetirementTokenAfterIdle(std::uint64_t)override{++completed;}
    bool waitForResourceIdle()override{++waits;return idleAllowed&&!inFrame;}
};
void run() {
    FenceDevice d;
    HumanRetirementQueue q(d);
    auto* retiring=q.enqueueEmpty();
    retiring->human.head=std::make_unique<render::Model>();
    gfx::BufferDesc desc;
    desc.type=gfx::BufferType::Vertex;
    desc.size=120;
    retiring->human.head->vb=d.createBuffer(desc);
    desc.type=gfx::BufferType::Index;
    desc.size=12;
    retiring->human.head->ib=d.createBuffer(desc);
    check(q.pending()==1&&d.buffers.size()==2,"retirement did not take ownership");
    d.beginFrame(16,16);
    // The old human can no longer be rendered, but its GPU handles stay
    // live until the frame is submitted and a later marker completes.
    d.endFrame();
    check(!q.collect()&&d.issued==1&&d.waits==0,
          "first GPU token stalled or completed before the backend");
    check(!q.collect()&&d.buffers.size()==2,
          "pending token destroyed old model buffers");
    d.token=gfx::ResourceRetirementStatus::Failed;
    check(!q.collect()&&d.buffers.size()==2,
          "failed token prematurely destroyed model buffers");
    d.idleAllowed=false;
    check(!q.drain()&&q.pending()==1&&d.buffers.size()==2,
          "failed idle barrier did not retain GPU owners");
    d.idleAllowed=true;
    check(q.drain()&&q.pending()==0&&d.buffers.empty(),
          "successful recovery drain failed to retire old model");
}

void backpressure() {
    FenceDevice d;
    HumanRetirementQueue q(d);
    q.setAdmission({2,4096});
    auto make=[&](std::uint32_t bytes) {
        auto* entry=q.enqueueEmpty();
        entry->human.head=std::make_unique<render::Model>();
        entry->human.head->vertices.resize(bytes/sizeof(render::ModelVertex));
        gfx::BufferDesc b{};
        b.type=gfx::BufferType::Vertex;
        b.size=entry->human.head->vertices.size()*sizeof(render::ModelVertex);
        entry->human.head->vb=d.createBuffer(b);
    };
    check(q.canPublish(120),"first retirement admission unexpectedly blocked");
    make(120);
    check(q.pending()==1&&q.canPublish(120),"second admission blocked too early");
    make(120);
    check(!q.canPublish(120)&&q.residency().blockedPublications==1,
          "pending count budget failed to block third publication");
    const auto before=d.buffers.size();
    d.beginFrame(16,16);d.endFrame();
    check(!q.collect() && d.buffers.size()==before &&
          !q.canPublish(120),"unresolved GPU token incorrectly released backpressure");
    d.token=gfx::ResourceRetirementStatus::Complete;
    check(q.collect() && q.pending()==0 && d.buffers.empty() &&
          q.canPublish(120),"completed token did not restore publication admission");

    HumanRetirementQueue memory(d);
    memory.setAdmission({8,100});
    auto* resident=memory.enqueueEmpty();
    resident->human.head=std::make_unique<render::Model>();
    resident->human.head->vertices.resize(3);
    gfx::BufferDesc b{};
    b.type=gfx::BufferType::Vertex;
    b.size=3*sizeof(render::ModelVertex);
    resident->human.head->vb=d.createBuffer(b);
    check(memory.residency().estimatedBytes>=3*sizeof(render::ModelVertex) &&
          !memory.canPublish(1),"estimated memory budget did not enforce limit");
    check(memory.drain() && d.buffers.empty(),
          "memory admission fixture did not release buffers");
}
}
int main(){
    try{run();backpressure();std::cout<<"VIVID whole-human retirement PASS\n";}
    catch(const std::exception& e){
        std::cerr<<"VIVID whole-human retirement FAIL: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
