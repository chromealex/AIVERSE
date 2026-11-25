#pragma once
// Studio-specific adapter: use the existing AssetIndex/.meta GUID identity.
// The asset loader still owns decoding, GPU upload and symlink/path safety.
// Never create a second GUID database in the building generator.
#include "apps/studio-native/StudioAssetCatalog.h"
#include "engine/buildings/BuildingApertureTextureAssets.h"
#include <functional>
#include <string>
#include <utility>

namespace aiverse::studio {

// Host-supplied texture importer reads a catalog entry through its existing
// project content sandbox and produces a render::Image CPU representation.
// Must reject unsafe paths and preserve importer compression / sRGB settings.
using StudioBuildingTextureLoad=std::function<bool(
    const StudioAssetEntry&,render::Image&,std::string&)>;

inline building::BuildingApertureTextureResolver::Resolve
studioBuildingTextureResolver(const StudioAssetCatalog& catalog,
                              StudioBuildingTextureLoad load){
    return [&catalog,load=std::move(load)](
        const std::string& guid,render::Image& out,
        std::string& error)->bool{
        if(!studioValidGuid(guid)||!load){
            error="Invalid Assets texture GUID or missing importer";return false;
        }
        const auto entry=catalog.byGuid(guid);
        if(!entry||entry->directory||entry->importer!="texture"){
            error="Assets GUID does not reference an indexed texture";return false;
        }
        // catalog index entries are content-relative. Do not join
        // unchecked paths here: the canonical asset-loader owns that.
        return load(*entry,out,error);
    };
}

} // namespace aiverse::studio
