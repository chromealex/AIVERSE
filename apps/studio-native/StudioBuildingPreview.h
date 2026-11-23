#pragma once
// CPU-only preparation of one TEMPORARY building model for the existing
// StudioScene renderer. Does not write GLB, recipe, .meta or scene entities.
// StudioScene::setBuildingPreview owns GPU upload/retirement.
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::studio {
inline std::unique_ptr<render::Model> studioBuildingPreviewCpuModel(
    const std::filesystem::path& engine,
    const std::filesystem::path& project,const Json& params){
    namespace fs=std::filesystem;
    const auto req=buildingStudioDetail::request(params);
    auto geometry=buildingStudioDetail::produce(engine,req);
    if(!req.materialBindings.empty()){
        if(project.empty())throw std::runtime_error(
            "Open project before using shared material overrides");
        applyProjectMaterials(project,req,geometry);
    }
    StudioAssetCatalog catalog(project);
    if(!project.empty())catalog.rebuild();
    const auto root=project.empty()?fs::path{}:studioContentRoot(project);
    const building::BuildingImageByGuid resolve=
        [&](const std::string& guid,render::Image& image,std::string& issue){
            if(project.empty()){
                issue="Imported building texture requires an open project";
                return false;
            }
            const auto record=catalog.byGuid(guid);
            if(!record||record->directory||record->importer!="texture"){
                issue="Building preview texture GUID missing in Assets";
                return false;
            }
            const fs::path relative(record->path);
            if(relative.empty()||relative.is_absolute()){
                issue="Invalid project texture path";return false;
            }
            fs::path file=root;
            for(const auto& part:relative){
                if(part=="."||part==".."||part.empty()){
                    issue="Unsafe project texture path";return false;
                }
                file/=part;
                std::error_code ec;
                const auto s=fs::symlink_status(file,ec);
                if(ec||fs::is_symlink(s)){
                    issue="Texture source unavailable or linked";return false;
                }
            }
            std::error_code ec;
            if(!fs::is_regular_file(file,ec)||ec||
               fs::file_size(file,ec)>16u*1024u*1024u||ec){
                issue="Texture source unavailable or over 16 MiB";
                return false;
            }
            std::ifstream in(file,std::ios::binary);
            if(!in){issue="Cannot read building texture";return false;}
            const std::string bytes{
                std::istreambuf_iterator<char>{in},
                std::istreambuf_iterator<char>{}};
            if(in.bad()||!render::decodeImage(bytes,image)||
               image.width<=0||image.height<=0||
               image.width>4096||image.height>4096){
                issue="Invalid CPU-decodable PBR texture";return false;
            }
            return true;
        };
    std::string error;
    auto result=building::runtimeTexturedBuildingModel(
        geometry,resolve,"studio:temporary-building",&error);
    if(!result)throw std::runtime_error(error.empty()?
        "Cannot construct temporary 3D building Model":error);
    result->batching.mergeRigid=false;
    result->batching.autoLod=false;
    result->batching.geometryPoolPolicy=
        render::Model::Batching::GeometryPoolPolicy::Dedicated;
    return result;
}
} // namespace aiverse::studio
