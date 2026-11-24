#include "MaterialOps.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "engine/render/Model.h"
#include "engine/render/TextureBcEncode.h"
#include "engine/render/TextureBcGpu.h"
#include "engine/render/DdsTexture.h"
#include "tools/aiversebuild/TextureCompress.h"
#include "engine/render/ExrTexture.h"
#include "engine/render/CameraExr.h"
#include "engine/render/EnvironmentIbl.h"
#include "engine/render/TextureGen.h"
#include "engine/render/graph/TextureGraph.h"
#include "engine/render/graph/TextureGraphContainerSource.h"
#include "engine/render/graph/TextureGraphTemplates.h"
#include "engine/render/graph/TextureGraphCache.h"
#include "engine/pack/Sha256.h"
#include <memory>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // stb_image_write uses sprintf (deprecated in Apple's SDK)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "third_party/stb_image_write/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {

Json okJson() { Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r; }
Json strs(const std::vector<std::string>& v) { Json a = Json::array(); for (const auto& s : v) a.push(Json::string(s)); return a; }

void needProject(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("no game is open (open_game first)");
}

fs::path gameFile(const Workspace& ws, const std::string& rel) {
    const fs::path norm = fs::path(rel).lexically_normal();
    if (rel.empty() || norm.is_absolute() || norm.string().rfind("..", 0) == 0) throw std::runtime_error("path must be inside the game folder: " + rel);
    return fs::path(ws.project.dir) / norm;
}

bool readDisk(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// rules.json edit with the same guarantees as rules_merge: validated by the loader, saved, shown live
void editRules(Workspace& ws, const std::string& what, const std::function<void(Json&)>& edit) {
    Json backup = ws.project.rulesJson;
    edit(ws.project.rulesJson);
    ws.project.rulesDirty = true;
    if (!ws.project.reloadRules()) {
        const std::string err = ws.project.rulesError;
        ws.project.rulesJson = backup;
        ws.project.reloadRules();
        throw std::runtime_error("rules rejected by the loader: " + err);
    }
    ws.note({Change::Rules, 0, 0, -1, -1, -1, what});
    std::string err;
    if (!ws.project.saveRules(&err)) throw std::runtime_error(err);
}

Json& member(Json& obj, const std::string& key) {
    if (!obj[key].isObject()) obj.set(key, Json::object());
    return *obj.find(key);
}

// model files of a type's view ("model": "a.glb" | [..., {"file": ...}])
std::vector<std::string> modelFiles(const Json& view) {
    std::vector<std::string> out;
    const Json& m = view["model"];
    if (m.isString()) out.push_back(m.str());
    for (const auto& e : m.items()) {
        if (e.isString()) out.push_back(e.str());
        else if (e.isObject() && !e.getStr("file").empty()) out.push_back(e.getStr("file"));
    }
    return out;
}

// material and node names of a model (CPU load; no textures needed, errors reported in place)
Json modelSummary(const Workspace& ws, const std::string& file) {
    Json r = Json::object();
    r.set("file", Json::string(file));
    try {
        const std::string base = ws.project.dir + "/";
        auto m = render::loadModel(file, [&](const std::string& p, std::string& out) { return readDisk(base + p, out); }, nullptr);
        Json mats = Json::array(), nodes = Json::array(), all = Json::array();
        for (const auto& mt : m->materials) mats.push(Json::string(mt.name));
        for (const auto& n : m->nodes) {
            all.push(Json::string(n.name));
            if (n.mesh >= 0) nodes.push(Json::string(n.name));
        }
        r.set("materials", mats);
        r.set("meshNodes", nodes);
        r.set("nodes", all);   // "node" in material_apply covers a node and everything under it
    } catch (const std::exception& e) {
        r.set("error", Json::string(e.what()));
    }
    return r;
}

bool usesMaterial(const Json& j, const std::string& name) {
    if (j.isString()) return j.str() == name;
    if (j.isObject()) {
        if (j["use"].isString() && j.getStr("use") == name) return true;
        if (j["base"].isString() && j.getStr("base") == name) return true;
    }
    return false;
}

Json patternSchema() {
    return Json::parse(R"J({
        "path": {"type": "string", "description": "PNG inside the game folder (default textures/<pattern>_<seed>.png)"},
        "overwrite": {"type":"boolean","description":"False refuses an existing destination; legacy default true"},
        "pattern": {"type": "string", "description": "solid, noise, stripes, checker, brick, tiles, panels, metal, wood, camo, gradient, dots, hex, scales (texture_patterns explains each)"},
        "size": {"type": "integer", "description": "pixels per side, 8..2048 (default 256)"},
        "colors": {"type": "array", "items": {"type": "array", "items": {"type": "number"}}, "description": "sRGB [r,g,b] 0..1 per colour (pattern defaults otherwise)"},
        "scale": {"type": "number", "description": "repetitions / cells per side (default 8)"},
        "width": {"type": "number", "description": "line / mortar / stripe / dot share 0..1"},
        "variation": {"type": "number", "description": "per-cell brightness variation 0..1 (default 0.15)"},
        "angle": {"type": "number", "description": "stripes: 0, 45 or 90"},
        "octaves": {"type": "integer"}, "seed": {"type": "integer"},
        "output": {"type": "string", "enum": ["color", "normal", "mask"], "description": "normal = normal map of the same relief, mask = team mask (white where the second colour is)"},
        "strength": {"type": "number", "description": "normal map strength (default 2)"}})J");
}

const char* kMaterialDoc =
    "Material keys: color [r,g,b(,a)] linear 0..1, metallic 0..1, roughness 0..1, thinTransmission 0..1, shadowDiffusion 0..1 world units, sssStrength 0..1, sssScale 0..1, sssThickness 0..1 world units (0 = artistic backlight), sssColor [r,g,b] linear 0..1, emissive [r,g,b], emissiveStrength, "
    "alpha opaque|mask|blend, cutoff, doubleSided, unlit, castShadow, receiveShadow, vertexColors, team tint|mask|alpha|vertex|none, "
    "teamStrength, texture / normalMap / emissiveMap / teamMask (PNG/JPEG/TGA/KTX2 paths in the game folder, e.g. made by texture_generate), "
    "metallicRoughnessMap (glTF layout: G roughness, B metallic) / occlusionMap (R) with occlusionStrength, normalScale, "
    "uvScale [u,v] tiling, uvOffset [u,v], base (inherit another library material), replace (start from a blank material instead of the model's), description.";

} // namespace

const std::vector<std::string>& materialKeys() {
    static const std::vector<std::string> keys = {"shaderGraph", "color", "metallic", "roughness", "thinTransmission", "shadowDiffusion", "sssStrength", "sssScale", "sssThickness", "sssColor", "emissive", "emissiveStrength", "alpha", "cutoff",
                                                  "doubleSided", "unlit", "castShadow", "receiveShadow", "vertexColors", "team", "teamStrength",
                                                  "teamMask", "texture", "normalMap", "emissiveMap", "uvScale", "base", "use", "replace", "description",
                                                  "metallicRoughnessMap", "occlusionMap", "occlusionStrength", "normalScale", "uvOffset"};
    return keys;
}

