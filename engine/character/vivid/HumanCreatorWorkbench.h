// VIVID Human Creator workbench: the tools/vivid-creator authoring sections (DNA skin/eyes/groom/anatomy/52 rest channels/rig bone
// names, rig corrections, skin weights, facial shape routes) as a
// UI-independent, validated, undoable document. Pure C++20; no Metal/ImGui.
// Shared by the Studio "Human Creator" window (studio.humanCreator*) and the
// aiversemcp human_* tools. A renderer builds Models with buildModels() on
// its own device thread; editors and agents only ever see JSON.
#pragma once
#include "HumanCreatorSession.h"
#include "HumanPreview.h"
#include "RigAuthoring.h"
#include "SkinWeightAuthoring.h"
#include "Face.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace aiverse::vivid {

// Persisted creator document (project/humans/<name>.json), version 1.
struct HumanCreatorDocument {
    HumanDNA dna{};
    Json rigCorrections=Json::parse(R"({"version":1,"offsets":[]})");
    Json skinWeights; // null = none
    std::vector<ShapeRoute> routes;
};

// Default procedural routes (HumanPreview.cpp): ARKit channel -> target.
inline std::vector<ShapeRoute> humanCreatorDefaultRoutes() {
    return {{Channel::JawOpen,"jawOpen",1.f},{Channel::EyeBlinkLeft,"eyeBlinkLeft",1.f},
            {Channel::EyeBlinkRight,"eyeBlinkRight",1.f},{Channel::MouthSmileLeft,"mouthSmileLeft",1.f},
            {Channel::MouthSmileRight,"mouthSmileRight",1.f}};
}

// Built CPU Models for the native preview (private, not yet uploaded).
struct HumanCreatorModels {
    std::unique_ptr<render::Model> head,hair;
    std::size_t skinVerticesChanged=0;
};

class HumanCreatorWorkbench final {
public:
    HumanCreatorWorkbench() {routes_=humanCreatorDefaultRoutes();}

