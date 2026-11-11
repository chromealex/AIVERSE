#include "Definitions.h"
#include "modules/rts/RtsNative.h"
#include <stdexcept>
namespace {
using namespace aiverse;
using namespace aiverse::lib::strategy;
class StrategyDemo final:public game::NativeModule {
public:
    void setup(game::RtsModule&,Simulation&,game::NativeState& s,const Json& config)const override {
        auto rate=config.getInt("rate",1);if(rate<1||rate>1000)throw std::runtime_error("strategy_demo.rate: expected 1..1000");
        s.values["rate"]=rate;
        auto turns=beginTurns({{1,1,10,3,0,true},{2,2,5,3,0,true}});
        auto cards=makeCards(strategy_demo::cardRules(),{{1,1,1},{2,2,2}},2);
        FactoryState factory{0,{{1,{1,{10000,0},{10000,10000}}},{2,{2,{0,0},{10000,10000}}}}};
        PushHistory puzzle{{6,{7}},{},{}};
        if(!turns||!cards||!storeState(s,"turns",*turns)||!storeState(s,"cards",*cards)||!storeState(s,"puzzle",puzzle)||!storeState(s,"factory",factory)||!storeState(s,"offline",OfflineState{0,{0,0}})||!storeState(s,"number",BigNumber{1}))throw std::runtime_error("strategy_demo: state initialization failed");
    }
    void tick(game::RtsModule&,Simulation& sim,game::NativeState& s)const override {
        OfflineState offline;FactoryState factory;BigNumber number;
        if(!restoreState(s,"offline",offline)||!restoreState(s,"factory",factory)||!restoreState(s,"number",number))return;
        if(!advanceOffline(strategy_demo::law(uint32_t(s.values.at("rate"))),offline,uint64_t(sim.tick())+1)||!stepFactory(strategy_demo::factoryRules(),factory,uint64_t(sim.tick())+1))return;
        auto next=number.plus(BigNumber{1});if(!next)return;
        storeState(s,"offline",offline);storeState(s,"factory",factory);storeState(s,"number",*next);
    }
    void command(game::RtsModule&,Simulation&,game::NativeState& s,const Command& cmd)const override {
        if(cmd.player<1||cmd.player>2)return;
        // Example wire protocol: param 1 = push right, 2 = play first card, 3 = pass,
        // 4 = queue attack, 5 = resolve pending attack. The engine owns command ID routing.
        if(cmd.param==1&&cmd.player==1){PushHistory puzzle;if(restoreState(s,"puzzle",puzzle)&&pushMove(strategy_demo::board(),puzzle,1))storeState(s,"puzzle",puzzle);}
        else if(cmd.param==2||cmd.param==3){
            CardState cards;if(!restoreState(s,"cards",cards)||cards.lastCommand==UINT64_MAX)return;
            auto player=int32_t(cmd.player)-1;bool changed=false;
            if(cmd.param==3)changed=passCards(strategy_demo::cardRules(),cards,player,cards.lastCommand+1);
            else if(!cards.seats[size_t(player)].zones[1].empty())changed=playCard(strategy_demo::cardRules(),cards,player,cards.seats[size_t(player)].zones[1].back(),1-player,cards.lastCommand+1);
            if(changed)storeState(s,"cards",cards);
        }else if(cmd.param==4){
            TurnState turns;if(!restoreState(s,"turns",turns)||turns.lastSequence==UINT64_MAX)return;
            auto actor=int64_t(cmd.player);TurnAction action{turns.lastSequence+1,actor,3-actor,1,1,false};
            if(queueAction(turns,cmd.player,action,[](const auto&,const auto&){return true;}))storeState(s,"turns",turns);
        }else if(cmd.param==5&&cmd.player==1){TurnState turns;if(restoreState(s,"turns",turns)&&resolveAction(turns))storeState(s,"turns",turns);}
    }
};
}
std::unique_ptr<aiverse::game::NativeModule> createStrategyDemo(){return std::make_unique<StrategyDemo>();}
