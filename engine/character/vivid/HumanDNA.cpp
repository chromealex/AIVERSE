#include "HumanDNA.h"
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace aiverse::vivid {
namespace {
void limit(float value,float lo,float hi,const char* field) {
    if(!std::isfinite(value)||value<lo||value>hi)
        throw std::invalid_argument(std::string("VIVID DNA invalid ")+field);
}
float getFloat(const Json& node,const char* field) {
    if(!node.isNumber())throw std::invalid_argument(std::string("VIVID DNA expected number: ")+field);
    const double d=node.toDouble(std::numeric_limits<double>::quiet_NaN());
    if(!std::isfinite(d)||d<-100000.0||d>100000.0)throw std::invalid_argument(std::string("VIVID DNA invalid number: ")+field);
    return float(d);
}
std::uint32_t getCount(const Json& node,const char* field) {
    if(!node.isNumber())throw std::invalid_argument(std::string("VIVID DNA expected integer: ")+field);
    auto n=node.integer(-1);
    if(n<0||n>std::numeric_limits<std::uint32_t>::max() ||
       node.numberLiteral().find_first_of(".eE")!=std::string::npos)
        throw std::invalid_argument(std::string("VIVID DNA integer range: ")+field);
    return std::uint32_t(n);
}
void unknownFields(const Json& obj,const char* path,
                   std::initializer_list<const char*> allowed) {
    if(!obj.isObject())throw std::invalid_argument(std::string("VIVID DNA object: ")+path);
    for(const auto& [key,value]:obj.members()) {
        (void)value;
        if(std::find_if(allowed.begin(),allowed.end(),
                        [&](const char* candidate){return key==candidate;})==allowed.end())
            throw std::invalid_argument(std::string("VIVID DNA unknown ")+path+"."+key);
    }
}
void stringID(const std::string& id,const char* label,bool dots=false) {
    if(id.empty()||id.size()>96)throw std::invalid_argument(std::string("VIVID DNA empty/long ")+label);
    for(const unsigned char c:id)
        if(!(std::isalnum(c)||c=='_'||c=='-'||(dots&&c=='.')))
            throw std::invalid_argument(std::string("VIVID DNA unsafe ")+label);
}
std::array<float,3> readRgb(const Json& data,const char* name) {
    if(!data.isArray()||data.size()!=3)
        throw std::invalid_argument(std::string("VIVID DNA expected RGB: ")+name);
    std::array<float,3> rgb{};
    for(int i=0;i<3;++i) {
        rgb[std::size_t(i)]=getFloat(data[std::size_t(i)],name);
        limit(rgb[std::size_t(i)],0.0f,1.0f,name);
    }
    return rgb;
}
Json colorJson(const std::array<float,3>& rgb) {
    auto j=Json::array();
    for(float c:rgb)j.push(Json::number(double(c),6));
    return j;
}
}

void HumanDNA::validate() const {
    if(version!=kVersion)throw std::invalid_argument("VIVID DNA version unsupported");
    stringID(id,"id",true);
    if(displayName.empty()||displayName.size()>128)
        throw std::invalid_argument("VIVID DNA displayName invalid");
    limit(anatomy.heightMeters,0.8f,2.6f,"anatomy.heightMeters");
    limit(anatomy.headScale,0.5f,1.8f,"anatomy.headScale");
    limit(anatomy.headWidth,0.5f,1.8f,"anatomy.headWidth");
    limit(anatomy.jawWidth,0.5f,1.8f,"anatomy.jawWidth");
    limit(anatomy.jawLength,0.5f,1.8f,"anatomy.jawLength");
    limit(anatomy.cheekFullness,0.0f,1.0f,"anatomy.cheekFullness");
    limit(anatomy.noseLength,0.0f,1.0f,"anatomy.noseLength");
    limit(anatomy.noseWidth,0.0f,1.0f,"anatomy.noseWidth");
    limit(anatomy.eyeSpacing,0.0f,1.0f,"anatomy.eyeSpacing");
    limit(anatomy.eyeScale,0.5f,1.8f,"anatomy.eyeScale");
    limit(anatomy.browHeight,0.0f,1.0f,"anatomy.browHeight");
    limit(anatomy.mouthWidth,0.0f,1.0f,"anatomy.mouthWidth");
    limit(anatomy.lipFullness,0.0f,1.0f,"anatomy.lipFullness");
    limit(anatomy.earScale,0.5f,1.8f,"anatomy.earScale");
    limit(anatomy.neckLength,0.0f,1.0f,"anatomy.neckLength");
    limit(anatomy.shoulderWidth,0.0f,1.0f,"anatomy.shoulderWidth");
    limit(anatomy.bodyBuild,0.0f,1.0f,"anatomy.bodyBuild");
    limit(skin.melanin,0.0f,1.0f,"skin.melanin");
    limit(skin.hemoglobin,0.0f,1.0f,"skin.hemoglobin");
    limit(skin.scatteringMm,0.01f,6.0f,"skin.scatteringMm");
    limit(skin.roughness,0.02f,1.0f,"skin.roughness");
    limit(skin.oiliness,0.0f,1.0f,"skin.oiliness");
    limit(skin.poreScale,0.0f,2.0f,"skin.poreScale");
    limit(skin.wrinkleStrength,0.0f,1.0f,"skin.wrinkleStrength");
    limit(skin.wetness,0.0f,1.0f,"skin.wetness");
    limit(eyes.irisRadius,0.15f,0.75f,"eyes.irisRadius");
    limit(eyes.pupil,0.05f,0.95f,"eyes.pupil");
    limit(eyes.corneaIor,1.25f,1.45f,"eyes.corneaIor");
    limit(eyes.limbus,0.0f,1.0f,"eyes.limbus");
    limit(eyes.scleraRedness,0.0f,1.0f,"eyes.scleraRedness");
    limit(eyes.tearFilm,0.0f,1.0f,"eyes.tearFilm");
    limit(eyes.gazeLimitDegrees,0.0f,60.0f,"eyes.gazeLimitDegrees");
    limit(groom.lengthMeters,0.0f,2.0f,"groom.lengthMeters");
    limit(groom.density,0.0f,1.0f,"groom.density");
    limit(groom.curl,0.0f,1.0f,"groom.curl");
    limit(groom.clump,0.0f,1.0f,"groom.clump");
    limit(groom.melanin,0.0f,1.0f,"groom.melanin");
    limit(groom.roughness,0.05f,1.0f,"groom.roughness");
    limit(groom.stiffness,0.0f,1.0f,"groom.stiffness");
    limit(groom.damping,0.0f,1.0f,"groom.damping");
    limit(groom.gravity,0.0f,3.0f,"groom.gravity");
    limit(groom.wind,0.0f,3.0f,"groom.wind");

    for(float channel:expressionRest)limit(channel,0.0f,1.0f,"expressionRest");
    for(float c:skin.melaninColor)limit(c,0.0f,1.0f,"skin.melaninColor");
    for(float c:eyes.irisColor)limit(c,0.0f,1.0f,"eyes.irisColor");
    for(float c:groom.color)limit(c,0.0f,1.0f,"groom.color");
    if(groom.guideCount<4||groom.guideCount>8192||
       groom.segments<2||groom.segments>32||
       std::uint64_t(groom.guideCount)*groom.segments>262144)
        throw std::invalid_argument("VIVID DNA groom guide/segment budget exceeded");
    if(rig.revision!=1)throw std::invalid_argument("VIVID DNA rig revision unsupported");
    std::set<std::string> bones;
    for(const std::string* s:{&rig.rootBone,&rig.headBone,&rig.jawBone,
                               &rig.leftEyeBone,&rig.rightEyeBone}) {
        stringID(*s,"bone",true);
        if(!bones.insert(*s).second)throw std::invalid_argument("VIVID DNA duplicate rig bone");
    }
    if(rig.optionalBones.size()>128)throw std::invalid_argument("VIVID DNA too many optional bones");
    for(const auto& name:rig.optionalBones) {
        stringID(name,"optional bone",true);
        if(!bones.insert(name).second)throw std::invalid_argument("VIVID DNA duplicate optional bone");
    }
}
HumanDNA HumanDNA::fromJson(const Json& src) {
    unknownFields(src,"root",{"version","id","displayName","seed","anatomy",
                               "skin","eyes","groom","rig","expressionRest"});
    HumanDNA d;
    if(src.has("version"))d.version=getCount(src["version"],"version");
    if(src.has("id")) {
        if(!src["id"].isString())throw std::invalid_argument("VIVID DNA id string");
        d.id=src["id"].str();
    }
    if(src.has("displayName")) {
        if(!src["displayName"].isString())throw std::invalid_argument("VIVID DNA displayName string");
        d.displayName=src["displayName"].str();
    }
    if(src.has("seed"))d.seed=getCount(src["seed"],"seed");
    for(const char* section:{"anatomy","skin","eyes","groom","rig"})
        if(src.has(section)&&!src[section].isObject())
            throw std::invalid_argument(std::string("VIVID DNA invalid object: ")+section);
    if(src["anatomy"].has("heightMeters")) d.anatomy.heightMeters=getFloat(src["anatomy"]["heightMeters"],"anatomy.heightMeters");
    if(src["anatomy"].has("headScale")) d.anatomy.headScale=getFloat(src["anatomy"]["headScale"],"anatomy.headScale");
    if(src["anatomy"].has("headWidth")) d.anatomy.headWidth=getFloat(src["anatomy"]["headWidth"],"anatomy.headWidth");
    if(src["anatomy"].has("jawWidth")) d.anatomy.jawWidth=getFloat(src["anatomy"]["jawWidth"],"anatomy.jawWidth");
    if(src["anatomy"].has("jawLength")) d.anatomy.jawLength=getFloat(src["anatomy"]["jawLength"],"anatomy.jawLength");
    if(src["anatomy"].has("cheekFullness")) d.anatomy.cheekFullness=getFloat(src["anatomy"]["cheekFullness"],"anatomy.cheekFullness");
    if(src["anatomy"].has("noseLength")) d.anatomy.noseLength=getFloat(src["anatomy"]["noseLength"],"anatomy.noseLength");
    if(src["anatomy"].has("noseWidth")) d.anatomy.noseWidth=getFloat(src["anatomy"]["noseWidth"],"anatomy.noseWidth");
    if(src["anatomy"].has("eyeSpacing")) d.anatomy.eyeSpacing=getFloat(src["anatomy"]["eyeSpacing"],"anatomy.eyeSpacing");
    if(src["anatomy"].has("eyeScale")) d.anatomy.eyeScale=getFloat(src["anatomy"]["eyeScale"],"anatomy.eyeScale");
    if(src["anatomy"].has("browHeight")) d.anatomy.browHeight=getFloat(src["anatomy"]["browHeight"],"anatomy.browHeight");
    if(src["anatomy"].has("mouthWidth")) d.anatomy.mouthWidth=getFloat(src["anatomy"]["mouthWidth"],"anatomy.mouthWidth");
    if(src["anatomy"].has("lipFullness")) d.anatomy.lipFullness=getFloat(src["anatomy"]["lipFullness"],"anatomy.lipFullness");
    if(src["anatomy"].has("earScale")) d.anatomy.earScale=getFloat(src["anatomy"]["earScale"],"anatomy.earScale");
    if(src["anatomy"].has("neckLength")) d.anatomy.neckLength=getFloat(src["anatomy"]["neckLength"],"anatomy.neckLength");
    if(src["anatomy"].has("shoulderWidth")) d.anatomy.shoulderWidth=getFloat(src["anatomy"]["shoulderWidth"],"anatomy.shoulderWidth");
    if(src["anatomy"].has("bodyBuild")) d.anatomy.bodyBuild=getFloat(src["anatomy"]["bodyBuild"],"anatomy.bodyBuild");
    if(src.has("anatomy"))unknownFields(src["anatomy"],"anatomy",{"heightMeters","headScale","headWidth","jawWidth","jawLength","cheekFullness","noseLength","noseWidth","eyeSpacing","eyeScale","browHeight","mouthWidth","lipFullness","earScale","neckLength","shoulderWidth","bodyBuild"});
    if(src["skin"].has("melanin")) d.skin.melanin=getFloat(src["skin"]["melanin"],"skin.melanin");
    if(src["skin"].has("hemoglobin")) d.skin.hemoglobin=getFloat(src["skin"]["hemoglobin"],"skin.hemoglobin");
    if(src["skin"].has("scatteringMm")) d.skin.scatteringMm=getFloat(src["skin"]["scatteringMm"],"skin.scatteringMm");
    if(src["skin"].has("roughness")) d.skin.roughness=getFloat(src["skin"]["roughness"],"skin.roughness");
    if(src["skin"].has("oiliness")) d.skin.oiliness=getFloat(src["skin"]["oiliness"],"skin.oiliness");
    if(src["skin"].has("poreScale")) d.skin.poreScale=getFloat(src["skin"]["poreScale"],"skin.poreScale");
    if(src["skin"].has("wrinkleStrength")) d.skin.wrinkleStrength=getFloat(src["skin"]["wrinkleStrength"],"skin.wrinkleStrength");
    if(src["skin"].has("wetness")) d.skin.wetness=getFloat(src["skin"]["wetness"],"skin.wetness");
    if(src.has("skin"))unknownFields(src["skin"],"skin",{"melaninColor","melanin","hemoglobin","scatteringMm","roughness","oiliness","poreScale","wrinkleStrength","wetness"});
    if(src["eyes"].has("irisRadius")) d.eyes.irisRadius=getFloat(src["eyes"]["irisRadius"],"eyes.irisRadius");
    if(src["eyes"].has("pupil")) d.eyes.pupil=getFloat(src["eyes"]["pupil"],"eyes.pupil");
    if(src["eyes"].has("corneaIor")) d.eyes.corneaIor=getFloat(src["eyes"]["corneaIor"],"eyes.corneaIor");
    if(src["eyes"].has("limbus")) d.eyes.limbus=getFloat(src["eyes"]["limbus"],"eyes.limbus");
    if(src["eyes"].has("scleraRedness")) d.eyes.scleraRedness=getFloat(src["eyes"]["scleraRedness"],"eyes.scleraRedness");
    if(src["eyes"].has("tearFilm")) d.eyes.tearFilm=getFloat(src["eyes"]["tearFilm"],"eyes.tearFilm");
    if(src["eyes"].has("gazeLimitDegrees")) d.eyes.gazeLimitDegrees=getFloat(src["eyes"]["gazeLimitDegrees"],"eyes.gazeLimitDegrees");
    if(src.has("eyes"))unknownFields(src["eyes"],"eyes",{"irisColor","irisRadius","pupil","corneaIor","limbus","scleraRedness","tearFilm","gazeLimitDegrees"});
    if(src["groom"].has("lengthMeters")) d.groom.lengthMeters=getFloat(src["groom"]["lengthMeters"],"groom.lengthMeters");
    if(src["groom"].has("density")) d.groom.density=getFloat(src["groom"]["density"],"groom.density");
    if(src["groom"].has("curl")) d.groom.curl=getFloat(src["groom"]["curl"],"groom.curl");
    if(src["groom"].has("clump")) d.groom.clump=getFloat(src["groom"]["clump"],"groom.clump");
    if(src["groom"].has("melanin")) d.groom.melanin=getFloat(src["groom"]["melanin"],"groom.melanin");
    if(src["groom"].has("roughness")) d.groom.roughness=getFloat(src["groom"]["roughness"],"groom.roughness");
    if(src["groom"].has("stiffness")) d.groom.stiffness=getFloat(src["groom"]["stiffness"],"groom.stiffness");
    if(src["groom"].has("damping")) d.groom.damping=getFloat(src["groom"]["damping"],"groom.damping");
    if(src["groom"].has("gravity")) d.groom.gravity=getFloat(src["groom"]["gravity"],"groom.gravity");
    if(src["groom"].has("wind")) d.groom.wind=getFloat(src["groom"]["wind"],"groom.wind");
    if(src.has("groom"))unknownFields(src["groom"],"groom",{"color","guideCount","segments","lengthMeters","density","curl","clump","melanin","roughness","stiffness","damping","gravity","wind"});

    if(src["skin"].has("melaninColor"))d.skin.melaninColor=readRgb(src["skin"]["melaninColor"],"skin.melaninColor");
    if(src["eyes"].has("irisColor"))d.eyes.irisColor=readRgb(src["eyes"]["irisColor"],"eyes.irisColor");
    if(src["groom"].has("color"))d.groom.color=readRgb(src["groom"]["color"],"groom.color");
    if(src["groom"].has("guideCount"))d.groom.guideCount=getCount(src["groom"]["guideCount"],"groom.guideCount");
    if(src["groom"].has("segments"))d.groom.segments=getCount(src["groom"]["segments"],"groom.segments");
    if(src["rig"].has("revision"))d.rig.revision=getCount(src["rig"]["revision"],"rig.revision");
    if(src.has("rig"))unknownFields(src["rig"],"rig",
        {"rootBone","headBone","jawBone","leftEyeBone",
         "rightEyeBone","revision","optionalBones"});
    const std::array<std::pair<const char*,std::string*>,5> rigKeys{{
        {"rootBone",&d.rig.rootBone},{"headBone",&d.rig.headBone},
        {"jawBone",&d.rig.jawBone},{"leftEyeBone",&d.rig.leftEyeBone},
        {"rightEyeBone",&d.rig.rightEyeBone}
    }};
    for(const auto& [key,out]:rigKeys) {
        if(src["rig"].has(key)) {
            if(!src["rig"][key].isString())throw std::invalid_argument("VIVID DNA bone field type");
            *out=src["rig"][key].str();
        }
    }
    if(src.has("rig") && src["rig"].has("optionalBones")) {
        if(!src["rig"]["optionalBones"].isArray())throw std::invalid_argument("VIVID DNA optionalBones array");
        d.rig.optionalBones.clear();
        for(const auto& bone:src["rig"]["optionalBones"].items()) {
            if(!bone.isString())throw std::invalid_argument("VIVID DNA optionalBones item");
            d.rig.optionalBones.push_back(bone.str());
        }
    }
    if(src.has("expressionRest")) {
        const auto& rest=src["expressionRest"];
        if(!rest.isArray()||rest.size()!=d.expressionRest.size())
            throw std::invalid_argument("VIVID DNA expressionRest requires 52 channels");
        for(std::size_t i=0;i<rest.size();++i)d.expressionRest[i]=getFloat(rest[i],"expressionRest");
    }
    d.validate();
    return d;
}

Json HumanDNA::toJson() const {
    validate();
    Json root=Json::object();
    root.set("version",Json::number(std::int64_t(version)));
    root.set("id",Json::string(id));
    root.set("displayName",Json::string(displayName));
    root.set("seed",Json::number(std::int64_t(seed)));
    auto anatomyj=Json::object();
    auto skinj=Json::object();
    auto eyesj=Json::object();
    auto groomj=Json::object();
    anatomyj.set("heightMeters",Json::number(double(anatomy.heightMeters),6));
    anatomyj.set("headScale",Json::number(double(anatomy.headScale),6));
    anatomyj.set("headWidth",Json::number(double(anatomy.headWidth),6));
    anatomyj.set("jawWidth",Json::number(double(anatomy.jawWidth),6));
    anatomyj.set("jawLength",Json::number(double(anatomy.jawLength),6));
    anatomyj.set("cheekFullness",Json::number(double(anatomy.cheekFullness),6));
    anatomyj.set("noseLength",Json::number(double(anatomy.noseLength),6));
    anatomyj.set("noseWidth",Json::number(double(anatomy.noseWidth),6));
    anatomyj.set("eyeSpacing",Json::number(double(anatomy.eyeSpacing),6));
    anatomyj.set("eyeScale",Json::number(double(anatomy.eyeScale),6));
    anatomyj.set("browHeight",Json::number(double(anatomy.browHeight),6));
    anatomyj.set("mouthWidth",Json::number(double(anatomy.mouthWidth),6));
    anatomyj.set("lipFullness",Json::number(double(anatomy.lipFullness),6));
    anatomyj.set("earScale",Json::number(double(anatomy.earScale),6));
    anatomyj.set("neckLength",Json::number(double(anatomy.neckLength),6));
    anatomyj.set("shoulderWidth",Json::number(double(anatomy.shoulderWidth),6));
    anatomyj.set("bodyBuild",Json::number(double(anatomy.bodyBuild),6));
    root.set("anatomy",std::move(anatomyj));
    skinj.set("melanin",Json::number(double(skin.melanin),6));
    skinj.set("hemoglobin",Json::number(double(skin.hemoglobin),6));
    skinj.set("scatteringMm",Json::number(double(skin.scatteringMm),6));
    skinj.set("roughness",Json::number(double(skin.roughness),6));
    skinj.set("oiliness",Json::number(double(skin.oiliness),6));
    skinj.set("poreScale",Json::number(double(skin.poreScale),6));
    skinj.set("wrinkleStrength",Json::number(double(skin.wrinkleStrength),6));
    skinj.set("wetness",Json::number(double(skin.wetness),6));
    skinj.set("melaninColor",colorJson(skin.melaninColor));
    root.set("skin",std::move(skinj));
    eyesj.set("irisRadius",Json::number(double(eyes.irisRadius),6));
    eyesj.set("pupil",Json::number(double(eyes.pupil),6));
    eyesj.set("corneaIor",Json::number(double(eyes.corneaIor),6));
    eyesj.set("limbus",Json::number(double(eyes.limbus),6));
    eyesj.set("scleraRedness",Json::number(double(eyes.scleraRedness),6));
    eyesj.set("tearFilm",Json::number(double(eyes.tearFilm),6));
    eyesj.set("gazeLimitDegrees",Json::number(double(eyes.gazeLimitDegrees),6));
    eyesj.set("irisColor",colorJson(eyes.irisColor));
    root.set("eyes",std::move(eyesj));
    groomj.set("lengthMeters",Json::number(double(groom.lengthMeters),6));
    groomj.set("density",Json::number(double(groom.density),6));
    groomj.set("curl",Json::number(double(groom.curl),6));
    groomj.set("clump",Json::number(double(groom.clump),6));
    groomj.set("melanin",Json::number(double(groom.melanin),6));
    groomj.set("roughness",Json::number(double(groom.roughness),6));
    groomj.set("stiffness",Json::number(double(groom.stiffness),6));
    groomj.set("damping",Json::number(double(groom.damping),6));
    groomj.set("gravity",Json::number(double(groom.gravity),6));
    groomj.set("wind",Json::number(double(groom.wind),6));
    groomj.set("color",colorJson(groom.color));
    groomj.set("guideCount",Json::number(std::int64_t(groom.guideCount)));
    groomj.set("segments",Json::number(std::int64_t(groom.segments)));
    root.set("groom",std::move(groomj));

    auto rigj=Json::object();
    rigj.set("rootBone",Json::string(rig.rootBone));
    rigj.set("headBone",Json::string(rig.headBone));
    rigj.set("jawBone",Json::string(rig.jawBone));
    rigj.set("leftEyeBone",Json::string(rig.leftEyeBone));
    rigj.set("rightEyeBone",Json::string(rig.rightEyeBone));
    rigj.set("revision",Json::number(std::int64_t(rig.revision)));
    auto extra=Json::array();
    for(const auto& bone:rig.optionalBones)extra.push(Json::string(bone));
    rigj.set("optionalBones",std::move(extra));
    root.set("rig",std::move(rigj));
    auto rest=Json::array();
    for(float w:expressionRest)rest.push(Json::number(double(w),6));
    root.set("expressionRest",std::move(rest));
    return root;
}
HumanDNA HumanDNA::preset(std::string_view which) {
    HumanDNA dna;
    if(which=="neutral") {}
    else if(which=="warm") {
        dna.id="vivid_warm";dna.displayName="Warm Skin";
        dna.skin.melaninColor={0.65f,0.38f,0.24f};
        dna.skin.hemoglobin=0.58f;dna.eyes.irisColor={0.36f,0.24f,0.13f};
    } else if(which=="cool") {
        dna.id="vivid_cool";dna.displayName="Cool Skin";
        dna.skin.melaninColor={0.62f,0.53f,0.46f};
        dna.skin.hemoglobin=0.35f;dna.eyes.irisColor={0.22f,0.33f,0.55f};
    } else if(which=="stylized") {
        dna.id="vivid_stylized";dna.displayName="Stylized Human";
        dna.anatomy.eyeScale=1.35f;dna.anatomy.headScale=1.2f;
        dna.anatomy.noseLength=0.35f;dna.skin.poreScale=0.05f;
    } else throw std::invalid_argument("VIVID unknown DNA preset");
    dna.validate();return dna;
}
} // namespace aiverse::vivid