    // ---- schema: one entry per window section, fields are DNA JSON paths.
    static Json schema() {
        Json sections=Json::array();
        auto section=[&](const char* id,const char* title,const char* hint)->Json& {
            Json s=Json::object();s.set("id",Json::string(id));s.set("title",Json::string(title));
            s.set("hint",Json::string(hint));s.set("fields",Json::array());sections.push(std::move(s));
            return sections.at(sections.size()-1);
        };
        auto num=[](Json& s,const char* key,const char* label,double lo,double hi,double step) {
            Json f=Json::object();f.set("key",Json::string(key));f.set("label",Json::string(label));
            f.set("kind",Json::string("number"));f.set("min",Json::number(lo,6));f.set("max",Json::number(hi,6));
            f.set("step",Json::number(step,6));s.member("fields").push(std::move(f));
        };
        auto kind=[](Json& s,const char* key,const char* label,const char* k) {
            Json f=Json::object();f.set("key",Json::string(key));f.set("label",Json::string(label));
            f.set("kind",Json::string(k));s.member("fields").push(std::move(f));
        };
        {auto& s=section("identity","Identity","Recipe id, display name and procedural seed");
         kind(s,"id","Id","string");kind(s,"displayName","Display name","string");kind(s,"seed","Seed","integer");}
        {auto& s=section("skin","Skin / skin scattering","Melanin/hemoglobin pigment, subsurface scattering and micro surface");
         kind(s,"skin.melaninColor","Melanin colour","color");
         num(s,"skin.melanin","Melanin",0,1,.01);num(s,"skin.hemoglobin","Hemoglobin",0,1,.01);
         num(s,"skin.scatteringMm","Scattering (mm)",.01,6,.01);num(s,"skin.roughness","Roughness",.02,1,.01);
         num(s,"skin.oiliness","Oiliness",0,1,.01);num(s,"skin.poreScale","Pore scale",0,2,.01);
         num(s,"skin.wrinkleStrength","Wrinkles",0,1,.01);num(s,"skin.wetness","Wetness",0,1,.01);}
        {auto& s=section("eyes","Eyes / iris / cornea","Layered eye: iris, pupil, corneal IOR and tear film");
         kind(s,"eyes.irisColor","Iris colour","color");
         num(s,"eyes.irisRadius","Iris radius",.15,.75,.01);num(s,"eyes.pupil","Pupil",.05,.95,.01);
         num(s,"eyes.corneaIor","Cornea IOR",1.25,1.45,.001);num(s,"eyes.limbus","Limbus",0,1,.01);
         num(s,"eyes.scleraRedness","Sclera redness",0,1,.01);num(s,"eyes.tearFilm","Tear film",0,1,.01);
         num(s,"eyes.gazeLimitDegrees","Gaze limit (deg)",0,60,1);}
        {auto& s=section("groom","Hair / groom","Deterministic scalp guides and cards; guides x segments <= 262144");
         kind(s,"groom.color","Hair colour","color");
         num(s,"groom.guideCount","Guides",4,8192,1);num(s,"groom.segments","Segments",2,32,1);
         num(s,"groom.lengthMeters","Length (m)",0,2,.01);num(s,"groom.density","Density",0,1,.01);
         num(s,"groom.curl","Curl",0,1,.01);num(s,"groom.clump","Clump",0,1,.01);
         num(s,"groom.melanin","Melanin",0,1,.01);num(s,"groom.roughness","Roughness",.05,1,.01);
         num(s,"groom.stiffness","Stiffness",0,1,.01);num(s,"groom.damping","Damping",0,1,.01);
         num(s,"groom.gravity","Gravity",0,3,.01);num(s,"groom.wind","Wind",0,3,.01);}
        {auto& s=section("anatomy","Face and body proportions","Head, face and upper body shape");
         num(s,"anatomy.heightMeters","Height (m)",.8,2.6,.01);
         for(const char* k:{"headScale","headWidth","jawWidth","jawLength","eyeScale","earScale"}) {
             const std::string key=std::string("anatomy.")+k;num(s,key.c_str(),k,.5,1.8,.01);
         }
         for(const char* k:{"cheekFullness","noseLength","noseWidth","eyeSpacing","browHeight","mouthWidth",
                            "lipFullness","neckLength","shoulderWidth","bodyBuild"}) {
             const std::string key=std::string("anatomy.")+k;num(s,key.c_str(),k,0,1,.01);
         }}
        {auto& s=section("expression","Facial morph / 52 semantic channels","ARKit-order rest weights 0..1, baked into the preview through the shape routes");
         for(std::size_t i=0;i<kChannelCount;++i) {
             const std::string key="expressionRest."+std::to_string(i);
             const std::string label(channelName(Channel(i)));
             num(s,key.c_str(),label.c_str(),0,1,.01);
         }}
        {auto& s=section("routes","Facial morph bindings","ARKit channel -> morph target routes; gain 0..4 (studio.humanCreatorRoute)");(void)s;}
        {auto& s=section("rig","Rig / imported bone names","Named bones the DNA drives; must exist in an imported rig");
         for(const char* k:{"rootBone","headBone","jawBone","leftEyeBone","rightEyeBone"}) {
             const std::string key=std::string("rig.")+k;kind(s,key.c_str(),k,"bone");
         }
         kind(s,"rig.optionalBones","Optional bones","bones");}
        {auto& s=section("rigCorrections","Rig correction authoring","Presentation-only per-node translation overlay, |offset| <= 1 (studio.humanCreatorRig)");(void)s;}
        {auto& s=section("skinWeights","Skin weight authoring","Set one model-relative vertex's weight to a named bone (studio.humanCreatorSkin)");(void)s;}
        return sections;
    }

