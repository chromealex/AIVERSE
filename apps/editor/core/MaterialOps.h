// Material operations of the editor core (MCP agents and the Materials window): the game's shared
// material library (materials.json), applying library or inline materials to a type's view (by
// model material name or by node), and procedural textures written as PNG into the game folder.
// Format: docs/models.md "Material library".
#pragma once
#include "Workspace.h"
#include <filesystem>
#include <limits>
#include <map>
#include "engine/render/Model.h"
#include "engine/render/graph/TextureGraph.h"
#include <span>

namespace aiverse::editor {

void registerMaterialOps(Workspace& ws);
render::texture_graph::ImageResolver textureGraphImageResolver(const Workspace& ws);
// Native owner only: publish completed validated pixels as a new texture asset.
Json publishTextureGraphImage(Workspace& ws,const std::string& name,const std::string& output,const render::Image& image);
Json publishTextureGraphCompressed(Workspace& ws,const std::string& name,const std::string& output,const render::Image& image,const std::string& format,bool mipmaps);
Json publishTextureGraphBcBlocks(Workspace& ws,const std::string& name,const std::string& output,int size,const std::string& format,std::span<const uint8_t> blocks);
Json publishTextureGraphRadiance(Workspace& ws,const std::string& name,int size,std::span<const float> rgba);
Json publishTextureGraphRadianceFormat(Workspace& ws,const std::string& name,int size,std::span<const float> rgba,const std::string& format,bool mipmaps);
Json publishTextureGraphEnvironment(Workspace& ws,const std::string& name,int size,std::span<const float> rgba);
// Images/backends in native Program::outputs sorted order; trusted native owner only.
Json publishTextureGraphSet(Workspace& ws,const std::string& name,const Json& graph,const Json& overrides,const std::vector<render::Image>& images,const std::vector<std::string>& backends);

// materials.json of the open game ({"materials": {...}}); empty object when missing.
Json materialFilePaths(const Workspace& ws);
Json resolveMaterialLibraryIndex(const std::filesystem::path& root,const Json& index,
                                 size_t maxFileBytes=std::numeric_limits<size_t>::max());
Json loadMaterialLibrary(const Workspace& ws);
void saveMaterialLibrary(Workspace& ws, const Json& lib, const std::string& what,
                         const std::map<std::string,std::string>& relocated = {});   // throws on failure
// Keys a material understands (override keys + "base" / "use" / "replace" / "description").
const std::vector<std::string>& materialKeys();
// Problems of one material definition ("unknown key 'colour'", "metallic must be 0..1").
std::vector<std::string> checkMaterial(const Json& m);

} // namespace aiverse::editor