std::vector<std::string> checkMaterial(const Json& m) {
    std::vector<std::string> out;
    if (!m.isObject()) { out.push_back("a material is an object"); return out; }
    const auto& keys = materialKeys();
    for (const auto& [k, v] : m.members()) {
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) { out.push_back("unknown key '" + k + "'"); continue; }
        if (k == "shaderGraph") { try { render::graph::compile(v, "material"); } catch (const std::exception& e) { out.push_back(e.what()); } }
        if ((k == "color" || k == "emissive" || k == "sssColor") && (!v.isArray() || v.size() < 3)) out.push_back(k + " must be [r, g, b]");
        if (k == "sssColor" && v.isArray() && v.size() >= 3) {
            for (size_t channel=0; channel<3; ++channel)
                if (!v[channel].isNumber() || !std::isfinite(v[channel].toDouble()) ||
                    v[channel].toDouble()<0 || v[channel].toDouble()>1) {
                    out.push_back("sssColor channels must be numbers 0..1");
                    break;
                }
        }
        if ((k == "metallic" || k == "roughness" || k == "thinTransmission" || k == "shadowDiffusion" || k == "sssStrength" || k == "sssScale" || k == "sssThickness" || k == "cutoff" || k == "teamStrength") && (!v.isNumber() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() > 1))
            out.push_back(k + " must be a number 0..1");
        if (k == "alpha" && v.str() != "opaque" && v.str() != "mask" && v.str() != "blend") out.push_back("alpha must be opaque, mask or blend");
        if (k == "team" && v.str() != "tint" && v.str() != "mask" && v.str() != "alpha" && v.str() != "vertex" && v.str() != "none")
            out.push_back("team must be tint, mask, alpha, vertex or none");
        if ((k == "uvScale" || k == "uvOffset") && (!v.isArray() || v.size() != 2)) out.push_back(k + " must be [u, v]");
        if (k == "occlusionStrength" && (!v.isNumber() || v.toDouble() < 0 || v.toDouble() > 1)) out.push_back(k + " must be a number 0..1");
    }
    return out;
}

Json loadMaterialLibrary(const Workspace& ws) {
    std::string text;
    if (ws.project.dir.empty() || !readDisk(ws.project.dir + "/materials.json", text)) return Json::object();
    try {
        Json j = Json::parse(text)["materials"];
        return j.isObject() ? j : Json::object();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("materials.json: ") + e.what());
    }
}

void saveMaterialLibrary(Workspace& ws, const Json& lib, const std::string& what) {
    Json doc = Json::object();
    doc.set("materials", lib);
    const fs::path f = fs::path(ws.project.dir) / "materials.json";
    {
        std::ofstream o(f.string() + ".tmp", std::ios::binary);
        o << doc.dump(2) << "\n";
        if (!o) throw std::runtime_error("cannot write " + f.string());
    }
    std::error_code ec;
    fs::rename(f.string() + ".tmp", f, ec);
    if (ec) throw std::runtime_error("cannot write " + f.string() + ": " + ec.message());
    ws.note({Change::Rules, 0, 0, -1, -1, -1, what});   // views rebuild with the library
}

static Json publishGraphExr(Workspace& ws,const std::string& name,int width,int height,const char* suffix,const char* output,const std::vector<uint8_t>& exr){
    needProject(ws);
    if(!render::texture_graph::safeId(name)||exr.empty())throw std::invalid_argument("invalid EXR publication");
    const auto root=fs::canonical(ws.project.dir),dir=root/"content"/"textures";
    if(fs::is_symlink(root/"content")||fs::is_symlink(dir))throw std::runtime_error("texture destination symlink rejected");
    fs::create_directories(dir);
    const std::string rel="content/textures/"+name+suffix+".exr";
    const auto dest=root/rel,stage=fs::path(dest.string()+".stage");
    if(fs::exists(dest)||fs::is_symlink(dest)||!fs::create_directory(stage))throw std::runtime_error("EXR already exists or staging busy");
    struct Cleanup {fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}} cleanup{stage};
    const auto temp=stage/"payload";
    {std::ofstream stream(temp,std::ios::binary);stream.write(reinterpret_cast<const char*>(exr.data()),std::streamsize(exr.size()));stream.close();if(!stream)throw std::runtime_error("EXR write failed");}
    fs::create_hard_link(temp,dest);
    ws.note({Change::Rules,0,0,-1,-1,-1,"Texture graph HDR bake: "+rel});
    Json r=okJson();r.set("path",Json::string(rel));r.set("output",Json::string(output));r.set("size",Json::number(int64_t(width)));r.set("width",Json::number(int64_t(width)));r.set("height",Json::number(int64_t(height)));r.set("format",Json::string("exr-rgba16f"));return r;
}
Json publishTextureGraphRadiance(Workspace& ws,const std::string& name,int size,std::span<const float> rgba){
    needProject(ws);
    if(!render::texture_graph::safeId(name)||size<8||size>1024||rgba.size()!=size_t(size)*size*4)throw std::invalid_argument("invalid HDR texture result");
    for(size_t i=0;i<rgba.size();++i)if(!std::isfinite(rgba[i])||std::abs(rgba[i])>65504.f||(i%4==3&&(rgba[i]<0.f||rgba[i]>1.f)))throw std::invalid_argument("invalid HDR texture sample");
    std::vector<uint8_t> exr;
    if(!render::cameraEncodeExrFloat(size,size,rgba,exr))throw std::runtime_error("EXR encode failed");
    return publishGraphExr(ws,name,size,size,"_radiance","radiance",exr);
}
Json publishTextureGraphEnvironment(Workspace& ws,const std::string& name,int size,std::span<const float> rgba){
    needProject(ws);
    const auto image=render::environment_ibl::fromGraphLatLong(size,rgba);
    std::vector<uint8_t> exr;
    if(!render::cameraEncodeExrHalf(image.width,image.height,image.rgba16f,exr))throw std::runtime_error("HDR environment EXR encode failed");
    return publishGraphExr(ws,name,image.width,image.height,"_environment","environment",exr);
}
Json publishTextureGraphImage(Workspace& ws,const std::string& name,const std::string& output,const render::Image& image){
    needProject(ws);
    if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture name");
    if(render::texture_graph::outputKind(output)==render::texture_graph::Kind::Radiance)throw std::invalid_argument("radiance must be exported as EXR");
    if(image.width<8||image.width>1024||image.height!=image.width||image.rgba.size()!=size_t(image.width)*image.height*4)
        throw std::invalid_argument("invalid completed texture image");
    const auto root=fs::canonical(ws.project.dir),dir=root/"content"/"textures";
    if(fs::is_symlink(root/"content")||fs::is_symlink(dir))throw std::runtime_error("texture destination symlink rejected");
    fs::create_directories(dir);
    const std::string rel="content/textures/"+name+"_"+output+".png";
    const auto dest=root/rel,stage=fs::path(dest.string()+".stage");
    if(fs::exists(dest)||fs::is_symlink(dest)||!fs::create_directory(stage))throw std::runtime_error("texture already exists or staging busy");
    struct Cleanup {fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}} cleanup{stage};
    std::string png;
    if(!stbi_write_png_to_func([](void* ctx,void* data,int size){static_cast<std::string*>(ctx)->append(static_cast<const char*>(data),size_t(size));},&png,image.width,image.height,4,image.rgba.data(),image.width*4)||png.empty())throw std::runtime_error("PNG encode failed");
    const auto temp=stage/"payload";
    {std::ofstream stream(temp,std::ios::binary);stream.write(png.data(),std::streamsize(png.size()));stream.close();if(!stream)throw std::runtime_error("PNG write failed");}
    fs::create_hard_link(temp,dest);
    ws.note({Change::Rules,0,0,-1,-1,-1,"Texture graph bake: "+rel});
    Json r=okJson();r.set("path",Json::string(rel));r.set("output",Json::string(output));r.set("size",Json::number(int64_t(image.width)));return r;
}

