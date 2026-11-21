#include "engine/render/PrismaLensWater.h"
#include "engine/render/PrismaData.h"
#include "engine/render/PrismaShaders.h"
#include <string>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace aiverse::render;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    auto dry=advancePrismaLensWater({},false,.1f,4);
    check(dry.wetness==0,"dry camera must not invent lens water");
    auto immersed=advancePrismaLensWater({},true,.1f,4);
    check(immersed.wetness==1&&immersed.submerged,"immersion wets the lens");
    auto emerged=advancePrismaLensWater(immersed,false,.1f,4);
    check(!emerged.submerged&&emerged.wetness>.9f&&emerged.age>0,"exit retains draining droplets");
    auto shortDry=advancePrismaLensWater(immersed,false,.25f,.1f);
    check(shortDry.wetness==0,"authored dry time must clear lens");
    for(int i=0;i<50;++i)emerged=advancePrismaLensWater(emerged,false,.1f,4);
    check(emerged.wetness==0&&emerged.age==0,"lens dries completely without recurring rain");
    auto invalid=advancePrismaLensWater(immersed,false,std::numeric_limits<float>::quiet_NaN(),4);
    check(std::isfinite(invalid.age)&&invalid.wetness==1,"invalid delta must not poison history");
    const auto effect=parsePrismaEffect(aiverse::Json::parse(R"({"type":"lensWater","enabled":false,"dropSize":24,"dryTime":7})"),true);
    check(!effect.enabled&&effect.get("dropSize")==24&&effect.get("dryTime")==7,"lens water has independent settings and disable switch");
    const auto again=parsePrismaEffect(prismaEffectJson(effect),true);
    check(!again.enabled&&again.get("dryTime")==7,"lens effect round trips enabled state");
    // Waterline + streak/glint settings: defaults on, independently switchable.
    const auto defaults=parsePrismaEffect(aiverse::Json::parse(R"({"type":"lensWater"})"),true);
    check(defaults.get("waterline")>.5f&&defaults.get("meniscus")>0&&defaults.get("streaks")>0&&defaults.get("glint")>0,
          "waterline, streaks and glints are on by default");
    const auto tuned=parsePrismaEffect(aiverse::Json::parse(
        R"({"type":"lensWater","waterline":false,"meniscus":10,"waveHeight":0.2,"waveLength":2,"waveSpeed":0.5,"lensDistance":0.4,"streaks":0,"glint":1})"),true);
    check(tuned.get("waterline")<.5f&&tuned.get("meniscus")==10&&tuned.get("waveHeight")==0.2f&&tuned.get("lensDistance")==0.4f&&
          tuned.get("streaks")==0&&tuned.get("glint")==1,"waterline parameters parse");
    const auto tunedAgain=parsePrismaEffect(prismaEffectJson(tuned),true);
    check(tunedAgain.get("waterline")<.5f&&tunedAgain.get("waveSpeed")==0.5f,"waterline parameters round trip");
    // Water-covered part of the lens carries no drops/trails: the drop layer
    // is scaled by the dry fraction from the per-pixel waterline test.
    const std::string composite=prisma::kComposite;
    check(composite.find("pfLensDry = 1.0 - wet")!=std::string::npos &&
          composite.find("lensWater0.x*pfLensDry")!=std::string::npos,
          "lens drops must be masked below the waterline");
    check(composite.find("sheetTop")!=std::string::npos&&composite.find("(1.0-inSheet)")!=std::string::npos,
          "emerging lens shows a draining water film that hides drops until it passes");
    check(defaults.get("sheet")>0&&defaults.get("sheetTime")>0,"draining film is on by default");
    const auto noSheet=parsePrismaEffect(aiverse::Json::parse(R"({"type":"lensWater","sheet":0,"sheetTime":2})"),true);
    check(noSheet.get("sheet")==0&&noSheet.get("sheetTime")==2,"draining film can be tuned and disabled");
}
