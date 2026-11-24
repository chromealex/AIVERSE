// Voxel sandbox module + session tests (CTest: voxel_game): determinism, walking, digging and building,
// saves, night mobs, and the authoritative session over a loopback connection.
#include "engine/game/GameCore.h"
#include "engine/voxel/VoxelSession.h"
#include "engine/voxel/Project.h"
#include <aiverselib/systems/voxellight/VoxelLight.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::voxelgame;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
std::string root;
Project load(){return readProject([](const std::string& f,std::string& t){std::ifstream in(std::string(AIVERSE_TEMPLATES_DIR)+"/voxel_sandbox/"+f);if(!in)return false;t.assign(std::istreambuf_iterator<char>(in),{});return true;});}
std::unique_ptr<Simulation> make(const Project& p,uint64_t seed=1){auto sim=std::make_unique<Simulation>(SimConfig{seed,0,0},std::make_unique<game::GameCore>());sim->setup(p.rules,p.map);return sim;}
VoxelHost& mod(Simulation& s){return host(s);}
void send(Simulation& sim,uint8_t slot,Input in){static uint32_t seq=0;in.sequence=++seq;auto c=command(slot,in);c.tick=sim.tick();sim.inject(c);}
void basics(){
    auto p=load();auto a=make(p),b=make(p);CHECK(a->checksum()==b->checksum());
    auto& st=mod(*a).state(*a);CHECK(st.players.size()==8&&st.world.chunks().size()>=12*60);
    auto spawn=st.spawn;CHECK(!mod(*a).content().blocks[st.world.blockOr({8,int32_t(spawn.y.floorInt())-1,8},kAir)].replaceable);
    // Walk forward for 3 seconds: the player moves and stays on the ground (step-up over slopes).
    for(int i=0;i<60;++i){Input in;in.forward=1000;in.yaw=Fixed::fromRatio(1,8);in.sequence=uint32_t(i+1);for(auto* sim:{a.get(),b.get()}){auto c=command(1,in);c.tick=sim->tick();sim->inject(c);sim->step();}}
    auto& pl=st.players[0];CHECK(pl.active&&(pl.pos-spawn).length()>4_fx);CHECK(pl.grounded||pl.inFluid);
    CHECK(a->checksum()==b->checksum());
}
void digAndBuild(){
    auto p=load();auto sim=make(p);auto& s=mod(*sim).state(*sim);const auto& r=mod(*sim).content().blocks;
    // Look straight down and dig the block under the feet.
    auto& pl=s.players[0];auto below=BlockPos{int32_t(pl.pos.x.floorInt()),int32_t(pl.pos.y.floorInt())-1,int32_t(pl.pos.z.floorInt())};
    auto before=s.world.blockOr(below,kAir);CHECK(before!=kAir);
    Input dig;dig.pitch=-Fixed::fromRatio(24,100);dig.buttons=Break;
    for(int i=0;i<int(r[before].breakTicks)+3;++i){send(*sim,1,dig);sim->step();}
    CHECK(s.world.blockOr(below,kAir)==kAir||s.world.blockOr(below,kAir)!=before);
    bool got=false;auto drop=r[before].dropsSelf?before:r[before].drop;for(auto& stck:pl.hotbar)got|=stck.block==drop&&stck.count>=1;CHECK(got||drop==kAir);
    // Place a planks block from slot 1 in front.
    // Stand on flat ground next to the hole (test setup), then build in front.
    {int32_t x=int32_t(pl.pos.x.floorInt())+4,z=int32_t(pl.pos.z.floorInt());int32_t y=120;while(y>0&&!r[s.world.blockOr({x,y,z},kAir)].solid)--y;
     pl.pos={Fixed::fromInt(x)+Fixed::fromRatio(1,2),Fixed::fromInt(y+1),Fixed::fromInt(z)+Fixed::fromRatio(1,2)};pl.vel={};}
    for(int i=0;i<10;++i){Input idle;send(*sim,1,idle);sim->step();}
    auto planks=*r.find("planks");auto count=pl.hotbar[0].count;CHECK(pl.hotbar[0].block==planks);
    Input place;place.pitch=-Fixed::fromRatio(15,100);place.buttons=Place;place.slot=1;send(*sim,1,place);sim->step();
    CHECK(pl.hotbar[0].count==count-1);
    // A torch lights its surroundings.
    auto t=target(pl,s,mod(*sim).config(),r);CHECK(t.block);
    Input torch;torch.pitch=place.pitch;torch.slot=4;send(*sim,1,torch);sim->step();torch.buttons=Place;send(*sim,1,torch);sim->step();
    bool lit=false;for(int dx=-2;dx<=2;++dx)for(int dz=-2;dz<=2;++dz)for(int dy=-2;dy<=2;++dy)lit|=blockLight(s.world.light(offset(t.before,dx,dy,dz)))>=12;CHECK(lit);
}
void saves(){
    auto p=load();auto a=make(p);
    for(int i=0;i<40;++i){Input in;in.forward=1000;in.yaw=Fixed::fromRatio(3,8);in.pitch=-Fixed::fromRatio(2,10);in.buttons=(i>20?uint32_t(Break):0u)|(i%7==0?uint32_t(Jump):0u);send(*a,1,in);a->step();}
    auto bytes=a->serialize();CHECK(!bytes.empty());
    auto b=make(p);CHECK(b->deserialize(bytes));CHECK(a->tick()==b->tick());CHECK(a->checksum()==b->checksum());
    for(int i=0;i<80;++i){Input in;in.forward=700;in.right=300;in.yaw=Fixed::fromRatio(5,8);in.buttons=(i%9==0?uint32_t(Jump):0u)|(i>40?uint32_t(Place):0u);in.slot=uint8_t(i>40?2:0);
        auto c=command(1,in);c.seq=10000+uint32_t(i);c.args[6]=10000+i;c.tick=a->tick();a->inject(c);c.tick=b->tick();b->inject(c);a->step();b->step();}
    CHECK(a->checksum()==b->checksum());
    auto broken=bytes;broken.resize(broken.size()/2);auto c=make(p);CHECK(!c->deserialize(broken));
}
void night(){
    auto p=load();p.rules.member("voxelGame").set("dayTicks",Json::number(int64_t(400)));p.rules.member("voxelGame").set("spawnEvery",Json::number(int64_t(5)));
    auto sim=make(p);auto& s=mod(*sim).state(*sim);
    CHECK(skyDarkening(0,400)==0&&skyDarkening(300,400)==11);
    size_t most=0;for(int i=0;i<380;++i){Input idle;send(*sim,1,idle);sim->step();most=std::max(most,s.mobs.size());}
    CHECK(most>0); // mobs appear at night near the player
    std::cout<<"mobs at night: "<<most<<", player health "<<s.players[0].health<<"\n";
}
void session(){
    auto p=load();Server server(p.rules,p.map,1);auto pair=net::makeLoopbackPair();CHECK(server.attach(1,std::move(pair.first)));
    Client client(p.rules,p.map,std::move(pair.second));uint64_t now=1000;
    for(int i=0;i<200;++i){
        client.poll(now);Input in;in.forward=i>100?1000:0;in.yaw=Fixed::fromRatio(1,4);if(i==150)in.buttons=Jump;client.submit(in);
        server.poll(now);server.step();client.poll(now);now+=50;}
    CHECK(client.ready()&&client.slot()==1);CHECK(client.receivedChunks()>200);
    auto& s=server.module().state(server.simulation());
    size_t same=0,checked=0;for(const auto& [c,chunk]:client.world().chunks()){auto* real=s.world.chunk(c);if(!real)continue;++checked;same+=real->blocks==chunk.blocks;}
    CHECK(checked>200&&same==checked);
    // Prediction matches the authority within a block after the inputs are acknowledged.
    CHECK((client.predicted().pos-s.players[0].pos).length()<1_fx);
    // Edits on the server reach the mirror.
    const auto& r=server.module().content().blocks;auto at=BlockPos{int32_t(s.spawn.x.floorInt()),int32_t(s.spawn.y.floorInt())+3,int32_t(s.spawn.z.floorInt())+2};
    auto old=s.world.set(at,*r.find("glass"),0,r);CHECK(old);
    for(int i=0;i<3;++i){server.poll(now);server.step();client.poll(now);now+=50;}
    CHECK(client.world().blockOr(at,kAir)==*r.find("glass"));
    std::cout<<"session: chunks "<<client.receivedChunks()<<", sent "<<server.sentBytes()/1024<<" KiB\n";
    // Wrong content is rejected.
    auto other=p.rules;other.member("voxelGen").set("seed",Json::number(int64_t(1)));auto pair2=net::makeLoopbackPair();CHECK(server.attach(2,std::move(pair2.first)));
    Client wrong(other,p.map,std::move(pair2.second));for(int i=0;i<5;++i){wrong.poll(now);server.poll(now);now+=50;}CHECK(!wrong.ready());
}
}
int main(int argc,char** argv){
    root=argc>1?argv[1]:".";
    try{basics();digAndBuild();saves();night();session();}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    std::cout<<"voxel game tests passed\n";return 0;
}