static Json publishBcFile(Workspace& ws,const std::string& name,const std::string& output,int width,const std::string& format,const render::bc_encode::Encoded& encoded,const char* backend){
    const auto kind=render::texture_graph::outputKind(output);
    render::CompressedImage check;std::string error;
    if(!render::parseDdsTexture(encoded.dds,check,error)||check.levels.size()!=size_t(encoded.levels))throw std::runtime_error("BC DDS validation failed: "+error);
    const auto root=fs::canonical(ws.project.dir),dir=root/"content"/"textures";
    if(fs::is_symlink(root/"content")||fs::is_symlink(dir))throw std::runtime_error("texture destination symlink rejected");
    fs::create_directories(dir);
    const auto rel="content/textures/"+name+"_"+output+"_"+format+".dds";
    const auto dest=root/rel,stage=fs::path(dest.string()+".stage");
    if(fs::exists(dest)||fs::is_symlink(dest)||!fs::create_directory(stage))throw std::runtime_error("texture already exists or staging busy");
    struct Cleanup{fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}}cleanup{stage};
    const auto temp=stage/"payload";
    {std::ofstream stream(temp,std::ios::binary);stream.write(encoded.dds.data(),std::streamsize(encoded.dds.size()));stream.close();if(!stream)throw std::runtime_error("DDS write failed");}
    fs::create_hard_link(temp,dest);
    ws.note({Change::Rules,0,0,-1,-1,-1,"Texture graph BC bake: "+rel});
    Json r=okJson();r.set("path",Json::string(rel));r.set("output",Json::string(output));r.set("size",Json::number(int64_t(width)));
    r.set("format",Json::string(format));r.set("compressionBackend",Json::string(backend));r.set("mipLevels",Json::number(int64_t(encoded.levels)));
    r.set("compressedBytes",Json::number(int64_t(encoded.payload)));r.set("rgbaBytes",Json::number(int64_t(encoded.rgbaBytes)));
    r.set("sRGB",Json::fromBool(kind==render::texture_graph::Kind::Color));return r;
}

// Explicit external codec invocation. Stage is exclusive; only validated
// complete containers are published via hard link. No fallback/re-encoding.
static Json publishExternalTexture(Workspace& ws,const std::string& name,const std::string& output,int size,
    const std::string& format,bool mipmaps,const render::Image* image,std::span<const float> radiance){
    needProject(ws);
    using namespace render;
    if(!texture_graph::safeId(name)||size<8||size>1024)throw std::invalid_argument("Invalid external texture export");
    const auto kind=texture_graph::outputKind(output);
    const bool hdr=format=="bc6h"||format=="bc6hs";
    const bool astc=format=="astc4x4"||format=="astc6x6";
    if((!hdr&&!astc&&format!="bc7")||(hdr!=(kind==texture_graph::Kind::Radiance)))throw std::invalid_argument("Incompatible external codec format");
    if(hdr){
        if(radiance.size()!=size_t(size)*size*4u)throw std::invalid_argument("Invalid HDR extent");
        for(size_t i=0;i<radiance.size();++i){
            const float v=radiance[i];
            if(!std::isfinite(v)||std::abs(v)>65504.f||(i%4==3&&v!=1.f)||(i%4!=3&&format=="bc6h"&&v<0.f))
                throw std::invalid_argument("BC6H requires opaque alpha and unsigned BC6H requires nonnegative RGB; use EXR or signed BC6H");
        }
    }else if(!image||image->width!=size||image->height!=size||image->rgba.size()!=size_t(size)*size*4u)throw std::invalid_argument("Invalid LDR extent");
    const auto root=fs::canonical(ws.project.dir),dir=root/"content"/"textures";
    if(fs::is_symlink(root/"content")||fs::is_symlink(dir))throw std::runtime_error("texture destination symlink rejected");
    fs::create_directories(dir);
    const auto rel="content/textures/"+name+"_"+output+"_"+format+(astc?".ktx2":".dds");
    const auto dest=root/rel,stage=fs::path(dest.string()+".stage");
    if(fs::exists(dest)||fs::is_symlink(dest)||!fs::create_directory(stage))throw std::runtime_error("texture already exists or staging busy");
    struct Cleanup{fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}}cleanup{stage};
    const bool color=kind==texture_graph::Kind::Color;
    std::string container,error;
    if(astc){
        aiverse::build::AstcOptions opt;opt.block=format=="astc4x4"?4:6;opt.srgb=color;opt.mipmaps=mipmaps;opt.semanticMips=true;opt.normal=kind==texture_graph::Kind::Normal;
        if(!aiverse::build::encodeAstcKtx2(image->rgba.data(),size,size,opt,aiverse::build::findAstcenc(),stage.string(),container,error))throw std::runtime_error(error);
    }else{
        std::string source;
        if(hdr){
            std::vector<uint8_t> bytes;
            if(!cameraEncodeExrFloat(size,size,radiance,bytes))throw std::runtime_error("BC6H source EXR encode failed");
            source.assign(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        }else if(!stbi_write_png_to_func([](void* ctx,void* data,int count){static_cast<std::string*>(ctx)->append(static_cast<const char*>(data),size_t(count));},&source,size,size,4,image->rgba.data(),size*4)||source.empty())throw std::runtime_error("BC7 source PNG encode failed");
        if(!aiverse::build::encodeCompressonatorDds(source,hdr?".exr":".png",size,size,format,mipmaps,aiverse::build::findCompressonator(),stage.string(),container,error))throw std::runtime_error(error);
        if(format=="bc7"&&container.size()>=148&&container.compare(0,4,"DDS ")==0&&container.compare(84,4,"DX10")==0){
            // BC7 UNORM/SRGB have identical block bytes. Set authored channel intent.
            const auto dxgi=uint32_t(uint8_t(container[128]))|(uint32_t(uint8_t(container[129]))<<8)|(uint32_t(uint8_t(container[130]))<<16)|(uint32_t(uint8_t(container[131]))<<24);
            if(dxgi==98||dxgi==99){container[128]=char(color?99:98);}
        }
    }
    CompressedImage check;
    const auto expected=hdr?(format=="bc6h"?BlockFormat::BC6H_UFloat:BlockFormat::BC6H_SFloat):astc?(format=="astc4x4"?BlockFormat::ASTC_4x4:BlockFormat::ASTC_6x6):BlockFormat::BC7;
    int levels=1;for(int n=size;mipmaps&&n>1;n/=2)++levels;
    if(!(astc?parseKtx2(container,check,error):parseDdsTexture(container,check,error))||check.format!=expected||
       check.width!=size||check.height!=size||check.depth!=1||check.arrayLayers!=1||check.faces!=1||check.oneDimensional||check.threeDimensional||
       check.levels.size()!=size_t(levels)||check.srgb!=color)
        throw std::runtime_error("External texture codec result rejected: "+error);
    uint64_t compressed=0,raw=0;for(int i=0;i<levels;++i){compressed+=check.levels[size_t(i)].size();raw+=uint64_t(levelWidth(check,i))*levelHeight(check,i)*(hdr?8u:4u);}
    const auto temp=stage/"payload";
    {std::ofstream stream(temp,std::ios::binary);stream.write(container.data(),std::streamsize(container.size()));stream.close();if(!stream)throw std::runtime_error("Container write failed");}
    fs::create_hard_link(temp,dest);
    ws.note({Change::Rules,0,0,-1,-1,-1,"Texture graph external codec: "+rel});
    Json result=okJson();result.set("path",Json::string(rel));result.set("output",Json::string(output));result.set("size",Json::number(int64_t(size)));
    result.set("format",Json::string(format));result.set("compressionBackend",Json::string(astc?"astcenc":"compressonator"));
    result.set("mipLevels",Json::number(int64_t(levels)));result.set("compressedBytes",Json::number(int64_t(compressed)));result.set("rgbaBytes",Json::number(int64_t(raw)));result.set("sRGB",Json::fromBool(color));return result;
}
Json publishTextureGraphRadianceFormat(Workspace& ws,const std::string& name,int size,std::span<const float> rgba,const std::string& format,bool mipmaps){
    if(format=="exr"||format=="png"||format.empty())return publishTextureGraphRadiance(ws,name,size,rgba);
    return publishExternalTexture(ws,name,"radiance",size,format,mipmaps,nullptr,rgba);
}
Json publishTextureGraphCompressed(Workspace& ws,const std::string& name,const std::string& output,const render::Image& image,const std::string& format,bool mipmaps){
    needProject(ws);
    using namespace render::bc_encode;
    using render::texture_graph::Kind;
    if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture name");
    const auto kind=render::texture_graph::outputKind(output);
    if(format=="astc4x4"||format=="astc6x6"||format=="bc7")return publishExternalTexture(ws,name,output,image.width,format,mipmaps,&image,{});
    const auto f=parseFormat(format);
    if(kind==Kind::Radiance||(f==Format::BC4&&kind!=Kind::Scalar)||(f==Format::BC5&&kind!=Kind::Normal))
        throw std::invalid_argument("BC4 is scalar-only, BC5 is normal-only; radiance must remain HDR");
    const auto encoded=encodeDds(image.rgba,image.width,image.height,f,kind==Kind::Color,kind==Kind::Normal,mipmaps);
    return publishBcFile(ws,name,output,image.width,format,encoded,"cpu");
}
Json publishTextureGraphBcBlocks(Workspace& ws,const std::string& name,const std::string& output,int size,const std::string& format,std::span<const uint8_t> blocks){
    needProject(ws);using namespace render::bc_encode;using render::texture_graph::Kind;
    if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture name");
    const auto kind=render::texture_graph::outputKind(output);const auto f=parseFormat(format);
    if(kind==Kind::Radiance||(f==Format::BC4&&kind!=Kind::Scalar)||(f==Format::BC5&&kind!=Kind::Normal))throw std::invalid_argument("Invalid GPU BC output");
    const bool mips=blocks.size()!=gpuBcBytes(size,f,false);
    const auto encoded=wrapGpuBcChain(size,f,kind==Kind::Color,mips,blocks);
    return publishBcFile(ws,name,output,size,format,encoded,"gpu");
}

