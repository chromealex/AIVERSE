// game.json "splash" choices (splash::resolveBlock / resolveMapBlock / projectSpec): every project decides
// what its loading screen is - the engine screen, the engine screen with changes, its own, or none.
#include "engine/splash/SplashData.h"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace aiverse;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
splash::Choice pick(const char* text){return splash::resolveBlock(Json::parse(text));}
bool engine(const splash::Choice& c){return !c.off&&c.block.dump(-1)==splash::defaultBlock().dump(-1);}
bool throws(const char* text){try{pick(text);}catch(const std::runtime_error&){return true;}return false;}
}
int main(){
    try{
        // The engine screen: no block, null, true, "default".
        CHECK(engine(splash::resolveBlock(Json())));
        CHECK(engine(pick("null"))&&engine(pick("true"))&&engine(pick("\"default\""))&&engine(pick("\"engine\"")));
        auto spec=splash::projectSpec(Json());CHECK(spec.enabled&&spec.layers.size()==4);
        // None: false, "none", "off", {"enabled": false}.
        for(const char* off:{"false","\"none\"","\"off\"","{\"enabled\":false,\"background\":\"#ff0000\"}"}){CHECK(pick(off).off);CHECK(!splash::projectSpec(Json::parse(off)).enabled);}
        // The engine screen with changes: keys replaced, objects merged one by one, the logos kept.
        auto changed=pick(R"({"base":"engine","background":"#204060","minSeconds":0,"progress":{"color":"#00ff00"}})");
        CHECK(!changed.off&&changed.block.getStr("background")=="#204060"&&changed.block["minSeconds"].toDouble(9)==0);
        CHECK(changed.block["progress"].getStr("color")=="#00ff00"&&changed.block["progress"].getStr("position")=="bottom");
        CHECK(changed.block["logos"].size()==splash::defaultBlock()["logos"].size()&&!changed.block.has("base"));
        spec=splash::parseSpec(changed.block);CHECK(spec.minimumTime()==0&&spec.layers.size()==4);
        auto noBar=pick(R"({"base":"engine","progress":false})");CHECK(!splash::parseSpec(noBar.block).progress.show);
        auto ownLogo=pick(R"({"base":"engine","logos":[{"image":"branding/logo.png"}]})");CHECK(splash::parseSpec(ownLogo.block).layers.size()==1);
        // The project's own screen is taken as written; background only is an object without layers.
        auto own=pick(R"({"background":"#000000","progress":{"show":false}})");
        CHECK(!own.off&&own.block.getStr("background")=="#000000");spec=splash::parseSpec(own.block);CHECK(spec.layers.empty()&&!spec.progress.show);
        // Map loading: the same screen by default, or its own choice.
        auto base=Json::parse(R"({"background":"#111111","mapLoading":false})");
        CHECK(!splash::resolveBlock(base).off&&!splash::resolveBlock(base).block.has("mapLoading"));
        CHECK(splash::resolveMapBlock(base).off);
        CHECK(engine(splash::resolveMapBlock(Json::parse(R"({"background":"#111111","mapLoading":"default"})"))));
        CHECK(splash::resolveMapBlock(Json::parse(R"({"background":"#111111","mapLoading":"same"})")).block.getStr("background")=="#111111");
        CHECK(splash::resolveMapBlock(Json::parse("false")).off&&engine(splash::resolveMapBlock(Json())));
        // Mistakes are named.
        CHECK(throws("\"fancy\"")&&throws("3")&&throws(R"({"base":"studio"})"));
        std::printf("splash choices: engine, changed, own, none, map loading: OK\n");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