    // ---- DNA
    const HumanDNA& dna() const noexcept {return session_.dna();}
    uint64_t revision() const noexcept {return revision_;}
    // Set one schema field. Returns false (session unchanged) with error.
    bool set(const std::string& key,const Json& value,std::string* error=nullptr) {
        try {
            Json doc=session_.dna().toJson();
            if(key.rfind("expressionRest.",0)==0) {
                const auto tail=key.substr(15);
                if(tail.empty()||tail.size()>2||tail.find_first_not_of("0123456789")!=std::string::npos)
                    throw std::invalid_argument("Invalid expression channel");
                const std::size_t i=std::stoul(tail);
                if(i>=kChannelCount||!value.isNumber())throw std::invalid_argument("Invalid expression channel value");
                doc.member("expressionRest").at(i)=Json::number(value.toDouble(),6);
            } else {
                if(!knownKey(key))throw std::invalid_argument("Unknown creator field: "+key);
                const auto dot=key.find('.');
                if(dot==std::string::npos)doc.set(key,value);
                else doc.member(key.substr(0,dot)).set(key.substr(dot+1),value);
            }
            const auto next=HumanDNA::fromJson(doc);
            next.validate();
            if(!session_.edit([&](HumanDNA& d){d=next;},error))return false;
            ++revision_;return true;
        }catch(const std::exception& ex) {if(error)*error=ex.what();return false;}
    }
    bool preset(const std::string& id,std::string* error=nullptr) {
        if(!session_.preset(id,error))return false;
        ++revision_;return true;
    }
    bool undo() {if(!session_.undo())return false;++revision_;return true;}
    bool redo() {if(!session_.redo())return false;++revision_;return true;}

    // ---- facial shape routes
    bool setRoute(std::size_t index,const std::string& channel,const std::string& target,
                  double gain,std::string* error=nullptr) {
        Channel c{};
        if(!channelFromName(channel,c)) {if(error)*error="Unknown ARKit channel";return false;}
        if(!std::isfinite(gain)||gain<0||gain>4) {if(error)*error="Route gain must be 0..4";return false;}
        if(!validTarget(target)) {if(error)*error="Invalid morph target name";return false;}
        auto next=routes_;
        if(index==next.size()) {
            if(next.size()>=256) {if(error)*error="Too many routes";return false;}
            next.push_back({c,target,float(gain)});
        } else if(index<next.size()) next[index]={c,target,float(gain)};
        else {if(error)*error="Route index out of range";return false;}
        routes_=std::move(next);++revision_;return true;
    }
    bool removeRoute(std::size_t index) {
        if(index>=routes_.size())return false;
        routes_.erase(routes_.begin()+std::ptrdiff_t(index));++revision_;return true;
    }
    const std::vector<ShapeRoute>& routes() const noexcept {return routes_;}

    // ---- rig corrections (presentation overlay, applied to node world pose)
    bool setRig(const std::string& node,render::float3 offset,std::string* error=nullptr) {
        if(!rig_.setTranslation(node,offset)) {if(error)*error="Invalid rig node or offset (|x|,|y|,|z| <= 1)";return false;}
        ++revision_;return true;
    }
    bool rigUndo() {if(!rig_.undo())return false;++revision_;return true;}
    bool rigRedo() {if(!rig_.redo())return false;++revision_;return true;}
    const RigAuthoring& rigCorrections() const noexcept {return rig_;}

    // ---- skin weights (baked into the private CPU head before upload)
    bool setSkin(std::uint32_t vertex,const std::string& joint,double weight,std::string* error=nullptr) {
        if(!std::isfinite(weight)||!skin_.set(vertex,joint,float(weight))) {
            if(error)*error="Invalid skin weight edit (bone name, weight 0..1)";
            return false;
        }
        ++revision_;return true;
    }
    bool eraseSkin(std::uint32_t vertex) {if(!skin_.erase(vertex))return false;++revision_;return true;}
    bool skinUndo() {if(!skin_.undo())return false;++revision_;return true;}
    bool skinRedo() {if(!skin_.redo())return false;++revision_;return true;}

