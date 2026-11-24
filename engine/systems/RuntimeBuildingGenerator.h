#pragma once
// Build-time-independent procedural building generation. The same CPU code
// runs in native game clients, dedicated runtimes, mobile and Web builds.
// No Studio/editor, disk, Metal/Vulkan/GL or gameplay-genre dependency.
// Register a bundled style JSON at load, call generate() per seed/size, then
// hand mesh/collision/gates to the game's presentation and physics owners.
//
// IMPORTANT: register/clear only before concurrent worker generation; do not
// mutate the registry while other threads use it.
#include "BuildingFeatureGenerator.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::systems {

struct RuntimeBuildingRequest {
    std::string style;                    // registered style ID
    std::string function;                 // structure kind in that style
    uint64_t seed=1;
    StructureSize size{};                 // metres; all-zero = authored style size
    Vec3 origin{};                        // position of visible minimum corner
    uint32_t yaw=0;                       // quarter turns: 0,1,2,3
    // New genre-free functionality. When composed, 'function' is not needed.
    bool composeFeatures=false;
    std::vector<BuildingFeature> features;
};

enum class RuntimeBuildingError : uint8_t {
    None, InvalidInput, UnknownStyle, UnknownFunction, FitFailed, BudgetExceeded
};

struct RuntimeBuildingResult {
    RuntimeBuildingError error=RuntimeBuildingError::None;
    std::string message;
    StyledStructure building;             // CPU mesh, style materials, AABB, gate points
    std::vector<BuildingFeatureAnchor> featureAnchors; // doors/windows/roof controls
    BuildingStructureGraph structureGraph; // room cells, partition passages and all source solids
    explicit operator bool() const noexcept {
        return error==RuntimeBuildingError::None&&!building.mesh.vertices.empty();
    }
};

// Owns parsed, immutable style data; stores NO generated GPU resources or ECS
// objects. Games decide whether/when to cache instances or publish them.
// Returned geometry is copied/owned by the caller and survives clear().
class RuntimeBuildingGenerator {
public:
    static constexpr size_t kMaximumStyleBytes=1024u*1024u;
    static constexpr size_t kMaximumVertices=400000u;
    static constexpr size_t kMaximumTriangles=150000u;

    bool registerStyleJson(const std::string& source,std::string* error=nullptr) {
        if(source.empty()||source.size()>kMaximumStyleBytes) {
            if(error)*error="Building style must be between 1 byte and 1 MiB";
            return false;
        }
        try {
            return registerStyle(readBuildingStyle(Json::parse(source)),error);
        } catch(const std::exception& e) {
            if(error)*error=e.what();
            return false;
        }
    }

    // A packaged-game resource callback (Addressables/PAK/zip/HTTP/file
    // abstraction) supplies UTF-8 bytes. This never opens an OS file.
    using AssetReader=std::function<bool(const std::string&,std::string&)>;
    bool registerStyleAsset(const std::string& assetId,const AssetReader& read,
                            std::string* error=nullptr) {
        std::string bytes;
        if(assetId.empty()||assetId.size()>256||!read||!read(assetId,bytes)) {
            if(error)*error="Failed to read packaged building-style asset: "+assetId;
            return false;
        }
        return registerStyleJson(bytes,error);
    }

    bool registerStyle(BuildingStyle style,std::string* error=nullptr) {
        if(style.id.empty()||style.id.size()>64||style.materials.empty()||
           style.shell<0||style.roof<0) {
            if(error)*error="Invalid runtime building style (id, structure or materials)";
            return false;
        }
        if(styles_.count(style.id)) {
            if(error)*error="Building style already registered: "+style.id;
            return false;
        }
        styles_.emplace(style.id,std::move(style));
        if(error)error->clear();
        return true;
    }

    const BuildingStyle* style(const std::string& id) const noexcept {
        auto it=styles_.find(id);
        return it==styles_.end()?nullptr:&it->second;
    }

    std::vector<std::string> styleIds() const {
        std::vector<std::string> ids;
        ids.reserve(styles_.size());
        for(const auto& [id,_]:styles_)ids.push_back(id);
        return ids;
    }

    void clear() noexcept { styles_.clear(); }
    size_t styleCount() const noexcept { return styles_.size(); }