namespace {
struct PinnedRaster {std::string revision;render::texture_graph::ImageSource source;};
float decodeGraphHalf(uint16_t h){
    const unsigned exponent=(h>>10)&31u,mantissa=h&1023u;
    if(exponent==31)throw std::invalid_argument("nonfinite HDR texel");
    const float v=exponent?std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
    return (h&0x8000u)?-v:v;
}
std::string readPinnedEncoded(const Workspace& ws,const std::string& rel,size_t cap=16u*1024u*1024u){
    needProject(ws);
    const fs::path relative(rel);
    if(rel.size()>512||rel.find('\0')!=std::string::npos||rel.find('\\')!=std::string::npos||rel.find(':')!=std::string::npos||relative.is_absolute()||relative.empty()||*relative.begin()!="content")throw std::invalid_argument("image asset must be inside project content");
    auto path=fs::canonical(ws.project.dir);
    for(const auto& part:relative){
        if(part==".."||part=="."||part.empty())throw std::invalid_argument("invalid image asset path");
        path/=part;if(fs::is_symlink(path))throw std::invalid_argument("image asset symlink rejected");
    }
    if(!fs::is_regular_file(path))throw std::runtime_error("image asset missing");
    const auto length=fs::file_size(path);
    if(!length||length>16u*1024u*1024u||length>cap)throw std::runtime_error("image asset exceeds encoded read budget");
    std::string bytes(size_t(length),'\0');std::ifstream stream(path,std::ios::binary);
    if(!stream.read(bytes.data(),std::streamsize(bytes.size()))||stream.peek()!=std::char_traits<char>::eof())throw std::runtime_error("image asset changed during read");
    return bytes;
}
std::string encodedRevision(const std::string& bytes){
    const auto digest=pack::sha256(bytes.data(),bytes.size());
    return pack::toHex(digest.data(),digest.size());
}
PinnedRaster readPinnedRaster(const Workspace& ws,const std::string& rel,const std::string& expected,size_t maxBytes){
    const auto bytes=readPinnedEncoded(ws,rel);
    PinnedRaster result;result.revision=encodedRevision(bytes);
    if(!expected.empty()&&expected!=result.revision)throw std::runtime_error("image asset revision changed; explicitly reselect or refresh source");
    const bool hdr=bytes.compare(0,10,"#?RADIANCE")==0||bytes.compare(0,6,"#?RGBE")==0;
    const bool exr=bytes.size()>=4&&bytes.compare(0,4,"\x76\x2f\x31\x01",4)==0;
    auto& source=result.source;
    source.hasAlpha=true;
    if(hdr||exr){
        render::RadianceImage image;std::string error;
        // Decoder uses 8 bytes/pixel, graph snapshot uses 16.
        const bool ok=hdr?render::parseRadianceHdr(bytes,image,error,maxBytes/2):render::parseOpenExr(bytes,image,error,maxBytes/2);
        if(!ok)throw std::runtime_error(error);
        if(image.width>4096||image.height>4096)throw std::runtime_error("HDR image extent exceeds graph limit");
        source.width=image.width;source.height=image.height;
        source.rgba32f.reserve(image.rgba16f.size());
        for(uint16_t h:image.rgba16f)source.rgba32f.push_back(decodeGraphHalf(h));
    }else if(render::isKtx2(bytes)||bytes.compare(0,4,"DDS ")==0){
        source=render::texture_graph::decodeContainerSource(bytes,maxBytes);
    }else{
        render::Image image;
        if(!render::decodeTextureGraphRaster(bytes,maxBytes/4,image))throw std::runtime_error("unsupported PNG/JPEG/HDR/EXR/DDS/KTX2 or image source exceeds decoded budget");
        source.width=image.width;source.height=image.height;
        const auto& rgba=image.rgba;source.rgb.resize(rgba.size()/4);
        for(size_t i=0;i<source.rgb.size();++i)source.rgb[i]=(uint32_t(rgba[i*4+3])<<24)|(uint32_t(rgba[i*4])<<16)|(uint32_t(rgba[i*4+1])<<8)|rgba[i*4+2];
    }
    return result;
}
}
render::texture_graph::ImageResolver textureGraphImageResolver(const Workspace& ws){
    return [&ws](const std::string& path,const std::string& revision,size_t budget){
        return readPinnedRaster(ws,path,revision,budget).source;
    };
}