    // ---- document
    Json toDocument() const {
        Json d=Json::object();d.set("version",Json::number(std::int64_t(1)));
        d.set("dna",session_.dna().toJson());d.set("rigCorrections",rig_.toJson());
        d.set("skinWeights",skin_.toJson());
        Json r=Json::array();
        for(const auto& route:routes_) {
            Json e=Json::object();e.set("channel",Json::string(std::string(channelName(route.channel))));
            e.set("target",Json::string(route.targetName));e.set("gain",Json::number(double(route.gain),6));
            r.push(std::move(e));
        }
        d.set("routes",std::move(r));
        return d;
    }
    // All-or-nothing: on any error the current creator is unchanged.
    bool loadDocument(const Json& d,std::string* error=nullptr) {
        try {
            if(!d.isObject()||d["version"].integer(-1)!=1)throw std::invalid_argument("Creator document version must be 1");
            HumanCreatorSession session;
            if(!session.loadJson(d["dna"],error))return false;
            auto rig=d.has("rigCorrections")?RigAuthoring::fromJson(d["rigCorrections"]):RigAuthoring{};
            auto skin=d.has("skinWeights")&&!d["skinWeights"].isNull()?SkinWeightAuthoring::fromJson(d["skinWeights"]):SkinWeightAuthoring{};
            std::vector<ShapeRoute> routes;
            if(d.has("routes")) {
                if(!d["routes"].isArray()||d["routes"].size()>256)throw std::invalid_argument("Invalid routes");
                for(const auto& e:d["routes"].items()) {
                    Channel c{};const double g=e["gain"].isNumber()?e["gain"].toDouble():-1;
                    if(!e["channel"].isString()||!channelFromName(e.getStr("channel"),c)||
                       !e["target"].isString()||!validTarget(e.getStr("target"))||!std::isfinite(g)||g<0||g>4)
                        throw std::invalid_argument("Invalid facial route");
                    routes.push_back({c,e.getStr("target"),float(g)});
                }
            } else routes=humanCreatorDefaultRoutes();
            session_=std::move(session);rig_=std::move(rig);skin_=std::move(skin);routes_=std::move(routes);
            ++revision_;return true;
        }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
    }

    // Names for the rig / skin / routes sections, from a cheap CPU build.
    Json catalog() const {
        refreshCatalog();
        return catalog_;
    }

    Json info() const {
        Json r=Json::object();
        r.set("revision",Json::number(std::int64_t(revision_)));
        r.set("schema",schema());
        r.set("document",toDocument());
        r.set("presets",Json::parse(R"(["neutral","warm","cool","stylized"])"));
        r.set("canUndo",Json::fromBool(session_.canUndo()));r.set("canRedo",Json::fromBool(session_.canRedo()));
        r.set("rigCanUndo",Json::fromBool(rig_.canUndo()));r.set("rigCanRedo",Json::fromBool(rig_.canRedo()));
        r.set("skinCanUndo",Json::fromBool(skin_.canUndo()));r.set("skinCanRedo",Json::fromBool(skin_.canRedo()));
        r.set("dirty",Json::fromBool(session_.dirty()));
        r.set("catalog",catalog());
        return r;
    }

