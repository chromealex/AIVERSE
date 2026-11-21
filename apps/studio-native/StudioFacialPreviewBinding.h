// Studio facial-preview asset binding for the shared MotionWeave palette.
// Native runtime readiness and independently verified visual quality remain
// separate; browser inputs cannot declare GPU acceptance.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioFacialRigDocument.h"
#include "engine/render/Model.h"
#include "engine/character/vivid/HumanAppearance.h"
#include "engine/character/vivid/HumanFaceShape.h"
#include "engine/character/vivid/HumanStudioLighting.h"
#include "engine/character/vivid/HumanPreviewAnimation.h"
#include "engine/character/vivid/HumanWardrobe.h"
#include <optional>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::studio {

class StudioFacialPreviewBinding final {
public:
    vivid::HumanStudioLighting studioLighting;
    vivid::HumanPreviewAnimation previewAnimation;
    // Character-editor camera framing request (face | torso | full); the
    // native scene applies it once per serial, then the artist orbits freely.
    std::string cameraView="full";
    bool showRuler=false; // height ruler beside the character
    uint64_t cameraSerial=0;
    void reset() {
        studioLighting={};previewAnimation={};
        appearance_.reset();face_.reset();appearanceOnly_=false;compare_=false;wardrobe_=Json();
        creator_=Json();
        guid_.clear();
        path_.clear();
        draft_=Json();
        values_.clear();
        vectors_.clear();
        gpuReady_=false;
        gpuError_.clear();
        ++revision_;
    }
    const std::optional<vivid::HumanAppearance>& appearance() const {return appearance_;}
    const std::optional<vivid::HumanFaceShape>& face() const {return face_;}
    bool appearanceOnly() const {return appearanceOnly_;}
    // Live try-on: wardrobe recipe assembled into the preview (no bake).
    const Json& wardrobe() const {return wardrobe_;}
    Json setWardrobe(const Json& recipe) {
        if(!bound() || creatorMode())throw std::runtime_error("Покажите персонажа, чтобы примерять одежду");
        if(!recipe.isNull())vivid::validateHumanWardrobe(recipe);
        if(!(recipe==wardrobe_)){wardrobe_=recipe;gpuReady_=false;++revision_;}
        return describe();
    }
    // Forces a full re-import of the shown model (e.g. its clip sidecar changed).
    uint64_t reloadSerial() const {return reloadSerial_;}
    Json reload(){++reloadSerial_;gpuReady_=false;++revision_;return describe();}
    // Before/after: draw the untouched source model while held.
    bool compareOriginal() const {return compare_;}
    Json setCompareOriginal(bool on) {
        if(compare_!=on){compare_=on;gpuReady_=false;++revision_;}
        return describe();
    }
    Json bindAppearance(const StudioAssetCatalog& catalog,const std::string& guid,
                        uint64_t expected,const Json& look,const Json* face=nullptr) {
        if(expected!=revision_)throw std::runtime_error("Stale character selection; refresh first");
        const auto path=checkedModelPath(catalog,guid);
        const auto candidate=vivid::HumanAppearance::fromJson(look);
        auto candidateFace=guid_==guid?face_:std::optional<vivid::HumanFaceShape>{};
        if(face){candidateFace.reset();if(!face->isNull())candidateFace=vivid::HumanFaceShape::fromJson(*face); }
        if(guid_!=guid || !bound()) {
            draft_=Json();values_.clear();vectors_.clear();appearanceOnly_=true;compare_=false;wardrobe_=Json();
        }
        creator_=Json();
        guid_=guid;path_=path;appearance_=candidate;face_=candidateFace;
        gpuReady_=false;gpuError_.clear();++revision_;return describe();
    }
    bool bound() const {return !guid_.empty();}
    // Digital Human Creator mode: the native preview regenerates the
    // procedural VIVID human from this validated creator document (copy,
    // owner-thread safe). No asset path, no facial rig document.
    static constexpr const char* kCreatorGuid="vivid-creator";
    Json bindCreator(const Json& document) {
        if(!document.isObject() || document["version"].integer(-1)!=1)
            throw std::invalid_argument("Invalid Human Creator document");
        if(!creatorMode()) {
            draft_=Json();values_.clear();vectors_.clear();
            appearance_.reset();face_.reset();
        }
        appearanceOnly_=true;creator_=document;
        guid_=kCreatorGuid;path_.clear();
        gpuReady_=false;gpuError_.clear();++revision_;return describe();
    }
    // Viewport sculpt: one brush stroke becomes one face landmark on the
    // bound character (requires face meshes chosen in the Face tab).
    Json sculptStroke(const vivid::HumanFaceShape::Landmark& stroke) {
        if(!bound() || creatorMode())
            throw std::runtime_error("Sculpt needs a project character model in the preview");
        if(!face_ || face_->meshes.empty())
            throw std::runtime_error("Сначала выберите меши лица: «Лицо → Пропорции и импорт»");
        auto next=*face_;
        if(next.landmarks.size()>=1024)throw std::runtime_error("Предел скульпта: 1024 штриха — соберите персонажа, чтобы продолжить");
        next.landmarks.push_back(stroke);
        face_=vivid::HumanFaceShape::fromJson(next.toJson());
        gpuReady_=false;gpuError_.clear();++revision_;return describe();
    }
    // Mirror stroke across the face's X=0.5 plane (symmetric sculpt).
    static vivid::HumanFaceShape::Landmark mirrored(vivid::HumanFaceShape::Landmark l) {
        l.from.x=1-l.from.x;l.to.x=1-l.to.x;return l;
    }
    Json sculptClear() {
        if(!face_ || face_->landmarks.empty())throw std::runtime_error("No sculpt strokes to reset");
        face_->landmarks.clear();
        gpuReady_=false;gpuError_.clear();++revision_;return describe();
    }
    Json sculptUndo() {
        if(!face_ || face_->landmarks.empty())throw std::runtime_error("No sculpt stroke to undo");
        face_->landmarks.pop_back();
        gpuReady_=false;gpuError_.clear();++revision_;return describe();
    }
    bool creatorMode() const noexcept {return guid_==kCreatorGuid && creator_.isObject();}
    const Json& creatorDocument() const noexcept {return creator_;}
    uint64_t revision() const {return revision_;}
    const std::string& guid() const noexcept{return guid_;}
    const std::string& path() const noexcept{return path_;}
    const Json& document() const noexcept{return draft_;}
    const std::map<std::string,float>& scalars() const noexcept{return values_;}
    const std::map<std::string,std::array<float,3>>& vectors() const noexcept{return vectors_;}
    // Called solely by the native Metal owner after a real render attempt.
    // Ready != QA-verified pixels; external HTML cannot set this flag.
    // Animation clips of the shown model (from the GPU preview), for the
    // "project animations" list; preview clips are excluded.
    void setClips(const std::vector<std::string>& names){if(names!=clips_)clips_=names;}
    void setBaseHeight(float h){baseHeight_=std::isfinite(h)&&h>0?h:0.f;}
    void setGpuStatus(bool ready,const std::string& reason) {
        gpuReady_=bound() && ready;
        gpuError_=reason;
    }

