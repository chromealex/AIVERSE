#include "modules/genres/BoardModule.h"
#include "modules/genres/MotionModule.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::genres;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);} while(0)
namespace {
std::unique_ptr<Simulation> create(const Json& rules,const Json& map) {
    auto sim=std::make_unique<Simulation>(SimConfig{17,20,0},std::make_unique<BoardModule>());
    sim->setup(rules,map);return sim;
}
BoardState& state(Simulation& sim){return *sim.world().resource<BoardState>();}
void continuation(const Json& rules,const Json& map) {
    auto a=create(rules,map),b=create(rules,map);
    for(int t=0;t<21;++t){a->step();b->step();CHECK(a->checksum()==b->checksum());}
    auto bytes=a->serialize();CHECK(!bytes.empty());auto restored=create(rules,map);std::string error;
    CHECK(restored->deserialize(bytes,&error));CHECK(a->checksum()==restored->checksum());
    SimSnapshot snapshot;a->saveState(snapshot);auto before=a->checksum();a->step();a->loadState(snapshot);CHECK(a->checksum()==before);
    for(int t=0;t<3000&&!a->result();++t){a->step();restored->step();CHECK(a->checksum()==restored->checksum());}
    CHECK(a->result()!=0);
}
void cardRules(Json rules,Json map) {
    auto sim=create(rules,map);auto before=sim->checksum();
    Command c;c.type=PlayCard;c.player=2;sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.player=1;c.param=-1;sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.param=1000;sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.param=0;c.player=0;sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.player=1;auto& p=state(*sim).players[0];p.energy=0;before=sim->checksum();sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    p.energy=3;auto count=p.hand.size();auto enemyHealth=state(*sim).players[1].health;
    sim->module().applyCommand(*sim,c);CHECK(p.hand.size()==count-1);CHECK(p.energy==2);CHECK(state(*sim).players[1].health<enemyHealth);
    c.type=EndTurn;sim->module().applyCommand(*sim,c);CHECK(state(*sim).active==1);CHECK(p.hand.empty());
    map.member("decks").itemsMut()[0].itemsMut()[0]=Json::string("missing");bool rejected=false;
    try{create(rules,map);}catch(const JsonError&){rejected=true;}CHECK(rejected);
}
void puzzleRules(Json rules,Json map) {
    auto sim=create(rules,map);auto before=sim->checksum();
    Command c;c.player=1;c.type=SwapCells;c.param=0;c.point.x=Fixed::fromInt(8);
    sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.point.x=Fixed::fromInt(1);sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.param=1;c.point.x=Fixed::fromInt(4);c.player=2;sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    c.player=1;sim->module().applyCommand(*sim,c);CHECK(state(*sim).resolving);CHECK(state(*sim).moves==9);
    before=sim->checksum();sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==before);
    auto bytes=sim->serialize();auto restored=create(rules,map);CHECK(restored->deserialize(bytes));
    for(int i=0;i<30;++i){sim->step();restored->step();CHECK(sim->checksum()==restored->checksum());}
    CHECK(state(*sim).score>=30);
    map.member("cells").itemsMut()[0]=Json::number(int64_t(99));bool rejected=false;
    try{create(rules,map);}catch(const JsonError&){rejected=true;}CHECK(rejected);
}
void cardEffects(Json rules,Json map) {
    rules.member("board").set("cards",Json::parse(R"([{"id":"strike","cost":0,"damage":0,"guard":3,"draw":1}])"));
    auto sim=create(rules,map);Command c;c.player=1;c.type=PlayCard;
    for(int i=0;i<64;++i)sim->module().applyCommand(*sim,c);
    CHECK(state(*sim).plays==64);CHECK(state(*sim).players[0].guard==192);
    auto checksum=sim->checksum();sim->module().applyCommand(*sim,c);CHECK(sim->checksum()==checksum);
    c.type=EndTurn;sim->module().applyCommand(*sim,c);CHECK(state(*sim).plays==0);
    c.player=2;sim->module().applyCommand(*sim,c);CHECK(state(*sim).players[0].guard==0);
}
void motorRules(Json rules,Json map) {
    map.member("players").itemsMut()[0].set("bot",Json::fromBool(false));
    map.set("checkpoints",Json::parse(R"([{"min":[30,0,30],"max":[31,4,31]}])"));
    auto make=[&](){auto sim=std::make_unique<Simulation>(SimConfig{17,60,0},std::make_unique<MotionModule>());sim->setup(rules,map);return sim;};
    auto sim=make();auto actor=[&]() -> MotionActor& {return sim->world().resource<MotionState>()->actors[0];};
    Command c;c.player=1;c.type=kMotionInput;c.args={0,0,0};
    if(rules["motion"].getStr("kind")=="platformer") {
        for(int i=0;i<10;++i)sim->step();CHECK(actor().body.grounded);
        c.args[2]=1;sim->submit(c);sim->step();CHECK(actor().body.pos.y>Fixed::fromRatio(1,10));
        auto velocity=actor().body.velocity.y;c.args[2]=0;sim->submit(c);sim->step();
        c.args[2]=1;sim->submit(c);sim->step();CHECK(actor().body.velocity.y<velocity);
        for(int i=0;i<120;++i)sim->step();CHECK(actor().body.grounded);CHECK(actor().body.pos.y<Fixed::fromRatio(1,100));
    }else {
        map.member("colliders").push(Json::parse(R"({"min":[-5,0,3],"max":[5,4,4]})"));sim=make();
        c.args={1000,0,0};
        for(int i=0;i<240;++i){sim->submit(c);sim->step();}
        CHECK(actor().body.pos.z<Fixed::fromRatio(27,10));CHECK(actor().checkpoint==0);
        c.args={-1000,0,0};for(int i=0;i<60;++i){sim->submit(c);sim->step();}CHECK(actor().speed==Fixed{});
    }
}

