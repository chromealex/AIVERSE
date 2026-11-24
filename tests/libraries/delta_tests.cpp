// snapshot-delta: record diffs, XOR runs, sender/receiver over a lossy latest-only channel, malformed input.
#include "engine/net/SnapshotDelta.h"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace aiverse::lib::delta;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
struct Lcg{uint64_t s;uint32_t next(){s=s*6364136223846793005ull+1442695040888963407ull;return uint32_t(s>>33);}};
RecordSet world(Lcg& g,int actors){RecordSet r;r[0]=std::string(24,'g');for(int i=1;i<=actors;++i){std::string b(64,'\0');for(auto& c:b)c=char(g.next());r[uint32_t(1000+i)]=b;}return r;}
void move(RecordSet& r,Lcg& g){for(auto& [id,b]:r)if(id&&b.size()>40&&g.next()%3==0){b[4]=char(g.next());b[5]=char(g.next());b[40]=char(g.next());}}
void diffs(){
    Lcg g{1};auto a=world(g,50);auto b=a;
    auto same=encodeDelta(a,b);CHECK(same.size()==2);CHECK(*applyDelta(a,same)==b);
    move(b,g);b.erase(1003);b[5000]="new";auto d=encodeDelta(a,b);
    auto full=encodeDelta({},b);CHECK(d.size()*4<full.size());CHECK(*applyDelta(a,d)==b);CHECK(*applyDelta({},full)==b);
    RecordSet grow=a;grow[1001]+="xyz";CHECK(*applyDelta(a,encodeDelta(a,grow))==grow);   // size change: full record
    CHECK(!applyDelta({},d));                       // xor run against a missing record
    for(std::size_t n=0;n<d.size();++n)if(auto x=applyDelta(a,std::string_view(d).substr(0,n)))CHECK(x->size()<=b.size()+1);
    for(std::size_t i=0;i<d.size();++i){auto bad=d;bad[i]=char(bad[i]^0x5a);(void)applyDelta(a,bad);}   // must not crash
}
void channel(){
    DeltaSender send;DeltaReceiver recv;Lcg g{7},loss{3};auto w=world(g,80);
    std::size_t fullBytes=0,deltaBytes=0,decoded=0;
    for(uint32_t seq=1;seq<=600;++seq){
        move(w,g);if(seq%50==0)w.erase(w.rbegin()->first);if(seq%70==0)w[9000+seq]="spawn";
        auto packet=send.encode(seq,w);CHECK(!packet.empty());fullBytes+=encodeDelta({},w).size();deltaBytes+=packet.size();
        if(loss.next()%5==0)continue;                 // 20 % loss, acks below are lost too sometimes
        uint32_t got=0;auto out=recv.decode(packet,&got);CHECK(out&&*out==w&&got==seq);++decoded;
        if(loss.next()%4)send.acknowledge(got);
    }
    CHECK(decoded>400);CHECK(deltaBytes*2<fullBytes);
    CHECK(send.encode(600,w).empty());               // non-increasing sequence
    // a baseline the receiver dropped: decode fails; the sender recovers with a full packet after reset
    DeltaReceiver fresh;CHECK(!fresh.decode(send.encode(601,w)));send.reset();auto full=send.encode(602,w);CHECK(send.lastWasFull());CHECK(*fresh.decode(full)==w);
    CHECK(!fresh.decode(full));                      // replay of the same sequence
    std::printf("delta: %zu bytes as deltas vs %zu full (%.1f%%), %zu/600 decoded at 20%% loss\n",deltaBytes,fullBytes,100.0*double(deltaBytes)/double(fullBytes),decoded);
}
}
int main(){try{diffs();channel();}catch(const std::exception& e){std::fprintf(stderr,"delta tests failed: %s\n",e.what());return 1;}std::puts("snapshot-delta tests passed");return 0;}