Json publishTextureGraphSet(Workspace& ws,const std::string& name,const Json& graph,const Json& overrides,const std::vector<render::Image>& images,const std::vector<std::string>& backends){
    needProject(ws);const auto program=render::texture_graph::compile(graph,overrides,textureGraphImageResolver(ws));
    if(program.outputs.count("radiance"))throw std::invalid_argument("mixed HDR/PNG sets require separate EXR export");
    if(images.size()!=program.outputs.size()||backends.size()!=images.size())throw std::invalid_argument("incomplete texture set");
    for(size_t i=0;i<images.size();++i){
        const auto& image=images[i];const auto& backend=backends[i];
        if(image.width!=program.size||image.height!=program.size||image.rgba.size()!=size_t(program.size)*program.size*4||
           (backend!="cpu"&&backend!="metal"&&backend!="vulkan"))throw std::invalid_argument("invalid completed texture set image");
    }
        if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture set name");
        const auto root=fs::canonical(ws.project.dir),dir=root/"content"/"textures";
        if(fs::is_symlink(root/"content")||fs::is_symlink(dir))throw std::runtime_error("texture directory symlink rejected");
        fs::create_directories(dir);const auto dest=dir/name,stage=dir/("."+name+".stage");
        if(fs::exists(dest)||fs::is_symlink(dest)||!fs::create_directory(stage))throw std::runtime_error("texture set exists or staging busy");
        struct Cleanup {fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}} cleanup{stage};
        Json files=Json::array();size_t i=0;
        for(const auto& [output,index]:program.outputs){
            (void)index;const auto& img=images[i];const auto& backend=backends[i++];std::string png;
            if(!stbi_write_png_to_func([](void* ctx,void* data,int size){static_cast<std::string*>(ctx)->append(static_cast<const char*>(data),size_t(size));},&png,img.width,img.height,4,img.rgba.data(),img.width*4)||png.empty())throw std::runtime_error("PNG encode failed");
            std::ofstream stream(stage/(output+".png"),std::ios::binary);stream.write(png.data(),std::streamsize(png.size()));stream.close();if(!stream)throw std::runtime_error("PNG stage write failed");
            Json row=Json::object();row.set("output",Json::string(output));row.set("path",Json::string("content/textures/"+name+"/"+output+".png"));
            row.set("backend",Json::string(backend));row.set("sRGB",Json::fromBool(render::texture_graph::outputKind(output)==render::texture_graph::Kind::Color));files.push(row);
        }
        Json manifest=Json::object();manifest.set("version",Json::number(int64_t(1)));manifest.set("graph",graph);manifest.set("overrides",overrides);manifest.set("files",files);manifest.set("size",Json::number(int64_t(program.size)));
        {std::ofstream stream(stage/"texture-set.json",std::ios::binary);stream<<manifest.dump(2)<<"\n";stream.close();if(!stream)throw std::runtime_error("texture set manifest write failed");}
        // Cooperating publishers share the exclusive stage directory.
        // No promise against an external process creating an empty destination.
        if(fs::exists(dest)||fs::is_symlink(dest))throw std::runtime_error("destination appeared during bake");
        fs::rename(stage,dest);
        ws.note({Change::Rules,0,0,-1,-1,-1,"Texture graph set: "+name});
        Json r=okJson();r.set("files",files);r.set("size",Json::number(int64_t(program.size)));r.set("manifest",Json::string("content/textures/"+name+"/texture-set.json"));return r;

}