    // Use ONLY native catalog GUIDs, not browser-provided file paths.
    // File must still be inside project/content with no symlink segments.
    static std::string checkedModelPath(
            const StudioAssetCatalog& catalog,const std::string& guid) {
        namespace fs=std::filesystem;
        if(!studioValidGuid(guid))
            throw std::invalid_argument("Invalid facial model GUID");
        const auto entry=catalog.byGuid(guid);
        if(!entry || entry->directory || entry->importer!="model")
            throw std::runtime_error("Facial model GUID missing or is not a model");
        const fs::path relative(entry->path);
        if(relative.empty() || relative.is_absolute())
            throw std::runtime_error("Invalid relative model asset path");
        for(const auto& part:relative)
            if(part.empty() || part=="." || part=="..")
                throw std::runtime_error("Unsafe model asset path");
        const auto ext=relative.extension().string();
        if(ext!=".glb" && ext!=".gltf" && ext!=".fbx")
            throw std::runtime_error("Facial preview requires glTF/GLB/FBX");
        fs::path check=studioContentRoot(catalog.project());
        for(const auto& part:relative) {
            check/=part;
            std::error_code ec;
            const auto status=fs::symlink_status(check,ec);
            if(ec || fs::is_symlink(status))
                throw std::runtime_error("Symlink or unavailable facial model path");
        }
        if(!fs::is_regular_file(fs::symlink_status(check)))
            throw std::runtime_error("Facial model is not a regular asset");
        return entry->path; // content-relative only; never expose full path
    }

    Json bind(const StudioAssetCatalog& catalog,const std::string& guid,
              const Json& draft) {
        std::string diagnostic;
        if(!validStudioFacialRigDraft(draft,&diagnostic))
            throw std::invalid_argument("Invalid facial rig: "+diagnostic);
        const std::string path=checkedModelPath(catalog,guid);
        std::map<std::string,float> defaults;
        std::map<std::string,std::array<float,3>> vectors;
        for(const auto& morph:draft["morphs"].items()) {
            const std::string name=morph.getStr("parameter");
            defaults.try_emplace(name,0.f);
        }
        for(const auto& op:draft["rig"].items()) {
            const auto name=op.getStr("weightParameter");
            if(!name.empty())defaults.try_emplace(name,1.f);
            for(const char* key:{"target","pole","translationParameter",
                                 "rotationParameter","scaleParameter"}) {
                const auto param=op.getStr(key);
                if(!param.empty())vectors.try_emplace(param,
                    std::array<float,3>{0.f,0.f,0.f});
            }
        }
        for(const auto& item:vectors)
            if(defaults.contains(item.first))
                throw std::invalid_argument("Facial input declared as both scalar and vector: "+item.first);
        // Publish the COMPLETE new binding only after native GUID and
        // authoring schema checks. This is NOT a Model/Graph/GPU compile.
        if(guid_!=guid){appearance_.reset();face_.reset();}
        appearanceOnly_=false;
        creator_=Json();
        guid_=guid;
        path_=path;
        draft_=draft;
        values_=std::move(defaults);
        vectors_=std::move(vectors);
        gpuReady_=false;
        gpuError_.clear();
        ++revision_;
        return describe();
    }