    RuntimeBuildingResult generate(const RuntimeBuildingRequest& request) const {
        RuntimeBuildingResult result;
        auto fail=[&](RuntimeBuildingError code,const std::string& message) {
            result.error=code;result.message=message;
            return result;
        };
        if(request.yaw>3||(!request.composeFeatures&&request.function.empty()))
            return fail(RuntimeBuildingError::InvalidInput,"Invalid building orientation or function");
        const bool sized=request.size.width!=0||request.size.height!=0||request.size.depth!=0;
        if(sized&&(!std::isfinite(request.size.width)||!std::isfinite(request.size.height)||
                   !std::isfinite(request.size.depth)||request.size.width<=0||
                   request.size.height<=0||request.size.depth<=0))
            return fail(RuntimeBuildingError::InvalidInput,"Building size requires three finite positive dimensions");
        auto it=styles_.find(request.style);
        if(it==styles_.end())
            return fail(RuntimeBuildingError::UnknownStyle,"Unknown building style: "+request.style);
        if(request.composeFeatures){
            if(!sized)return fail(RuntimeBuildingError::InvalidInput,
                "Feature-based buildings require explicit width, height and depth");
            FunctionalBuildingRecipe recipe;
            recipe.width=request.size.width;
            recipe.height=request.size.height;
            recipe.depth=request.size.depth;
            recipe.seed=request.seed;
            recipe.features=request.features;
            return generateRecipe(request.style,recipe,request.origin,request.yaw);
        }
        const auto* kind=it->second.structureKind(request.function);
        if(!kind)
            return fail(RuntimeBuildingError::UnknownFunction,"Unknown building function: "+request.function);
        if(sized && (request.size.width < std::max(8.f,float(kind->doors)*4.f+4.f)||
                     request.size.width>160.f||request.size.depth<8.f||
                     request.size.depth>160.f||request.size.height<5.f||
                     request.size.height>80.f))
            return fail(RuntimeBuildingError::InvalidInput,
                "Requested building dimensions are outside style/geometry limits");
        try {
            result.building=dressStructure(it->second,request.function,request.seed,
                                           request.origin,request.yaw,request.size);
            if(result.building.mesh.vertices.empty())
                return fail(RuntimeBuildingError::FitFailed,
                    result.building.fitError.empty()?"Building geometry could not fit":result.building.fitError);
            if(result.building.mesh.vertices.size()>kMaximumVertices||
               result.building.mesh.triangles()>kMaximumTriangles)
                return fail(RuntimeBuildingError::BudgetExceeded,"Generated building exceeds runtime mesh budget");
            return result;
        } catch(const std::exception& e) {
            return fail(RuntimeBuildingError::FitFailed,e.what());
        }
    }

// A recipe has no required role and can be reused with any registered
    // architectural style. Mesh/colliders and semantic anchors share the
    // same shape features, independent of appearance and genre.
    RuntimeBuildingResult generateRecipe(const std::string& styleId,
                                         const FunctionalBuildingRecipe& recipe,
                                         Vec3 origin={},uint32_t yaw=0) const {
        RuntimeBuildingResult result;
        auto it=styles_.find(styleId);
        if(it==styles_.end()){
            result.error=RuntimeBuildingError::UnknownStyle;
            result.message="Unknown building style: "+styleId;
            return result;
        }
        // Game builds own lift platforms as movable visual/physics actors.
        // Keep the generated building Model and static collision free of
        // duplicated, immovable cabin geometry. Studio GLB export retains
        // the initial platform through the default generator argument.
        auto built=generateFunctionalBuilding(it->second,recipe,origin,yaw,false,false);
        const bool valid=bool(built);
        result.featureAnchors=std::move(built.anchors);
        result.structureGraph=std::move(built.graph);
        result.building=std::move(built.building);
        if(!valid){
            result.error=RuntimeBuildingError::InvalidInput;
            result.message=built.error;
            return result;
        }
        if(result.building.mesh.vertices.size()>kMaximumVertices||
           result.building.mesh.triangles()>kMaximumTriangles){
            result.error=RuntimeBuildingError::BudgetExceeded;
            result.message="Generated recipe exceeds runtime geometry budget";
        }
        return result;
    }

private:
    std::map<std::string,BuildingStyle> styles_;
};

} // namespace aiverse::lib::systems
