#include "LightBakeOps.h"
#include "BrandingOps.h"
#include "PrismaOps.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace aiverse::editor {

namespace fs = std::filesystem;

std::string lightBakeMapRel(const Workspace& ws) {
    if (ws.map.path.empty() || ws.project.dir.empty()) return {};
    const fs::path relative=ws.relative(ws.map.path);
    if(relative.empty() || relative.is_absolute() || *relative.begin()=="..")return {};
    return relative.generic_string();
}

bake::BakeSettings lightBakeSettings(const Workspace& ws) {
    bake::BakeSettings s;
    std::string err;
    if (const Json& b = ws.map.data.extra["bake"]; b.isObject()) bake::settingsFromJson(b.dump(-1), s, err);
    return s;
}

void applyLightBakeSettings(Workspace& ws,const bake::BakeSettings& settings) {
    Json block=Json::parse(bake::settingsToJson(settings));
    ws.map.beginEdit();
    ws.map.data.extra.set("bake",std::move(block));
    ws.map.touched();
}

// Keep page filenames native. The bridge accepts only a page index and channel.
static Json bakeIndex(const std::string& dir,const std::string& rel,Json& index) {
    Json r = Json::object();
    if (rel.empty() || dir.empty()) { r.set("exists", Json::fromBool(false)); return r; }
    const std::string folder = bake::bakeFolderFor(rel);
    r.set("folder", Json::string(folder));
    auto read = [&dir](const std::string& p, std::string& out) {
        std::ifstream f(dir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    bake::LightmapData d;
    std::string err;
    if (!bake::readLightmapHeader(folder, read, d, err, &index)) { r.set("exists", Json::fromBool(false)); return r; }
    r.set("exists", Json::fromBool(true));
    r.set("hash", Json::string(bake::hex64(d.hash)));
    r.set("rgbmRange",Json::number(d.rgbmRange,6));
    r.set("mode", Json::string(bake::bakedModeName(d.mode)));
    try { r.set("settings", Json::parse(d.settingsJson)); r.set("stats", Json::parse(d.statsJson)); } catch (...) {}
    return r;
}

Json lightBakeInfo(const Workspace& ws) {
    Json index;
    Json info=bakeIndex(ws.project.dir,lightBakeMapRel(ws),index);
    Json pages=Json::array();
    for(const auto& page:index["pages"].items()){
        Json item=Json::object();
        item.set("width",page["width"]);item.set("height",page["height"]);
        item.set("mask",Json::fromBool(!page.getStr("mask").empty()));
        pages.push(std::move(item));
    }
    info.set("atlasPages",std::move(pages));
    return info;
}

Json lightBakeAtlasPreview(const std::string& project,const std::string& relativeMap,const Json& params) {
    Json index;
    const auto info=bakeIndex(project,relativeMap,index);
    if(!info.getBool("exists") || info.getStr("hash")!=params.getStr("hash"))
        throw std::runtime_error("Baked output changed. Reopen the atlas.");
    const auto& pages=index["pages"];
    const auto n=params.getInt("page",-1);
    if(!params["page"].isNumber() || params["page"].toDouble()!=double(n) ||
       n<0 || size_t(n)>=pages.size())throw std::runtime_error("Unknown atlas page");
    const auto& page=pages[size_t(n)];
    const auto channel=params.getStr("channel");
    if(channel!="lightmap" && channel!="mask")throw std::runtime_error("Unknown atlas channel");
    const std::string filename=page.getStr(channel);
    const fs::path name(filename);
    if(filename.empty() || name!=name.filename() || name.has_root_path() ||
       filename.find_first_of("/\\:")!=std::string::npos || name.extension()!=".png")
        throw std::runtime_error("No PNG for this atlas channel");
    const auto directory=fs::path(project)/info.getStr("folder");
    const auto path=directory/name;
    if(fs::is_symlink(directory) || fs::is_symlink(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Atlas page file unavailable");

    std::ifstream file(path,std::ios::binary | std::ios::ate);
    const auto size=file.tellg();
    if(size<24 || size>300*1024*1024)throw std::runtime_error("Invalid atlas page size");
    file.seekg(0);
    unsigned char header[24];
    if(!file.read(reinterpret_cast<char*>(header),24) || std::memcmp(header,"\x89PNG\r\n\x1a\n",8) ||
       std::memcmp(header+12,"IHDR",4))throw std::runtime_error("Invalid atlas PNG");
    auto dimension=[&](int i){return uint32_t(header[i])<<24 | uint32_t(header[i+1])<<16 |
                                     uint32_t(header[i+2])<<8 | header[i+3];};
    const uint32_t w=dimension(16),h=dimension(20);
    if(!w || !h || w>8192 || h>8192 || w!=page.getInt("width") || h!=page.getInt("height"))
        throw std::runtime_error("Atlas dimensions differ from the bake header");
    std::string bytes(static_cast<size_t>(size),'\0');
    file.seekg(0);
    render::Image image;
    if(!file.read(bytes.data(),size) || !render::decodeImage(bytes,image) ||
       image.rgba.size()!=size_t(w)*h*4)throw std::runtime_error("Cannot decode atlas page");

    const double scale=std::min(1.,256./std::max(w,h));
    const int width=std::max(1,int(std::round(w*scale))),height=std::max(1,int(std::round(h*scale)));
    std::vector<uint8_t> rgba(size_t(width)*height*4,255);
    const float range=float(info["rgbmRange"].toDouble());
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto* pixel=image.rgba.data()+4*(size_t((y+.5)*h/height)*w+size_t((x+.5)*w/width));
        auto* out=rgba.data()+4*(y*width+x);
        if(channel=="mask"){out[0]=out[1]=out[2]=pixel[0];continue;}
        const auto rgb=bake::decodeRgbm(pixel,range);
        const auto display=[](float linear){return uint8_t(std::round(255.f*std::pow(linear/(1.f+linear),1.f/2.2f)));};
        out[0]=display(rgb.x);out[1]=display(rgb.y);out[2]=display(rgb.z);
    }
    Json result=Json::object();
    result.set("png",Json::string(base64Encode(encodePngRgba(width,height,rgba))));
    return result;
}

void registerLightBakeRuntimeOps(Workspace& ws,const LightBakeRuntimeHandler& handler) {
    auto add=[&](const char* name,const char* description,Json schema) {
        ws.registerOp({name,description,std::move(schema),[handler,name](const Json& params){
            if(!handler)throw std::runtime_error("Open a Studio project and connect to its editor link");
            return handler(name,params);
        },true});
    };
    Json schema=Json::object();
    for(const auto& op:ws.ops())if(op.name=="lightbake_set"){schema=op.schema;break;}
    schema.member("properties").set("force",Json::parse(R"({"type":"boolean"})"));
    add("lightbake_bake","Start baking the open map in the background. Optional settings update the map. Poll lightbake_status for progress.",std::move(schema));
    add("lightbake_status","Read background bake progress, the last result and the open map's baked lighting statistics.",Json::object());
}

void registerLightBakeOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"lightbake_atlas_preview","Preview one RGBM irradiance or sun visibility page of the open map.",
        Json::parse(R"({"properties":{"page":{"type":"integer"},"hash":{"type":"string"},"channel":{"type":"string","enum":["lightmap","mask"]}},"required":["page","hash","channel"]})"),
        [w](const Json& p){return lightBakeAtlasPreview(w->project.dir,lightBakeMapRel(*w),p);}});
    ws.registerOp({"lightbake_get",
        "Baked lighting of the open map: its bake settings (the map's 'bake' block over the defaults) and what the bake folder "
        "maps/<map>.bake holds (hash, mode, stats: regions, pages, texels, probes). Modes: indirect (lightmap = indirect light, sun "
        "realtime), shadowmask (+ baked sun shadows for far / shadowless views, default), subtractive (sun baked too, cheapest on phones).",
        Json::parse("{}"), [w](const Json&) {
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("settings", Json::parse(bake::settingsToJson(lightBakeSettings(*w))));
            r.set("bake", lightBakeInfo(*w));
            return r;
        }});
    ws.registerOp({"lightbake_set",
        "Change the open map's bake settings (stored in the map's 'bake' block; save the map to keep them). Keys: quality "
        "(preview | medium | high preset), mode (indirect | shadowmask | subtractive), texelsPerUnit, pageSize, samples, bounces, "
        "aoDistance, aoStrength, denoise, indirectIntensity, maskSamples, probeSpacing, probeHeight, probeSamples, padding. "
        "Then lightbake_bake (needs the editor).",
        Json::parse(R"({"properties": {"quality": {"type": "string", "enum": ["preview", "medium", "high"]},
            "mode": {"type": "string", "enum": ["indirect", "shadowmask", "subtractive"]}, "texelsPerUnit": {"type": "number"},
            "pageSize": {"type": "integer"}, "samples": {"type": "integer"}, "bounces": {"type": "integer"}, "aoDistance": {"type": "number"},
            "aoStrength": {"type": "number"}, "denoise": {"type": "boolean"}, "indirectIntensity": {"type": "number"}, "maskSamples": {"type": "integer"},
            "probeSpacing": {"type": "number"}, "probeHeight": {"type": "number"}, "probeSamples": {"type": "integer"}, "padding": {"type": "integer"}}})"),
        [w](const Json& p) {
            bake::BakeSettings s = lightBakeSettings(*w);
            std::string err;
            if (!bake::settingsFromJson(p.dump(-1), s, err)) throw std::runtime_error(err);
            applyLightBakeSettings(*w,s);
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("settings", Json::parse(bake::settingsToJson(s)));
            return r;
        }});
    ws.registerOp({"lightbake_clear",
        "Delete the open map's baked lighting (the folder maps/<map>.bake): the game falls back to realtime lighting. "
        "Generated lightmap UVs next to models (*.lmuv) are kept.",
        Json::parse("{}"), [w](const Json&) {
            const std::string rel = lightBakeMapRel(*w);
            if (rel.empty() || w->project.dir.empty()) throw std::runtime_error("no saved map is open");
            const fs::path dir = fs::path(w->project.dir) / bake::bakeFolderFor(rel);
            std::error_code ec;
            const auto removed = fs::remove_all(dir, ec);
            if (ec) throw std::runtime_error(dir.string() + ": " + ec.message());
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("removedFiles", Json::number(int64_t(removed)));
            w->log("lightbake", "cleared " + bake::bakeFolderFor(rel));
            return r;
        }});
    registerLightBakeRuntimeOps(ws);
}

} // namespace aiverse::editor
