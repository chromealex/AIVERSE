#include <aiverselib/app/MatchBridge.h>
#include "engine/meta/Meta.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse;
int main(){
    meta::Config config;config.assets["gold"]={1000,Json::object()};
    meta::Profile profile;profile.player="p1";
    meta::Economy economy(config,profile);
    lib::app::RewardReceipt receipt{"m1","p1","gold",100};
    auto first=lib::app::grantMatch(economy,receipt);
    if(!first||first->code!=meta::Code::Ok)return 1;
    auto duplicate=lib::app::grantMatch(economy,receipt);
    if(!duplicate||duplicate->code!=meta::Code::Duplicate)return 2;
    receipt.amount=101;auto conflict=lib::app::grantMatch(economy,receipt);
    if(!conflict||conflict->code!=meta::Code::Conflict)return 3;
    receipt.player="p2";if(lib::app::grantMatch(economy,receipt))return 4;
    if(economy.profile().balances.at("gold")!=100)return 5;
    std::cout<<"Match rewards reuse meta economy: OK\n";
}
