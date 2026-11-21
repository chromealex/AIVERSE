#include "engine/render/PrismaData.h"
#include "engine/render/PrismaVolumes.h"
#include <cmath>
#include <cstdio>
using namespace aiverse;
using namespace aiverse::render;
int main() {
    int errors=0;
    auto check=[&](bool v){if(!v)++errors;};
    for(auto mode:{HdrMode::Auto,HdrMode::Off,HdrMode::On})for(auto quality:{PrismaQuality::Off,PrismaQuality::Low,PrismaQuality::Medium,PrismaQuality::High})check(!hdrAtQuality(mode,quality,false));
    check(hdrAtQuality(HdrMode::Auto,PrismaQuality::High,true));
    check(!hdrAtQuality(HdrMode::Auto,PrismaQuality::Medium,true));
    check(hdrAtQuality(HdrMode::On,PrismaQuality::Medium,true));
    check(!hdrAtQuality(HdrMode::On,PrismaQuality::Low,true));
    check(!hdrAtQuality(HdrMode::Off,PrismaQuality::High,true));
    check(parsePrisma(Json(),Json()).hdr==HdrMode::Auto);
    check(parsePrisma(Json::parse(R"({"hdr":"on"})"),Json::parse(R"({"hdr":"off"})")).hdr==HdrMode::Off);
    std::vector<std::string> issues;
    check(parsePrisma(Json::parse(R"({"hdr":"bogus"})"),Json(),&issues).hdr==HdrMode::Auto);
    check(issues.size()==1);
    auto e=parsePrismaEffect(Json::parse(R"({"type":"bloom","threshold":4})"),true);
    check(e.get("threshold")==4);
    // PRISMA volume tests are CPU-only and need no GPU or renderer instance.
    PrismaVolumeStack stack;
    PrismaVolume sphere;
    sphere.shape=PrismaVolumeShape::Sphere;
    sphere.radius=2;
    sphere.blendDistance=2;
    sphere.profile=parsePrisma(Json::parse(R"({"effects":[{"type":"colorGrade","exposure":2}]})"),Json());
    stack.volumes.push_back(sphere);
    check(std::abs(PrismaVolumeStack::influence(sphere,{3,0,0})-.5f)<1.e-6f);
    check(PrismaVolumeStack::influence(sphere,{4,0,0})==0);
    check(PrismaVolumeStack::influence(sphere,{0,0,0})==1);
    const PrismaSettings original=parsePrisma(Json::parse(R"({"effects":[{"type":"colorGrade","exposure":0}]})"),Json());
    auto faded=stack.resolve(original,{3,0,0});
    check(faded.find(PrismaType::ColorGrade)!=nullptr);
    if (const auto* grade=faded.find(PrismaType::ColorGrade))
        check(std::abs(grade->get("exposure")-1.f)<1.e-6f);
    sphere.layerMask=2;
    check(PrismaVolumeStack::influence(sphere,{0,0,0},1)==0);
    stack.volumes[0]=sphere;
    check(stack.resolve(original,{0,0,0},1).find(PrismaType::ColorGrade)->get("exposure")==0);
    PrismaVolume box;
    box.shape=PrismaVolumeShape::Box;
    box.halfExtents={1,1,1};
    box.blendDistance=2;
    check(std::abs(PrismaVolumeStack::influence(box,{2,0,0})-.5f)<1.e-6f);
    box.priority=10;box.profile=parsePrisma(Json::parse(R"({"effects":[{"type":"colorGrade","exposure":3}]})"),Json());
    stack.volumes.push_back(box);
    auto high=stack.resolve(original,{0,0,0},1);
    check(high.find(PrismaType::ColorGrade)!=nullptr);
    if (const auto* grade=high.find(PrismaType::ColorGrade))check(grade->get("exposure")==3);
    stack.volumes[1].enabled=false;
    check(PrismaVolumeStack::influence(stack.volumes[1],{0,0,0})==0);
    std::printf("HDR policy: %d errors\n",errors);
    return errors?1:0;
}