void checkpointEntry(Json rules,Json map) {
    rules.member("motion").set("laps",Json::number(int64_t(3)));
    map.member("players").itemsMut()[0].set("bot",Json::fromBool(false));
    map.set("checkpoints",Json::parse(R"([{"min":[-1,-1,-1],"max":[1,4,1]}])"));
    Simulation sim({17,60,0},std::make_unique<MotionModule>());sim.setup(rules,map);
    for(int i=0;i<120;++i)sim.step();
    CHECK(sim.world().resource<MotionState>()->actors[0].lap==0);CHECK(sim.result()==0);
}

void motion(const Json& rules,const Json& map) {
    auto make=[&](){auto sim=std::make_unique<Simulation>(SimConfig{17,60,0},std::make_unique<MotionModule>());sim->setup(rules,map);return sim;};
    auto a=make(),b=make();
    auto initial=a->checksum();Command invalid;invalid.player=1;invalid.type=kMotionInput;invalid.args={1001,0,0};
    a->module().applyCommand(*a,invalid);CHECK(a->checksum()==initial);
    for(int t=0;t<20;++t){a->step();b->step();CHECK(a->checksum()==b->checksum());}
    auto bytes=a->serialize();auto restored=make();CHECK(restored->deserialize(bytes));
    SimSnapshot snapshot;a->saveState(snapshot);auto before=a->checksum();a->step();a->loadState(snapshot);CHECK(a->checksum()==before);
    for(int t=0;t<500&&!a->result();++t){a->step();restored->step();CHECK(a->checksum()==restored->checksum());}
    CHECK(a->result()==1);
}

}
int main(int argc,char** argv) {
    try {
        std::string root=argc>1?argv[1]:"examples/genres";
        auto cards=Json::parseFile(root+"/cards/rules.json"),cardMap=Json::parseFile(root+"/cards/map.json");
        auto puzzle=Json::parseFile(root+"/puzzle/rules.json"),puzzleMap=Json::parseFile(root+"/puzzle/map.json");
        cardRules(cards,cardMap);cardEffects(cards,cardMap);puzzleRules(puzzle,puzzleMap);
        for(const auto* genre:{"platformer","racing"}) {
            auto rules=Json::parseFile(root+"/"+genre+"/rules.json"),map=Json::parseFile(root+"/"+genre+"/map.json");
            motion(rules,map);motorRules(rules,map);if(std::string(genre)=="racing")checkpointEntry(rules,map);
        }
        continuation(cards,cardMap);continuation(puzzle,puzzleMap);
        std::puts("Genre command, card combat, match3, deterministic continuation and restore tests passed");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