void registerMaterialOps(Workspace& ws) {
    Workspace* w = &ws;
    auto add = [&ws](const char* name, const std::string& desc, const std::string& schema, Workspace::OpFn fn) {
        ws.registerOp({name, desc, Json::parse(schema), std::move(fn), false});
    };

    add("material_list",
        "The game's material library (materials.json) and, per type with a model, its material overrides, node assignments, "
        "the model's own material names and mesh nodes (targets for material_apply).",
        R"({"properties": {"type": {"type": "string", "description": "only this type"}}})", [w](const Json& p) {
            needProject(*w);
            Json r = okJson();
            r.set("library", loadMaterialLibrary(*w));
            Json types = Json::array();
            const std::string only = p.getStr("type");
            for (const auto& [id, t] : w->project.rulesJson["types"].members()) {
                if (!only.empty() && id != only) continue;
                const Json& view = t["view"];
                const auto files = modelFiles(view);
                if (files.empty() && only.empty()) continue;
                Json tj = Json::object();
                tj.set("type", Json::string(id));
                if (view.has("materials")) tj.set("materials", view["materials"]);
                if (view.has("assign")) tj.set("assign", view["assign"]);
                Json models = Json::array();
                for (const auto& f : files) models.push(modelSummary(*w, f));
                tj.set("models", models);
                if (files.empty()) tj.set("note", Json::string("built-in shape: materials \"*\" applies to all its parts"));
                types.push(tj);
            }
            r.set("types", types);
            return r;
        });

    add("material_get", "One library material, flattened with its \"base\" chain.",
        R"({"properties": {"name": {"type": "string"}}, "required": ["name"]})", [w](const Json& p) {
            needProject(*w);
            const Json lib = loadMaterialLibrary(*w);
            const std::string name = p.getStr("name");
            if (!lib[name].isObject()) throw std::runtime_error("no library material '" + name + "'");
            Json flat = Json::object();
            std::vector<std::string> chain;
            std::string cur = name;
            for (int depth = 0; depth < 8 && lib[cur].isObject(); ++depth) {
                chain.push_back(cur);
                for (const auto& [k, v] : lib[cur].members()) if (k != "base" && !flat.has(k)) flat.set(k, v);
                cur = lib[cur].getStr("base");
                if (cur.empty()) break;
            }
            Json r = okJson();
            r.set("material", lib[name]);
            r.set("resolved", flat);
            r.set("chain", strs(chain));
            return r;
        });

    add("material_set",
        std::string("Create or replace a material in the game's library (materials.json), usable by name from any type: "
                    "material_apply, or a view's \"materials\" / \"assign\". ") + kMaterialDoc +
            " merge: true keeps the existing keys and changes only the given ones.",
        R"J({"properties": {"name": {"type": "string", "description": "e.g. steel, rusty_iron, team_cloth"},
            "material": {"type": "object", "description": "material keys (see description)"},
            "merge": {"type": "boolean"}}, "required": ["name", "material"]})J", [w](const Json& p) {
            needProject(*w);
            const std::string name = p.getStr("name");
            if (name.empty() || name.find_first_of(" \t/\\\"") != std::string::npos) throw std::runtime_error("material name must be one word (letters, digits, _ -)");
            Json mat = p["material"];
            Json lib = loadMaterialLibrary(*w);
            if (p.getBool("merge") && lib[name].isObject()) {
                Json cur = lib[name];
                for (const auto& [k, v] : mat.members()) cur.set(k, v);
                mat = cur;
            }
            const auto problems = checkMaterial(mat);
            if (!problems.empty()) throw std::runtime_error("material '" + name + "': " + problems[0]);
            if (mat["base"].isString() && !lib[mat.getStr("base")].isObject() && mat.getStr("base") != name)
                throw std::runtime_error("material '" + name + "': base '" + mat.getStr("base") + "' is not in the library");
            if (mat.getStr("base") == name) throw std::runtime_error("a material can't be its own base");
            std::vector<std::string> warnings;
            for (const char* k : {"texture", "normalMap", "emissiveMap", "teamMask", "metallicRoughnessMap", "occlusionMap"})
                if (mat[k].isString() && !mat.getStr(k).empty() &&
                    !fs::exists(gameFile(*w, mat.getStr(k)))) warnings.push_back(std::string(k) + " '" + mat.getStr(k) + "' does not exist yet");
            const bool existed = lib.has(name);
            lib.set(name, mat);
            saveMaterialLibrary(*w, lib, std::string(existed ? "Материал изменён: " : "Материал создан: ") + name);
            Json r = okJson();
            r.set("summary", Json::string(std::string(existed ? "Материал изменён: " : "Материал создан: ") + name));
            r.set("warnings", strs(warnings));
            return r;
        });

    add("material_delete", "Delete a library material. Refuses while types or other materials use it, unless force.",
        R"({"properties": {"name": {"type": "string"}, "force": {"type": "boolean"}}, "required": ["name"]})", [w](const Json& p) {
            needProject(*w);
            const std::string name = p.getStr("name");
            Json lib = loadMaterialLibrary(*w);
            if (!lib.has(name)) throw std::runtime_error("no library material '" + name + "'");
            std::vector<std::string> users;
            for (const auto& [id, t] : w->project.rulesJson["types"].members()) {
                for (const char* block : {"materials", "assign"})
                    for (const auto& [k, v] : t["view"][block].members()) if (usesMaterial(v, name)) users.push_back(id);
                for (const auto& e : t["view"]["model"].items())
                    for (const char* block : {"materials", "assign"})
                        for (const auto& [k, v] : e[block].members()) if (usesMaterial(v, name)) users.push_back(id);
            }
            for (const auto& [k, v] : lib.members()) if (k != name && usesMaterial(v, name)) users.push_back("material " + k);
            if (!users.empty() && !p.getBool("force")) throw std::runtime_error("material '" + name + "' is used by " + users[0] + (users.size() > 1 ? " and others" : "") + " (force: true deletes it anyway)");
            lib.erase(name);
            saveMaterialLibrary(*w, lib, "Материал удалён: " + name);
            Json r = okJson();
            r.set("usedBy", strs(users));
            return r;
        });

    add("material_apply",
        std::string("Give a type's model a material: a library name (\"steel\") or an inline material object. "
                    "target = a material name of the model (material_list shows them) or \"*\" for all: the material's keys "
                    "override that material (textures are kept unless given; replace: true starts blank). node = a mesh node "
                    "name or glob (\"Turret\", \"Wheel*\"): the meshes of that node and of everything under it get a fresh material. remove: true deletes the "
                    "override / assignment. ") + kMaterialDoc,
        R"J({"properties": {"type": {"type": "string"}, "material": {"description": "library name or material object"},
            "target": {"type": "string", "description": "model material name, or * (default)"},
            "node": {"type": "string", "description": "mesh node name or glob; instead of target"},
            "remove": {"type": "boolean"}}, "required": ["type"]})J", [w](const Json& p) {
            needProject(*w);
            const std::string type = p.getStr("type");
            if (!w->project.rulesJson["types"][type].isObject()) throw std::runtime_error("unknown type '" + type + "'");
            const bool byNode = p.has("node");
            const std::string key = byNode ? p.getStr("node") : p.getStr("target", "*");
            if (key.empty()) throw std::runtime_error(byNode ? "node is empty" : "target is empty");
            const std::string block = byNode ? "assign" : "materials";
            const bool remove = p.getBool("remove");
            if (!remove) {
                const Json& m = p["material"];
                if (m.isString()) {
                    if (!loadMaterialLibrary(*w)[m.str()].isObject()) throw std::runtime_error("no library material '" + m.str() + "' (material_set creates it)");
                } else if (m.isObject()) {
                    const auto problems = checkMaterial(m);
                    if (!problems.empty()) throw std::runtime_error(problems[0]);
                } else {
                    throw std::runtime_error("material must be a library name or an object");
                }
            }
            // warn when the target / node doesn't exist in any of the type's models
            std::vector<std::string> warnings;
            const Json& view = w->project.rulesJson["types"][type]["view"];
            const auto files = modelFiles(view);
            if (!remove && key != "*" && !files.empty()) {
                bool found = false;
                for (const auto& f : files) {
                    const Json s = modelSummary(*w, f);
                    for (const auto& n : s[byNode ? "nodes" : "materials"].items()) {
                        const std::string nm = n.str();
                        if (byNode ? (key.find_first_of("*?") != std::string::npos || nm == key) : nm == key) found = true;
                    }
                }
                if (!found) warnings.push_back(std::string(byNode ? "node '" : "material '") + key + "' not found in the type's model(s) (material_list lists them)");
            }
            editRules(*w, "Материал: " + type + " / " + key, [&](Json& rules) {
                Json& v = member(member(member(rules, "types"), type), "view");
                Json& b = member(v, block);
                if (remove) b.erase(key);
                else b.set(key, p["material"]);
                if (b.size() == 0) v.erase(block);
            });
            Json r = okJson();
            r.set("summary", Json::string(std::string(remove ? "Материал снят: " : "Материал назначен: ") + type + " / " + key));
            r.set("warnings", strs(warnings));
            return r;
        });

    add("texture_patterns", "Procedural texture patterns of texture_generate, with the keys each one reads.", "{}", [](const Json&) {
        Json r = okJson();
        r.set("patterns", strs(render::texturePatterns()));
        r.set("help", Json::string(render::texturePatternHelp()));
        return r;
    });

    // Graph source documents and baked images have independent publication.
    // Fixed safe names, canonical parent check and exclusive staging directory.
    auto graphPath=[w](const std::string& name){
        needProject(*w);if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture graph name");
        auto root=fs::canonical(w->project.dir);auto dir=root/"texturegraphs";
        if(fs::is_symlink(dir))throw std::runtime_error("texturegraphs directory is a symlink");
        return dir/(name+".json");
    };
    auto readGraph=[](const fs::path& file){
        if(fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>256*1024)throw std::runtime_error("texture graph missing or invalid");
        return Json::parseFile(file.string());
    };
    auto publish=[](const fs::path& file,const std::string& bytes,bool overwrite,const Json* expected=nullptr){
        fs::create_directories(file.parent_path());
        if(fs::is_symlink(file)||fs::is_symlink(file.parent_path()))throw std::runtime_error("symlink destination rejected");
        const auto staging=fs::path(file.string()+".stage");
        if(!fs::create_directory(staging))throw std::runtime_error("publication already in progress or stale staging directory");
        struct Cleanup {fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}} cleanup{staging};
        if(overwrite&&(!expected||!fs::is_regular_file(file)||fs::is_symlink(file)||fs::file_size(file)>256*1024||!(Json::parseFile(file.string())==*expected)))throw std::runtime_error("texture graph changed before publication");
        auto temp=staging/"payload";
        {std::ofstream stream(temp,std::ios::binary);stream.write(bytes.data(),std::streamsize(bytes.size()));stream.close();if(!stream)throw std::runtime_error("texture graph publication write failed");}
        if(overwrite)fs::rename(temp,file);
        else fs::create_hard_link(temp,file); // exclusive creation, never overwrite existing asset
    };
    auto textureCache=std::make_shared<render::texture_graph::EvaluationCache>();
    const std::string graphSchema=R"({"type":"object","properties":{"name":{"type":"string"},"graph":{"type":"object"},"expected":{"type":"object"},"node":{"type":"string"},"output":{"type":"string"},"textureName":{"type":"string"},"overrides":{"type":"object"},"format":{"enum":["png","bc1","bc3","bc4","bc5","bc7","astc4x4","astc6x6","exr","bc6h","bc6hs"]},"mipmaps":{"type":"boolean"}}})";
    add("texture_graph_cache","Inspect or clear this Workspace texture evaluation cache.",R"({"properties":{"clear":{"type":"boolean"}}})",[w,textureCache](const Json& p){textureCache->selectProject(w->project.dir);if(p.getBool("clear"))textureCache->clear();return textureCache->status();});
    add("texture_graph_image_asset","Inspect a project PNG/JPEG/HDR/uncompressed EXR/DDS/KTX2 and return its immutable source revision, dimensions and format.",R"({"type":"object","properties":{"asset":{"type":"string"}},"required":["asset"]})",[w](const Json& p){
        auto image=readPinnedRaster(*w,p.getStr("asset"),"",render::texture_graph::maxImageSourceBytes);
        Json r=okJson();r.set("asset",Json::string(p.getStr("asset")));r.set("revision",Json::string(image.revision));
        r.set("width",Json::number(int64_t(image.source.width)));r.set("height",Json::number(int64_t(image.source.height)));
        r.set("format",Json::string(image.source.hdr()?"rgba32f-linear":image.source.authoredMips?"rgba8-container":"rgba8"));
        r.set("mipLevels",Json::number(int64_t(image.source.authoredMips?image.source.mips.size():1)));
        r.set("decodedBytes",Json::number(int64_t(image.source.bytes())));return r;
    });
    add("texture_graph_dependency_revisions","Read current SHA-256 revisions of bounded project image dependencies, without decoding or writing.",R"({"type":"object","properties":{"assets":{"type":"array","items":{"type":"string"}}},"required":["assets"]})",[w](const Json& p){
        needProject(*w);
        const auto& assets=p["assets"];
        if(!assets.isArray()||assets.size()>64)throw std::invalid_argument("dependency scan requires at most 64 assets");
        size_t remaining=64u*1024u*1024u;std::set<std::string> seen;Json revisions=Json::array();
        for(const auto& v:assets.items()){
            if(!v.isString()||!seen.insert(v.str()).second)throw std::invalid_argument("invalid or duplicate dependency asset");
            Json row=Json::object();row.set("asset",v);
            try{
                const auto bytes=readPinnedEncoded(*w,v.str(),remaining);remaining-=bytes.size();
                row.set("revision",Json::string(encodedRevision(bytes)));row.set("status",Json::string("ready"));
            }catch(const std::exception& e){row.set("status",Json::string("unavailable"));row.set("error",Json::string(e.what()));}
            revisions.push(row);
        }
        Json r=okJson();r.set("revisions",revisions);return r;
    });
    add("texture_graph_catalog","Native typed procedural CPU node catalog.",R"({"type":"object"})",[](const Json&){return render::texture_graph::describeCatalog();});
    add("texture_graph_templates","Validated native PBR starter graph documents.",R"({"type":"object"})",[](const Json&){Json r=okJson();r.set("templates",render::texture_graph::templates());return r;});
    add("texture_graph_list","List saved texture graph documents.",R"({"type":"object"})",[graphPath](const Json&){
        auto dir=graphPath("placeholder").parent_path();std::vector<std::string> names;
        if(fs::exists(dir))for(const auto& e:fs::directory_iterator(dir))if(!e.is_symlink()&&e.is_regular_file()&&e.path().extension()==".json"&&render::texture_graph::safeId(e.path().stem().string()))names.push_back(e.path().stem().string());
        std::sort(names.begin(),names.end());Json r=okJson();r.set("graphs",strs(names));return r;
    });
    add("texture_graph_get","Read saved source graph.",graphSchema,[graphPath,readGraph](const Json& p){Json r=okJson();r.set("graph",readGraph(graphPath(p.getStr("name"))));return r;});
    add("texture_graph_validate","Validate types, topology, parameters and work budget without evaluating or writing.",graphSchema,[w](const Json& p){
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object(),textureGraphImageResolver(*w));Json r=okJson();r.set("nodes",Json::number(int64_t(program.nodes.size())));r.set("size",Json::number(int64_t(program.size)));return r;
    });
    add("texture_graph_set","Save validated graph; existing source requires matching expected document. No texture bake.",graphSchema,[w,graphPath,readGraph,publish](const Json& p){
        render::texture_graph::compile(p["graph"],Json::object(),textureGraphImageResolver(*w));auto file=graphPath(p.getStr("name"));
        bool exists=fs::exists(file);
        if(exists&&(!p["expected"].isObject()||!(readGraph(file)==p["expected"])))throw std::runtime_error("texture graph changed; reload before save");
        const auto bytes=p["graph"].dump(2)+"\n";
        if(bytes.size()>256*1024)throw std::invalid_argument("saved texture graph exceeds 256 KiB file limit");
        publish(file,bytes,exists,&p["expected"]);
        Json r=okJson();r.set("graph",p["graph"]);return r;
    });
    add("texture_graph_preview","Evaluate one node or output at up to 32x32, return actual RGBA8 hex without disk writes.",graphSchema,[w,textureCache](const Json& p){
        auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object(),textureGraphImageResolver(*w));size_t target=program.nodes.size();
        if(p.has("node")){for(size_t i=0;i<program.nodes.size();++i)if(program.nodes[i].id==p.getStr("node"))target=i;}
        else {auto it=program.outputs.find(p.getStr("output"));if(it!=program.outputs.end())target=it->second;}
        textureCache->selectProject(w->project.dir);bool cacheHit=false;
        auto img=textureCache->evaluate(program,{target},std::min(program.size,32),cacheHit).front();std::string hex;hex.reserve(img.rgba.size()*2);
        for(auto v:img.rgba){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}
        Json r=okJson();r.set("width",Json::number(int64_t(img.width)));r.set("height",Json::number(int64_t(img.height)));r.set("rgbaHex",Json::string(hex));r.set("cacheHit",Json::fromBool(cacheHit));return r;
    });
    add("texture_graph_bake","Evaluate one declared output at full resolution and publish new PNG or linear HALF EXR under content/textures; never overwrite.",graphSchema,[w,textureCache](const Json& p){
        needProject(*w);auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object(),textureGraphImageResolver(*w));auto output=p.getStr("output"),name=p.getStr("textureName");
        if(!render::texture_graph::safeId(name))throw std::invalid_argument("invalid texture name");
        auto it=program.outputs.find(output);if(it==program.outputs.end())throw std::invalid_argument("unknown output");
        if(output=="radiance"){
            const auto rgba=render::texture_graph::evaluateRadiance(program,it->second);
            auto r=publishTextureGraphRadianceFormat(*w,name,program.size,std::span<const float>(rgba.data(),rgba.size()),p.getStr("format","exr"),p.getBool("mipmaps",true));
            r.set("backend",Json::string("cpu"));return r;
        }
        textureCache->selectProject(w->project.dir);bool cacheHit=false;
        auto img=textureCache->evaluate(program,{it->second},0,cacheHit).front();
        const auto format=p.getStr("format","png");
        auto r=format=="png"?publishTextureGraphImage(*w,name,output,img):
            publishTextureGraphCompressed(*w,name,output,img,format,p.getBool("mipmaps",true));
        r.set("cacheHit",Json::fromBool(cacheHit));return r;
    });

    add("texture_graph_bake_environment","Bake graph radiance into a new 2:1 linear EXR lat-long environment; never overwrite.",graphSchema,[w](const Json& p){
        needProject(*w);
        const auto program=render::texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object(),textureGraphImageResolver(*w));
        const auto target=program.outputs.find("radiance");
        if(target==program.outputs.end())throw std::invalid_argument("Bind a radiance output first");
        const auto rgba=render::texture_graph::evaluateRadiance(program,target->second);
        auto r=publishTextureGraphEnvironment(*w,p.getStr("textureName"),program.size,rgba);
        r.set("backend",Json::string("cpu"));return r;
    });

    add("texture_graph_bake_set","Bake all declared outputs into a new named directory; stage all PNGs before publication, no overwrite of existing set.",graphSchema,[w,textureCache](const Json& p){
        needProject(*w);const auto overrides=p.has("overrides")?p["overrides"]:Json::object();const auto program=render::texture_graph::compile(p["graph"],overrides,textureGraphImageResolver(*w));
        if(program.outputs.count("radiance"))throw std::invalid_argument("Export radiance separately as EXR before baking normalized PBR outputs");
        std::vector<size_t> targets;for(const auto& [output,index]:program.outputs)targets.push_back(index);
        textureCache->selectProject(w->project.dir);bool cacheHit=false;
        auto images=textureCache->evaluate(program,targets,0,cacheHit);
        auto r=publishTextureGraphSet(*w,p.getStr("textureName"),p["graph"],overrides,images,std::vector<std::string>(images.size(),"cpu"));
        r.set("cacheHit",Json::fromBool(cacheHit));return r;
    });

    add("texture_graph_hdr_material_variant","Create an inherited persistent PBR material referencing an existing linear EXR or BC6H bake; does not overwrite assets.",R"({"properties":{"textureName":{"type":"string"},"materialName":{"type":"string"},"base":{"type":"string"},"channel":{"enum":["color","emissive"]},"format":{"enum":["exr","bc6h","bc6hs"]}},"required":["textureName","materialName","base","channel"]})",[w](const Json& p){
        needProject(*w);
        const auto set=p.getStr("textureName"),name=p.getStr("materialName"),base=p.getStr("base"),channel=p.getStr("channel");
        if(!render::texture_graph::safeId(set)||!render::texture_graph::safeId(name)||!render::texture_graph::safeId(base)||name==base||(channel!="color"&&channel!="emissive"))throw std::invalid_argument("invalid HDR variant arguments");
        const auto root=fs::canonical(w->project.dir);
        const auto format=p.getStr("format","exr");
        if(format!="exr"&&format!="bc6h"&&format!="bc6hs")throw std::invalid_argument("Invalid HDR variant format");
        const auto rel="content/textures/"+set+(format=="exr"?"_radiance.exr":"_radiance_"+format+".dds");
        const auto file=root/rel;
        if(fs::is_symlink(root/"content")||fs::is_symlink(root/"content"/"textures")||fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>32u*1024u*1024u)throw std::runtime_error("missing or invalid HDR bake");
        std::ifstream stream(file,std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
        render::RadianceImage hdr;std::string error;
        if(format=="exr"){if(!render::parseOpenExr(bytes,hdr,error,32u*1024u*1024u))throw std::runtime_error(error);}
        else {render::CompressedImage image;if(!render::parseDdsTexture(bytes,image,error)||image.format!=(format=="bc6h"?render::BlockFormat::BC6H_UFloat:render::BlockFormat::BC6H_SFloat)||image.faces!=1||image.arrayLayers!=1||image.depth!=1||image.oneDimensional||image.threeDimensional)throw std::runtime_error("Invalid BC6H variant source: "+error);}
        const auto lib=loadMaterialLibrary(*w);
        if(lib.has(name)||!lib[base].isObject())throw std::runtime_error("variant name exists or base material missing");
        Json material=Json::object();material.set("base",Json::string(base));
        material.set(channel=="color"?"texture":"emissiveMap",Json::string(rel));
        if(channel=="emissive"){material.set("emissive",Json::parse("[1,1,1]"));material.set("emissiveStrength",Json::number(1.0));}
        Json args=Json::object();args.set("name",Json::string(name));args.set("material",material);
        return w->call("material_set",args,"texture-graph-hdr");
    });

    add("texture_graph_material_variant","Create a new inherited library material from a baked set manifest. Requires explicit new material and base names; no regeneration or overwrite.",R"({"properties":{"textureName":{"type":"string"},"materialName":{"type":"string"},"base":{"type":"string"}},"required":["textureName","materialName","base"]})",[w](const Json& p){
        needProject(*w);
        const auto set=p.getStr("textureName"),name=p.getStr("materialName"),base=p.getStr("base");
        if(!render::texture_graph::safeId(set)||!render::texture_graph::safeId(name)||!render::texture_graph::safeId(base)||name==base)throw std::invalid_argument("invalid variant names");
        const auto root=fs::canonical(w->project.dir),dir=root/"content"/"textures"/set,file=dir/"texture-set.json";
        if(fs::is_symlink(root/"content")||fs::is_symlink(root/"content"/"textures")||fs::is_symlink(dir)||fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>512*1024)throw std::runtime_error("invalid texture set manifest");
        const auto manifest=Json::parseFile(file.string());
        if(manifest.getInt("version")!=1)throw std::runtime_error("unsupported texture set manifest");
        const auto program=render::texture_graph::compile(manifest["graph"],manifest["overrides"],textureGraphImageResolver(*w));
        if(!manifest["files"].isArray()||manifest["files"].size()!=program.outputs.size())throw std::runtime_error("incomplete texture set manifest");
        const std::map<std::string,std::string> channels={{"color","texture"},{"normal","normalMap"},{"metallicRoughness","metallicRoughnessMap"},{"ao","occlusionMap"},{"emissive","emissiveMap"},{"mask","teamMask"}};
        Json material=Json::object();material.set("base",Json::string(base));
        std::set<std::string> seen;
        for(const auto& row:manifest["files"].items()){
            const auto output=row.getStr("output");
            const auto path="content/textures/"+set+"/"+output+".png";
            if(!program.outputs.count(output)||!seen.insert(output).second||row.getStr("path")!=path||fs::is_symlink(root/path)||!fs::is_regular_file(root/path))throw std::runtime_error("invalid or missing baked texture");
            if(auto it=channels.find(output);it!=channels.end())material.set(it->second,Json::string(path));
        }
        if(material.size()<2)throw std::runtime_error("no material-compatible outputs");
        const auto lib=loadMaterialLibrary(*w);
        if(lib.has(name)||!lib[base].isObject())throw std::runtime_error("variant name exists or base material missing");
        // Reuse the native library writer, not a separate runtime material format.
        Json args=Json::object();args.set("name",Json::string(name));args.set("material",material);
        auto r=w->call("material_set",args,"texture-graph");
        if(r.getBool("ok"))r.set("note",Json::string("Baked variant; configure texture importer metadata separately. Existing material library owner concurrency applies."));
        return r;
    });

    add("texture_generate",
        "Make a tileable texture (PNG in the game folder) for a material: bricks, metal panels, wood, camouflage, scales, noise, "
        "stripes, ... Also its normal map (output: normal) or a team mask (output: mask). Use the path in material_set / "
        "material_apply (texture, normalMap, teamMask) and uvScale for tiling.",
        std::string(R"({"properties": )") + patternSchema().dump(-1) + R"(, "required": ["pattern"]})", [w](const Json& p) {
            needProject(*w);
            render::Image img;
            render::generateTexture(p, img);
            std::string rel = p.getStr("path");
            if (rel.empty()) rel = "textures/" + p.getStr("pattern") + "_" + std::to_string(p.getInt("seed", 1)) +
                                   (p.getStr("output", "color") == "color" ? "" : "_" + p.getStr("output")) + ".png";
            if (fs::path(rel).extension() != ".png") throw std::runtime_error("path must end with .png");
            const fs::path f = gameFile(*w, rel);
            if (!p.getBool("overwrite", true) && fs::exists(f)) throw std::runtime_error("texture already exists; choose another name");
            std::error_code ec;
            fs::create_directories(f.parent_path(), ec);
            std::string png;
            stbi_write_png_to_func([](void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size_t(size)); },
                                   &png, img.width, img.height, 4, img.rgba.data(), img.width * 4);
            std::ofstream o(f, std::ios::binary);
            o.write(png.data(), std::streamsize(png.size()));
            if (!o) throw std::runtime_error("cannot write " + f.string());
            o.close();
            w->note({Change::Rules, 0, 0, -1, -1, -1, "Текстура: " + rel});   // materials using it reload
            Json r = okJson();
            r.set("path", Json::string(rel));
            r.set("size", Json::number(int64_t(img.width)));
            r.set("bytes", Json::number(int64_t(png.size())));
            return r;
        });
}

} // namespace aiverse::editor