    // Private, not-yet-uploaded Models for the preview owner thread:
    // procedural head (52 rest channels baked through the routes, skin
    // weights applied) and the groom card model.
    HumanCreatorModels buildModels(GroomQuality quality=GroomQuality::Medium) const {
        auto asset=generateHumanPreview(session_.dna(),quality,HumanMaterialQuality::High);
        if(!asset.head||asset.head->vertices.empty())throw std::runtime_error("VIVID Creator: generated human has no head");
        bakeRest(asset,*asset.head);
        HumanCreatorModels out;
        if(!skin_.edits().empty())out.skinVerticesChanged=skin_.apply(*asset.head).verticesChanged;
        out.head=std::move(asset.head);out.hair=std::move(asset.hair);
        return out;
    }

private:
    static bool validTarget(const std::string& t) {
        return !t.empty()&&t.size()<=128&&t.find_first_of("\n\r",0)==std::string::npos;
    }
    static bool knownKey(const std::string& key) {
        static const Json keys=[]{
            Json all=Json::array();
            const Json sections=schema();
            for(const auto& s:sections.items())for(const auto& f:s["fields"].items())all.push(f["key"]);
            return all;
        }();
        for(const auto& k:keys.items())if(k.str()==key)return true;
        return false;
    }
    void bakeRest(const HumanPreviewAsset& asset,render::Model& head) const {
        bool any=false;for(float w:session_.dna().expressionRest)any=any||w>0;
        if(!any||asset.facialShapes.targets()==0)return;
        std::vector<ShapeRoute> live;
        for(const auto& r:routes_){std::size_t t=0;if(asset.facialShapes.findTarget(r.targetName,t))live.push_back(r);}
        const auto binding=FaceShapeBinding::compile(asset.facialShapes,live);
        FaceFrame frame;
        for(std::size_t i=0;i<frame.values.size();++i)frame.values[i]=session_.dna().expressionRest[i];
        std::vector<float> weights(asset.facialShapes.targets(),0.f);
        if(!binding.evaluate(frame,weights))throw std::runtime_error("VIVID Creator: facial route evaluation failed");
        std::vector<MorphVertex> base(head.vertices.size()),out(head.vertices.size());
        for(std::size_t i=0;i<head.vertices.size();++i) {
            const auto& v=head.vertices[i];
            base[i].position={v.pos.x,v.pos.y,v.pos.z};base[i].normal={v.nrm.x,v.nrm.y,v.nrm.z};
            base[i].tangent={v.tangent.x,v.tangent.y,v.tangent.z};
        }
        if(asset.facialShapes.vertices()!=base.size()||!asset.facialShapes.evaluate(base,weights,out))
            throw std::runtime_error("VIVID Creator: rest expression bake failed");
        for(std::size_t i=0;i<head.vertices.size();++i) {
            auto& v=head.vertices[i];
            v.pos={out[i].position[0],out[i].position[1],out[i].position[2]};
            v.nrm={out[i].normal[0],out[i].normal[1],out[i].normal[2]};
            v.tangent={out[i].tangent[0],out[i].tangent[1],out[i].tangent[2],v.tangent.w};
        }
    }
    void refreshCatalog() const {
        if(catalogRevision_==revision_&&catalog_.isObject())return;
        Json c=Json::object();
        try {
            auto asset=generateHumanPreview(session_.dna(),GroomQuality::Low,HumanMaterialQuality::Low);
            Json nodes=Json::array();
            for(const auto& n:asset.head->nodes)if(!n.name.empty())nodes.push(Json::string(n.name));
            Json targets=Json::array();
            for(std::size_t i=0;i<asset.facialShapes.targets();++i)targets.push(Json::string(std::string(asset.facialShapes.targetName(i))));
            Json channels=Json::array();
            for(std::size_t i=0;i<kChannelCount;++i)channels.push(Json::string(std::string(channelName(Channel(i)))));
            c.set("nodes",std::move(nodes));c.set("targets",std::move(targets));c.set("channels",std::move(channels));
            c.set("vertices",Json::number(std::int64_t(asset.head->vertices.size())));
            c.set("error",Json::string(""));
        }catch(const std::exception& ex) {
            c.set("nodes",Json::array());c.set("targets",Json::array());c.set("channels",Json::array());
            c.set("vertices",Json::number(std::int64_t(0)));c.set("error",Json::string(ex.what()));
        }
        catalog_=std::move(c);catalogRevision_=revision_;
    }
    HumanCreatorSession session_;
    RigAuthoring rig_;
    SkinWeightAuthoring skin_;
    std::vector<ShapeRoute> routes_;
    uint64_t revision_=1;
    mutable Json catalog_;
    mutable uint64_t catalogRevision_=0;
};

