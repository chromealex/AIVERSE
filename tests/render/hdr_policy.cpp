#include "engine/render/PostFxData.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::render;
int main() {
    int errors=0;
    auto check=[&](bool v){if(!v)++errors;};
    for(auto mode:{HdrMode::Auto,HdrMode::Off,HdrMode::On})for(auto quality:{PostFxQuality::Off,PostFxQuality::Low,PostFxQuality::Medium,PostFxQuality::High})check(!hdrAtQuality(mode,quality,false));
    check(hdrAtQuality(HdrMode::Auto,PostFxQuality::High,true));
    check(!hdrAtQuality(HdrMode::Auto,PostFxQuality::Medium,true));
    check(hdrAtQuality(HdrMode::On,PostFxQuality::Medium,true));
    check(!hdrAtQuality(HdrMode::On,PostFxQuality::Low,true));
    check(!hdrAtQuality(HdrMode::Off,PostFxQuality::High,true));
    check(parsePostFx(Json(),Json()).hdr==HdrMode::Auto);
    check(parsePostFx(Json::parse(R"({"hdr":"on"})"),Json::parse(R"({"hdr":"off"})")).hdr==HdrMode::Off);
    std::vector<std::string> issues;
    check(parsePostFx(Json::parse(R"({"hdr":"bogus"})"),Json(),&issues).hdr==HdrMode::Auto);
    check(issues.size()==1);
    auto e=parsePostFxEffect(Json::parse(R"({"type":"bloom","threshold":4})"),true);
    check(e.get("threshold")==4);
    std::printf("HDR policy: %d errors\n",errors);
    return errors?1:0;
}