    Json controls(uint64_t expectedRevision,const Json& values,
                  const Json& vectors) {
        if(!bound() || revision_!=expectedRevision)
            throw std::runtime_error("Stale facial preview selection; bind model again");
        if(!values.isObject() || values.size()>256 ||
           (!vectors.isNull() && !vectors.isObject()) ||
           vectors.size()>256)
            throw std::invalid_argument("Facial inputs must be bounded typed objects");
        auto candidate=values_;
        auto vectorCandidate=vectors_;
        for(const auto& [name,value]:values.members()) {
            const auto it=candidate.find(name);
            if(it==candidate.end() || !value.isNumber())
                throw std::invalid_argument("Unknown/non-numeric facial input: "+name);
            const double number=value.toDouble();
            if(!std::isfinite(number) || number<0. || number>1.)
                throw std::invalid_argument("Facial preview input must be 0..1");
            it->second=float(number);
        }
        for(const auto& [name,entry]:vectors.members()) {
            auto it=vectorCandidate.find(name);
            if(it==vectorCandidate.end() || !entry.isArray() ||
               entry.size()!=3)
                throw std::invalid_argument("Unknown/non-vector facial IK input: "+name);
            std::array<float,3> next{};
            for(size_t axis=0;axis<3;++axis) {
                const auto& coord=entry[axis];
                if(!coord.isNumber() || !std::isfinite(coord.toDouble()) ||
                   std::abs(coord.toDouble())>1000000.)
                    throw std::invalid_argument("Nonfinite/out-of-range facial IK vector");
                next[axis]=float(coord.toDouble());
            }
            it->second=next;
        }
        values_=std::move(candidate);
        vectors_=std::move(vectorCandidate);
        return describe();
    }
    Json describe() const {
        Json result=Json::object();
        result.set("appearanceOnly",Json::fromBool(appearanceOnly_));
        result.set("compare",Json::fromBool(compare_));
        result.set("tryOn",Json::fromBool(!wardrobe_.isNull()));
        result.set("baseHeight",Json::number(double(baseHeight_),4));
        result.set("ruler",Json::fromBool(showRuler));
        {auto c=Json::array();for(const auto& n:clips_)c.push(Json::string(n));result.set("clips",std::move(c));}
        result.set("appearance",appearance_?appearance_->toJson():Json());
        result.set("creator",Json::fromBool(creatorMode()));
        result.set("bound",Json::fromBool(bound()));
        result.set("guid",Json::string(guid_));
        result.set("path",Json::string(path_));
        result.set("revision",Json::number(int64_t(revision_)));
        Json values=Json::object();
        for(const auto& [name,value]:values_)
            values.set(name,Json::number(double(value),6));
        result.set("values",std::move(values));
        Json vectors=Json::object();
        for(const auto& [name,value]:vectors_) {
            Json coords=Json::array();
            for(float component:value)
                coords.push(Json::number(double(component),6));
            vectors.set(name,std::move(coords));
        }
        result.set("vectors",std::move(vectors));
        result.set("runtimeReady",Json::fromBool(gpuReady_));
        result.set("runtimeVerified",Json::fromBool(false));
        result.set("rendererChannelCapacity",
                   Json::number(int64_t(render::kMaxMorphTargets)));
        result.set("reason",Json::string(gpuReady_?
            "Native Metal Renderer3D submitted a facial frame; pixel/backend QA not run":
            (gpuError_.empty()?
            "Model selected; waiting for native 64-channel import and facial GPU frame":
            gpuError_)));
        return result;
    }
private:
    std::string guid_,path_;
    Json draft_;
    Json creator_; // Human Creator document while guid_==kCreatorGuid
    std::optional<vivid::HumanAppearance> appearance_;
    std::optional<vivid::HumanFaceShape> face_;
    bool appearanceOnly_=false;
    bool compare_=false;
    Json wardrobe_;
    std::vector<std::string> clips_;
    float baseHeight_=0;
    uint64_t reloadSerial_=0;
    std::map<std::string,float> values_;
    std::map<std::string,std::array<float,3>> vectors_;
    uint64_t revision_=0;
    bool gpuReady_=false;
    std::string gpuError_;
};

} // namespace aiverse::studio