// ---- project storage: <project>/humans/<name>.json (atomic, no symlinks)
inline bool validHumanCreatorName(const std::string& name) {
    if(name.empty()||name.size()>64)return false;
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
inline std::filesystem::path humanCreatorFolder(const std::string& projectRoot,bool create=false) {
    namespace fs=std::filesystem;
    if(projectRoot.empty())throw std::runtime_error("Open a project first");
    const fs::path root=fs::weakly_canonical(fs::path(projectRoot));
    if(!fs::is_directory(root))throw std::runtime_error("Invalid project root");
    const fs::path folder=root/"humans";
    std::error_code ec;
    if(fs::is_symlink(fs::symlink_status(folder,ec)))throw std::runtime_error("Humans folder cannot be a symlink");
    if(!fs::exists(folder)) {
        if(!create)return folder;
        if(!fs::create_directory(folder))throw std::runtime_error("Cannot create humans directory");
    }
    if(!fs::is_directory(folder))throw std::runtime_error("Humans folder is not a real directory");
    return folder;
}
inline Json listHumanCreators(const std::string& projectRoot) {
    namespace fs=std::filesystem;
    std::vector<std::string> names;
    const auto folder=humanCreatorFolder(projectRoot);
    if(fs::exists(folder))
        for(const auto& e:fs::directory_iterator(folder)) {
            if(!e.is_regular_file()||e.is_symlink()||e.path().extension()!=".json")continue;
            const auto n=e.path().stem().string();
            if(validHumanCreatorName(n))names.push_back(n);
            if(names.size()>256)throw std::runtime_error("Too many saved humans");
        }
    std::sort(names.begin(),names.end());
    Json list=Json::array();for(const auto& n:names)list.push(Json::string(n));
    Json r=Json::object();r.set("humans",std::move(list));return r;
}
inline Json readHumanCreator(const std::string& projectRoot,const std::string& name) {
    namespace fs=std::filesystem;
    if(!validHumanCreatorName(name))throw std::invalid_argument("Invalid human name");
    const auto file=humanCreatorFolder(projectRoot)/(name+".json");
    if(fs::is_symlink(fs::symlink_status(file))||!fs::is_regular_file(file)||fs::file_size(file)>4u*1024u*1024u)
        throw std::runtime_error("Saved human unavailable or too large");
    std::ifstream in(file,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),{});
    return Json::parse(bytes);
}
// Never truncates or deletes the last-good file: exclusive staging + rename.
inline void saveHumanCreator(const std::string& projectRoot,const std::string& name,const Json& doc) {
    if(!validHumanCreatorName(name))throw std::invalid_argument("Invalid human name");
    const auto folder=humanCreatorFolder(projectRoot,true);
    const std::string filename=name+".json",pending=filename+".pending";
    const int dir=::open(folder.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(dir<0)throw std::runtime_error("Cannot open humans folder");
    int fd=-1;
    try {
        fd=::openat(dir,pending.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0)throw std::runtime_error("Cannot create exclusive human staging file");
        const std::string bytes=doc.dump(2)+"\n";
        std::size_t off=0;
        while(off<bytes.size()) {
            const ssize_t n=::write(fd,bytes.data()+off,bytes.size()-off);
            if(n<=0)throw std::runtime_error("Failed writing human staging file");
            off+=std::size_t(n);
        }
        if(::fsync(fd)!=0)throw std::runtime_error("Human staging fsync failed");
        const int closing=fd;fd=-1;
        if(::close(closing)!=0)throw std::runtime_error("Failed closing human staging file");
        if(::renameat(dir,pending.c_str(),dir,filename.c_str())!=0)throw std::runtime_error("Cannot publish human file");
        (void)::fsync(dir);(void)::close(dir);
    }catch(...) {
        if(fd>=0)(void)::close(fd);
        (void)::unlinkat(dir,pending.c_str(),0);(void)::close(dir);
        throw;
    }
}

} // namespace aiverse::vivid
