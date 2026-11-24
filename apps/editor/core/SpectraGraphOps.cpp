// SpectraFX graph authoring operations shared by the editor and MCP.
// All graph compilation is CPU-only and presentation-only (SpectraFX graph v1).
// Legacy fx/*.json and fx_preview continue to work; fx_graph_bake bridges them.
#include "Workspace.h"
#include "FxOps.h"
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraAuthoring.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::editor {

void registerSpectraFunctionOps(Workspace& ws); // reusable v2 function libraries

namespace {
namespace fs = std::filesystem;

Json strGraph(const std::string& value) { return Json::string(value); }
Json graphStrings(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const auto& value : values) out.push(strGraph(value));
    return out;
}
void requireGame(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("no game is open (open_game)");
}
std::string requireGraphName(const Json& p, const char* key = "name") {
    const std::string name = p.getStr(key);
    // Match the existing effect file naming contract; prevent path traversal.
    if (!validFxName(name) || name == "fx")
        throw std::runtime_error(std::string(key) + ": expected 1..48 lowercase letters, digits or underscores (not 'fx')");
    return name;
}
fs::path graphDir(const Workspace& ws) {
    return fs::path(ws.project.dir) / "fx" / "graphs";
}
fs::path graphFile(const Workspace& ws, const std::string& name) {
    return graphDir(ws) / (name + ".vfx.json");
}
fs::path draftFile(const Workspace& ws, const std::string& name) {
    return graphDir(ws) / "_drafts" / (name + ".vfx.json");
}
fs::path effectFile(const Workspace& ws, const std::string& name) {
    return fs::path(ws.project.dir) / "fx" / (name + ".json");
}
std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.generic_string());
    constexpr std::streamsize maxBytes = 1024 * 1024;
    std::string result;
    result.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.bad()) throw std::runtime_error("read failed: " + path.generic_string());
    if (result.size() > size_t(maxBytes)) throw std::runtime_error("SpectraFX graph file exceeds 1 MiB");
    return result;
}
void writeFile(const fs::path& path, const std::string& text) {
    if (text.size() > 1024 * 1024) throw std::runtime_error("SpectraFX graph file exceeds 1 MiB");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) throw std::runtime_error("cannot create SpectraFX directory: " + ec.message());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !(out << text) || !out.flush())
        throw std::runtime_error("cannot write " + path.generic_string());
}
Json sourceGraph(const Json& p, const Workspace& ws) {
    if (p.has("graph")) {
        if (!p["graph"].isObject()) throw std::runtime_error("graph must be an object");
        return p["graph"];
    }
    if (p.has("text")) {
        const std::string data = p.getStr("text");
        if (data.size() > 1024 * 1024) throw std::runtime_error("graph text exceeds 1 MiB");
        return Json::parse(data);
    }
    return Json::parse(readFile(graphFile(ws, requireGraphName(p))));
}
Json graphEvaluation(const Json& graph, const Json& overrides, fx::EffectDef* compiled = nullptr) {
    fx::EffectDef def;
    std::vector<std::string> errors;
    const bool ok = spectra::compileGraph(graph, overrides, def, errors);
    Json response = Json::object();
    response.set("ok", Json::fromBool(ok));
    response.set("errors", graphStrings(errors));
    if (ok) {
        response.set("emitters", Json::number(int64_t(def.emitters.size())));
        response.set("looping", Json::fromBool(def.looping()));
        if (!def.looping()) response.set("lifetime", Json::number(double(def.lifetime()), 3));
        std::set<std::string> textures;
        for (const auto& e : def.emitters) {
            if (!e.texture.empty()) textures.insert(e.texture);
            if (e.trail.enabled && !e.trail.texture.empty()) textures.insert(e.trail.texture);
        }
        response.set("textures", graphStrings({textures.begin(), textures.end()}));
        // GPU eligibility is a *schema* assessment only: the user's RHI
        // device might still lack Metal compute, or shader setup might fail.
        const auto eligibility = spectra::inspectGraphGpuEligibility(def);
        response.set("metalGpuEligible", Json::fromBool(eligibility.eligible));
        response.set("metalGpuReason", strGraph(eligibility.eligible
            ? "Metal compute subset compatible; device/runtime still unverified"
            : eligibility.reason));
        // Separate opt-in GPU VM capability. This is a pure schema check;
        // no Metal shader is executed or verified by the editor operation.
        spectra::GraphRuntimeOptions vmOptions;
        vmOptions.gpuProgram=true;
        const auto vm=spectra::inspectGraphGpuEligibility(def,vmOptions);
        const bool authoredVm=def.emitters.size()==1 &&
                             !def.emitters[0].particleProgram.isNull();
        response.set("metalVmEligible",Json::fromBool(authoredVm && vm.eligible));
        response.set("metalVmReason",strGraph(!authoredVm
            ? "effect has no per-particle VM program"
            : vm.eligible
                ? "experimental bounded Metal VM eligible; device execution not verified"
                : vm.reason));
        if (compiled) *compiled = std::move(def);
    }
    return response;
}
// Project content assets, not arbitrary filesystem paths. Reject symlinks
// at every owned component, including the destination file.
constexpr size_t kReplayMaxBytes=65536;
constexpr size_t kReplayMaxEntries=256;
constexpr const char* kReplaySuffix=".spectra-replay.json";
void requireReplayProject(const Workspace& ws,const Json& p) {
    requireGame(ws);
    if(p.has("expectedProjectPath")&&(!p["expectedProjectPath"].isString()||
        p.getStr("expectedProjectPath")!=ws.project.dir))
        throw std::runtime_error("Stale replay project");
}
fs::file_status replayFileStatus(const fs::path& path) {
    std::error_code ec;
    const auto status=fs::symlink_status(path,ec);
    if(ec&&ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect replay storage");
    if(fs::is_symlink(status))throw std::runtime_error("Replay storage cannot use symlinks");
    return status;
}
fs::path replayDirectory(const Workspace& ws,bool create) {
    fs::path dir=fs::path(ws.project.dir);
    if(!fs::is_directory(dir))throw std::runtime_error("Invalid project directory");
    for(const char* part:{"content","spectrafx","replays"}){
        dir/=part;
        auto st=replayFileStatus(dir);
        if(!fs::exists(st)){
            if(!create)return {};
            fs::create_directory(dir);
            st=replayFileStatus(dir);
        }
        if(!fs::is_directory(st))throw std::runtime_error("Invalid replay directory");
    }
    return dir;
}
void validateReplayPreset(const Json& p,const Workspace& ws) {
    if(!p.isObject()||p.getStr("format")!="aiverse.spectrafx.replay"||p.getInt("version",-1)!=1||
       !p["projectPath"].isString()||p.getStr("projectPath")!=ws.project.dir||
       !p["graphSignature"].isString()||p.getStr("graphSignature").empty())
        throw std::runtime_error("Invalid replay preset format or project identity");
    const auto graph=Json::parse(p.getStr("graphSignature"));
    if(!graph.isObject()||graph.getInt("version")!=2)
        throw std::runtime_error("Replay signature must contain Graph v2");
    auto bounded=[](const Json& v,double lo,double hi){
        return v.isNumber()&&std::isfinite(v.toDouble())&&v.toDouble()>=lo&&v.toDouble()<=hi;
    };
    auto integer=[&](const Json& v,double hi){
        return bounded(v,0,hi)&&std::floor(v.toDouble())==v.toDouble();
    };
    const auto speed=p["speed"].toDouble();
    const auto bg=p.getStr("background");
    if(!integer(p["tick"],3600)||!p["speed"].isNumber()||
       (speed!=0.25&&speed!=0.5&&speed!=1&&speed!=2&&speed!=4)||
       p["loop"].type()!=Json::Type::Bool||p["grid"].type()!=Json::Type::Bool||
       p["axes"].type()!=Json::Type::Bool||(bg!="dark"&&bg!="neutral"&&bg!="light"&&bg!="black"))
        throw std::runtime_error("Invalid replay timeline or stage");
    const auto& camera=p["camera"];
    if(!camera.isObject()||!bounded(camera["yaw"],-1000,1000)||
       !bounded(camera["pitch"],-1.56,1.56)||!bounded(camera["distance"],0.01,100000)||
       !bounded(camera["targetX"],-1000000,1000000)||!bounded(camera["targetY"],-1000000,1000000)||
       !bounded(camera["targetZ"],-1000000,1000000)||camera["ortho"].type()!=Json::Type::Bool)
        throw std::runtime_error("Invalid replay camera");
    if((p.has("gpuBirthRandom")&&p["gpuBirthRandom"].type()!=Json::Type::Bool)||
       (p.has("seed")&&!integer(p["seed"],2147483647.0))||
       (p.has("gpuAllocationSeed")&&!integer(p["gpuAllocationSeed"],2147483647.0))||
       (p.has("gpuShaderSeed")&&!integer(p["gpuShaderSeed"],4294967295.0))||
       int(p.has("seed"))+int(p.has("gpuAllocationSeed"))+int(p.has("gpuShaderSeed"))>1)
        throw std::runtime_error("Invalid replay seed mode");
    const auto& params=p["parameters"];
    if(!params.isObject()||params.size()>8)throw std::runtime_error("Invalid replay parameters");
    for(const auto& [name,value]:params.members()){
        const auto& spec=graph["instanceParameters"][name];
        if(name.empty()||name.size()>48||
           name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos||
           (name[0]>='0'&&name[0]<='9')||!bounded(value,-1000000,1000000)||
           !spec.isObject()||!spec["min"].isNumber()||!spec["max"].isNumber()||
           value.toDouble()<spec["min"].toDouble()||value.toDouble()>spec["max"].toDouble())
            throw std::runtime_error("Invalid replay scalar: "+name);
    }
}
// Bounded direct content dependencies. FNV-1a detects changes, not authenticity.
constexpr uint64_t kReplayDependencyBudget=128ull*1024*1024;
void collectReplayPaths(const Json& value,std::set<std::string>& paths,unsigned depth=0) {
    if(depth>64)throw std::runtime_error("Replay graph nesting limit exceeded");
    if(value.isString()){
        const auto text=value.str();
        if(text.rfind("content/",0)==0){
            paths.insert(text);
            if(paths.size()>64)throw std::runtime_error("Too many replay dependencies");
        }
    }else if(value.isArray()){
        for(const auto& item:value.items())collectReplayPaths(item,paths,depth+1);
    }else if(value.isObject()){
        for(const auto& [key,item]:value.members()){(void)key;collectReplayPaths(item,paths,depth+1);}
    }
}
fs::path checkedReplayDependency(const Workspace& ws,const std::string& relative) {
    if(relative.rfind("content/",0)!=0||relative.size()>520||
       relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos||
       std::any_of(relative.begin(),relative.end(),[](unsigned char c){return c<32;}))
        throw std::runtime_error("Unsafe replay resource path");
    fs::path path=ws.project.dir;
    for(const auto& part:fs::path(relative)){
        if(part.empty()||part=="."||part=="..")throw std::runtime_error("Unsafe replay resource component");
        path/=part;
        (void)replayFileStatus(path);
    }
    if(!fs::is_regular_file(replayFileStatus(path)))throw std::runtime_error("Missing replay resource: "+relative);
    return path;
}
std::string replayFingerprint(const fs::path& file,uint64_t& budget) {
    const auto size=fs::file_size(file);
    if(size>budget)throw std::runtime_error("Replay dependency byte budget exceeded");
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read replay dependency");
    std::array<char,16384> buffer{};
    uint64_t hash=14695981039346656037ULL,read=0;
    while(input){
        input.read(buffer.data(),std::streamsize(buffer.size()));
        const auto count=uint64_t(input.gcount());
        if(count>budget-read)throw std::runtime_error("Replay dependency byte budget exceeded");
        for(size_t i=0;i<count;++i){hash^=static_cast<unsigned char>(buffer[i]);hash*=1099511628211ULL;}
        read+=count;
    }
    if(input.bad()||read!=size)throw std::runtime_error("Replay dependency changed during read");
    budget-=read;
    return std::to_string(hash)+":"+std::to_string(read);
}
// Resolve only local glTF external buffer/image URIs. Embedded data is
// already covered by the owning file's fingerprint. Reject unsupported
// URI forms rather than silently claiming that they are verified.
std::string replayLocalUri(const std::string& owner,const std::string& uri) {
    if(uri.rfind("data:",0)==0)return {};
    if(uri.empty()||uri.size()>512||uri.front()=='/'||
       uri.find_first_of("\\:%?#")!=std::string::npos||
       std::any_of(uri.begin(),uri.end(),[](unsigned char c){return c<32;}))
        throw std::runtime_error("Unsupported replay resource URI in "+owner);
    auto combined=(fs::path(owner).parent_path()/fs::path(uri)).lexically_normal();
    const auto result=combined.generic_string();
    if(result.rfind("content/",0)!=0)throw std::runtime_error("Replay resource URI escapes content");
    return result;
}
void addReplayChild(std::set<std::string>& children,const std::string& child){
    if(!child.empty())children.insert(child);
    if(children.size()>64)throw std::runtime_error("Too many replay dependencies");
}
void collectReplayChildren(const fs::path& file,const std::string& owner,
                           std::set<std::string>& children,uint64_t& parseBudget) {
    auto ext=file.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext!=".json"&&ext!=".gltf")return;
    const auto bytes=fs::file_size(file);
    if(bytes>1024*1024||bytes>parseBudget)
        throw std::runtime_error("Replay dependency JSON exceeds parse budget");
    const auto text=readFile(file);
    if(text.size()!=bytes)throw std::runtime_error("Replay dependency changed during parse");
    parseBudget-=bytes;
    const auto doc=Json::parse(text);
    collectReplayPaths(doc,children);
    if(ext==".gltf"){
        for(const char* key:{"buffers","images"}){
            const auto& entries=doc[key];
            if(entries.isNull())continue;
            if(!entries.isArray())throw std::runtime_error("Invalid glTF dependency table");
            for(const auto& item:entries.items()){
                if(!item.isObject())throw std::runtime_error("Invalid glTF dependency entry");
                if(item.has("uri")){
                    if(!item["uri"].isString())throw std::runtime_error("Invalid glTF URI");
                    addReplayChild(children,replayLocalUri(owner,item.getStr("uri")));
                }
            }
        }
    }
}
Json replayDependencies(const Json& preset,const Workspace& ws,bool recursive) {
    std::set<std::string> paths;
    collectReplayPaths(Json::parse(preset.getStr("graphSignature")),paths);
    Json manifest=Json::array();
    uint64_t budget=kReplayDependencyBudget,parseBudget=8ull*1024*1024;
    std::set<std::string> visited;
    std::map<std::string,Json> rows;
    while(!paths.empty()){
        const auto relative=*paths.begin();paths.erase(paths.begin());
        if(!visited.insert(relative).second)continue;
        if(visited.size()>64)throw std::runtime_error("Too many replay dependencies");
        const auto file=checkedReplayDependency(ws,relative);
        Json row=Json::object();
        row.set("path",strGraph(relative));
        row.set("content",strGraph(replayFingerprint(file,budget)));
        const auto meta=fs::path(file.string()+".meta");
        if(fs::is_regular_file(replayFileStatus(meta))){
            if(fs::file_size(meta)>65536)throw std::runtime_error("Oversized asset metadata");
            const auto metadata=Json::parse(readFile(meta));
            const auto guid=metadata.getStr("guid");
            if(guid.size()!=32||guid.find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Invalid asset GUID for "+relative);
            row.set("guid",strGraph(guid));
            row.set("meta",strGraph(replayFingerprint(meta,budget)));
        }
        if(recursive){
            std::set<std::string> children;
            collectReplayChildren(file,relative,children,parseBudget);
            for(const auto& child:children)if(!visited.count(child))paths.insert(child);
            if(paths.size()+visited.size()>64)throw std::runtime_error("Too many replay dependencies");
        }
        rows.emplace(relative,std::move(row));
    }
    for(auto& [path,row]:rows){(void)path;manifest.push(std::move(row));}
    return manifest;
}
Json replayCompatibility(const Json& preset,const Workspace& ws) {
    Json out=Json::object();
    const auto version=preset.getInt("dependencyVersion",1);
    out.set("scope",strGraph(version==2?"content paths, JSON references and external glTF buffer/image URIs":"direct content paths and existing meta sidecars only"));
    if(version!=1&&version!=2){
        out.set("state",strGraph("unavailable"));out.set("detail",strGraph("Unsupported dependency manifest version"));return out;
    }
    if(!preset.has("dependencies")){
        out.set("state",strGraph("unverified"));return out;
    }
    try{
        const auto current=replayDependencies(preset,ws,version==2);
        const bool same=current.dump(-1)==preset["dependencies"].dump(-1);
        out.set("state",strGraph(same?"matching":"changed"));
        out.set("count",Json::number(int64_t(current.size())));
    }catch(const std::exception& e){
        out.set("state",strGraph("unavailable"));out.set("detail",strGraph(e.what()));
    }
    return out;
}
Json readReplayPreset(const fs::path& file,const Workspace& ws) {
    if(!fs::is_regular_file(replayFileStatus(file))||fs::file_size(file)>kReplayMaxBytes)
        throw std::runtime_error("Missing or oversized replay preset");
    const auto bytes=readFile(file);
    if(bytes.size()>kReplayMaxBytes)throw std::runtime_error("Replay preset exceeds 64 KiB");
    auto preset=Json::parse(bytes);
    validateReplayPreset(preset,ws);
    return preset;
}
void noteGraph(Workspace& ws, const std::string& text) {
    // Existing editor live-reload watcher subscribes to Change::Fx.
    ws.note({Change::Fx, 0, 0, -1, -1, -1, text});
}
} // namespace

void registerSpectraGraphOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn)});
    };

    add("fx_replay_preset_list","List named project Replay Presets in content/spectrafx/replays; bounded metadata only.",
        R"({"properties":{"expectedProjectPath":{"type":"string"}}})",[&ws](const Json& p){
        requireReplayProject(ws,p);
        const auto dir=replayDirectory(ws,false);
        Json entries=Json::array();
        std::vector<std::string> names;
        if(!dir.empty())for(const auto& entry:fs::directory_iterator(dir)){
            if(!fs::is_regular_file(entry.symlink_status()))continue;
            const auto file=entry.path().filename().string();
            const std::string suffix=kReplaySuffix;
            if(file.size()<=suffix.size()||file.compare(file.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
            const auto name=file.substr(0,file.size()-suffix.size());
            if(validFxName(name)&&name!="fx")names.push_back(name);
            if(names.size()>kReplayMaxEntries)throw std::runtime_error("Replay library exceeds 256 entries");
        }
        std::sort(names.begin(),names.end());
        for(const auto& name:names){
            Json row=Json::object();row.set("name",strGraph(name));
            try{
                const auto preset=readReplayPreset(dir/(name+kReplaySuffix),ws);
                row.set("tick",preset["tick"]);row.set("ok",Json::fromBool(true));
                row.set("hasDependencies",Json::fromBool(preset.has("dependencies")));
            }catch(const std::exception& e){row.set("ok",Json::fromBool(false));row.set("error",strGraph(e.what()));}
            entries.push(std::move(row));
        }
        Json out=Json::object();out.set("presets",std::move(entries));return out;
    });
    add("fx_replay_preset_get","Read a named replay preset; optionally require an exact authored graph signature.",
        R"({"properties":{"name":{"type":"string"},"expectedProjectPath":{"type":"string"},"expectedGraphSignature":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p){
        requireReplayProject(ws,p);const auto name=requireGraphName(p);
        const auto dir=replayDirectory(ws,false);
        if(dir.empty())throw std::runtime_error("Replay library is empty");
        const auto preset=readReplayPreset(dir/(name+kReplaySuffix),ws);
        if(p.has("expectedGraphSignature")&&(!p["expectedGraphSignature"].isString()||
           p.getStr("expectedGraphSignature")!=preset.getStr("graphSignature")))
            throw std::runtime_error("Replay graph revision mismatch");
        Json out=Json::object();out.set("name",strGraph(name));out.set("preset",preset);
        out.set("compatibility",replayCompatibility(preset,ws));return out;
    });
    add("fx_replay_preset_check","Check current direct project content dependencies against a recorded replay manifest; legacy presets are unverified.",
        R"({"properties":{"preset":{"type":"object"},"expectedProjectPath":{"type":"string"}},"required":["preset"]})",
        [&ws](const Json& p){
        requireReplayProject(ws,p);
        if(p["preset"].dump(-1).size()>kReplayMaxBytes)throw std::runtime_error("Replay preset exceeds 64 KiB");
        validateReplayPreset(p["preset"],ws);
        return replayCompatibility(p["preset"],ws);
    });
    add("fx_replay_preset_save","Create an immutable named replay preset asset. Existing names are never overwritten.",
        R"({"properties":{"name":{"type":"string"},"preset":{"type":"object"},"expectedProjectPath":{"type":"string"}},"required":["name","preset"]})",
        [&ws](const Json& p){
        requireReplayProject(ws,p);const auto name=requireGraphName(p);
        Json preset=p["preset"];
        if(preset.dump(-1).size()>kReplayMaxBytes)throw std::runtime_error("Replay preset exceeds 64 KiB");
        validateReplayPreset(preset,ws);
        // Preserve existing baselines; never silently bless a changed resource.
        if(preset.has("dependencies")){
            if(replayCompatibility(preset,ws).getStr("state")!="matching")
                throw std::runtime_error("Replay resource baseline no longer matches");
        }else{
            preset.set("dependencyVersion",Json::number(int64_t(2)));
            preset.set("dependencies",replayDependencies(preset,ws,true));
        }
        const auto bytes=preset.dump(-1);
        if(bytes.size()>kReplayMaxBytes)throw std::runtime_error("Replay preset with dependencies exceeds 64 KiB");
        const auto dir=replayDirectory(ws,true);
        const auto file=dir/(name+kReplaySuffix);
        if(fs::exists(replayFileStatus(file)))throw std::runtime_error("Replay name already exists");
        size_t count=0;
        for(const auto& entry:fs::directory_iterator(dir)){
            const auto n=entry.path().filename().string();const std::string suffix=kReplaySuffix;
            if(n.size()>suffix.size()&&n.compare(n.size()-suffix.size(),suffix.size(),suffix)==0)++count;
        }
        if(count>=kReplayMaxEntries)throw std::runtime_error("Replay library is full");
        const auto temp=dir/(name+".aiverse-tmp");
        if(fs::exists(replayFileStatus(temp)))throw std::runtime_error("Replay temporary file already exists");
        try{
            writeFile(temp,bytes);
            if(fs::exists(replayFileStatus(file)))throw std::runtime_error("Replay name already exists");
            fs::rename(temp,file);
        }catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
        Json out=Json::object();out.set("name",strGraph(name));
        out.set("path",strGraph("spectrafx/replays/"+name+kReplaySuffix));
        return out;
    });

    add("fx_graph_list", "List SpectraFX graphs (fx/graphs/*.vfx.json) with compiler diagnostics and emitter counts.",
        R"({"properties":{}})", [&ws](const Json&) {
        requireGame(ws);
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(graphDir(ws), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string file = it->path().filename().string();
            const std::string suffix = ".vfx.json";
            if (file.size() <= suffix.size() || file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            const std::string name = file.substr(0, file.size() - suffix.size());
            if (validFxName(name) && name != "fx") names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        Json list = Json::array();
        for (const auto& name : names) {
            Json item = Json::object();
            item.set("name", strGraph(name));
            try {
                Json state = graphEvaluation(Json::parse(readFile(graphFile(ws, name))), Json::object());
                for (const auto& [key, value] : state.members()) item.set(key, value);
            } catch (const std::exception& e) {
                item.set("ok", Json::fromBool(false));
                item.set("errors", graphStrings({e.what()}));
            }
            list.push(std::move(item));
        }
        Json response = Json::object();
        response.set("graphs", std::move(list));
        response.set("summary", strGraph("SpectraFX graphs: " + std::to_string(names.size())));
        return response;
    });

    add("fx_graph_get", "Read an existing SpectraFX graph asset as raw text and parsed JSON.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})", [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        const std::string content = readFile(graphFile(ws, name));
        Json response = Json::object();
        response.set("name", strGraph(name));
        response.set("text", strGraph(content));
        try {
            const Json graph = Json::parse(content);
            response.set("graph", graph);
            response.set("validation", graphEvaluation(graph, Json::object()));
        } catch (const std::exception& e) {
            response.set("parseError", strGraph(e.what()));
        }
        return response;
    });

    add("fx_graph_validate", "Compile SpectraFX graph v1 without writing files. Accept a graph object, text or an existing name; optional exposed-parameter overrides.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"},"overrides":{"type":"object"}}})",
        [&ws](const Json& p) {
        requireGame(ws);
        return graphEvaluation(sourceGraph(p, ws), p.has("overrides") ? p["overrides"] : Json::object());
    });

    add("fx_graph_set", "Create or replace fx/graphs/<name>.vfx.json after strict SpectraFX compiler validation; legacy effects are not changed.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        if (!p.has("graph") && !p.has("text")) throw std::runtime_error("provide graph or text");
        const Json graph = sourceGraph(p, ws);
        Json response = graphEvaluation(graph, Json::object());
        response.set("name", strGraph(name));
        if (!response.getBool("ok")) return response; // never overwrite a valid graph with invalid input
        const std::string content = graph.dump(2) + "\n";
        const bool existed = fs::exists(graphFile(ws, name));
        writeFile(graphFile(ws, name), content);
        noteGraph(ws, (existed ? "SpectraFX graph updated: " : "SpectraFX graph created: ") + name);
        response.set("summary", strGraph((existed ? "Updated " : "Created ") + graphFile(ws, name).generic_string()));
        return response;
    });

    add("fx_graph_delete", "Delete a SpectraFX graph asset by name. Does not affect separately baked fx/*.json effects.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})", [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        std::error_code ec;
        if (!fs::remove(graphFile(ws, name), ec)) {
            if (ec) throw std::runtime_error("cannot delete SpectraFX graph: " + ec.message());
            throw std::runtime_error("SpectraFX graph not found: " + name);
        }
        noteGraph(ws, "SpectraFX graph deleted: " + name);
        Json response = Json::object();
        response.set("ok", Json::fromBool(true));
        response.set("summary", strGraph("Deleted graph " + name));
        return response;
    });

    add("fx_graph_node_catalog",
        "List SpectraFX v1/v2 authoring node types, categories, starter properties and typed data input ports.",
        R"({"properties":{}})", [&ws](const Json&) {
        requireGame(ws);
        Json entries=Json::array();
        for(const auto& info:spectra::authoring::nodeCatalog()) {
            Json item=Json::object();
            item.set("type",Json::string(info.type));
            item.set("group",Json::string(info.group));
            item.set("v2Only",Json::fromBool(info.v2Only));
            item.set("properties",Json::parse(info.initialProperties));
            item.set("ports",graphStrings(spectra::authoring::inputPorts(info.type)));
            entries.push(std::move(item));
        }
        Json response=Json::object();
        response.set("nodes",std::move(entries));
        response.set("newGraph",spectra::authoring::newGraphV2());
        return response;
    });

    // Safe model-only edit. Never overwrites an existing effect or source
    // file, and allows temporarily disconnected drafts as the user builds
    // a valid graph node by node. The caller can commit via fx_graph_set or
    // preserve incomplete work using fx_graph_draft_set.
    add("fx_graph_edit",
        "Apply a v1/v2 graph edit in memory and return the edited JSON plus compiler diagnostics. Actions: new, upgrade, add, connect, disconnect, deleteNode, setProperty; does not write files.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"},
             "action":{"type":"string"},"type":{"type":"string"},"from":{"type":"string"},
             "to":{"type":"string"},"toPort":{"type":"string"},"node":{"type":"string"},
             "property":{"type":"string"},"value":{},"draft":{"type":"boolean"}},
             "required":["action"]})", [&ws](const Json& p) {
        requireGame(ws);
        const std::string action=p.getStr("action");
        Json graph;
        if(action=="new")graph=spectra::authoring::newGraphV2();
        else if(p.getBool("draft") && !p.has("graph") && !p.has("text"))
            graph=Json::parse(readFile(draftFile(ws,requireGraphName(p))));
        else graph=sourceGraph(p,ws);
        std::string error;
        std::string added;
        bool ok=true;
        if(action=="new") {}
        else if(action=="upgrade") {
            if(graph.getInt("version")!=1)ok=false;
            else graph.set("version",Json::number(int64_t(2)));
        } else if(action=="add")
            ok=spectra::authoring::addNode(graph,p.getStr("type"),added,error);
        else if(action=="connect") {
            if(p.has("toPort"))
                ok=spectra::authoring::connectData(
                    graph,p.getStr("from"),p.getStr("to"),p.getStr("toPort"),error);
            else ok=spectra::authoring::connectExec(
                    graph,p.getStr("from"),p.getStr("to"),error);
        } else if(action=="disconnect")
            ok=spectra::authoring::disconnect(
                graph,p.getStr("from"),p.getStr("to"),p.getStr("toPort"));
        else if(action=="deleteNode") {
            const std::string name=p.getStr("node");
            const Json* found=spectra::authoring::node(
                static_cast<const Json&>(graph),name);
            if(!found)ok=false;
            else {
                Json nodes=Json::array(),links=Json::array(),outputs=Json::array();
                for(const Json& n:graph["nodes"].items())
                    if(n.getStr("id")!=name)nodes.push(n);
                for(const Json& e:graph["links"].items())
                    if(e.getStr("from")!=name&&e.getStr("to")!=name)links.push(e);
                for(const Json& id:graph["outputs"].items())
                    if(id.str()!=name)outputs.push(id);
                graph.set("nodes",std::move(nodes));
                graph.set("links",std::move(links));
                graph.set("outputs",std::move(outputs));
            }
        } else if(action=="setProperty") {
            Json* n=spectra::authoring::node(graph,p.getStr("node"));
            if(!n || !p.has("value"))ok=false;
            else {
                Json props=(*n)["properties"];
                if(!props.isObject())props=Json::object();
                ok=fx::vfx2::nestedSet(props,p.getStr("property"),p["value"]);
                if(ok)n->set("properties",std::move(props));
            }
        } else {ok=false;error="unknown graph edit action";}
        Json response=Json::object();
        response.set("ok",Json::fromBool(ok));
        if(!ok) {
            response.set("error",Json::string(error.empty()?"invalid or missing graph edit arguments":error));
            return response;
        }
        response.set("graph",graph);
        if(!added.empty())response.set("newNodeId",Json::string(added));
        response.set("validation",graphEvaluation(graph,Json::object()));
        response.set("summary",Json::string("In-memory SpectraFX graph edit; save via fx_graph_set or fx_graph_draft_set"));
        return response;
    });

    add("fx_graph_draft_set",
        "Save an incomplete SpectraFX graph in fx/graphs/_drafts/<name>.vfx.json without changing published assets. Return compiler diagnostics but do not reject disconnected drafts.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name=requireGraphName(p);
        if(!p.has("graph")&&!p.has("text"))
            throw std::runtime_error("provide a draft graph or text");
        const Json doc=sourceGraph(p,ws);
        if(!doc.isObject() || !spectra::authoring::checkVersion(doc) ||
           !doc["nodes"].isArray() || doc["nodes"].size()>256 ||
           !doc["links"].isArray() || doc["links"].size()>512)
            throw std::runtime_error("draft must be a bounded v1/v2 graph object");
        writeFile(draftFile(ws,name),doc.dump(2)+"\n");
        noteGraph(ws,"SpectraFX draft saved: "+name);
        Json response=Json::object();
        response.set("name",Json::string(name));
        response.set("ok",Json::fromBool(true));
        response.set("validation",graphEvaluation(doc,Json::object()));
        response.set("summary",Json::string("Draft saved separately; published graph/effect unchanged"));
        return response;
    });

    add("fx_graph_draft_get",
        "Read a saved incomplete SpectraFX v1/v2 draft, including validation diagnostics.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name=requireGraphName(p);
        const Json doc=Json::parse(readFile(draftFile(ws,name)));
        Json response=Json::object();
        response.set("name",Json::string(name));
        response.set("graph",doc);
        response.set("validation",graphEvaluation(doc,Json::object()));
        return response;
    });

    add("fx_graph_draft_list",
        "List SpectraFX draft graph names without publishing them.",
        R"({"properties":{}})", [&ws](const Json&) {
        requireGame(ws);
        Json list=Json::array();
        std::vector<std::string> names;
        std::error_code ec;
        for(fs::directory_iterator it(graphDir(ws)/"_drafts",ec),end;
            !ec&&it!=end;it.increment(ec)) {
            if(!it->is_regular_file(ec))continue;
            const std::string filename=it->path().filename().string();
            const std::string suffix=".vfx.json";
            if(filename.size()<=suffix.size()||
               filename.compare(filename.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
            const std::string name=filename.substr(0,filename.size()-suffix.size());
            if(validFxName(name)&&name!="fx")names.push_back(name);
        }
        std::sort(names.begin(),names.end());
        for(const auto& name:names)list.push(Json::string(name));
        Json response=Json::object();
        response.set("drafts",std::move(list));
        return response;
    });

    add("fx_graph_draft_publish",
        "Validate a draft and publish to fx/graphs/<name>.vfx.json; invalid drafts remain private and never replace the working asset.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name=requireGraphName(p);
        const fs::path draft=draftFile(ws,name);
        const Json doc=Json::parse(readFile(draft));
        Json response=graphEvaluation(doc,Json::object());
        response.set("name",Json::string(name));
        if(!response.getBool("ok"))return response;
        writeFile(graphFile(ws,name),doc.dump(2)+"\n");
        std::error_code ec;
        fs::remove(draft,ec); // publication succeeds even if cleanup fails
        noteGraph(ws,"SpectraFX draft published: "+name);
        response.set("summary",Json::string("Published validated SpectraFX graph "+name));
        return response;
    });

    add("fx_graph_bake", "Compile a SpectraFX graph into legacy fx/<effect>.json so fx_preview and existing game/event bindings can use it. Refuses to replace an effect unless overwrite=true.",
        R"({"properties":{"name":{"type":"string"},"effect":{"type":"string"},"overrides":{"type":"object"},"overwrite":{"type":"boolean"}},"required":["name","effect"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        const std::string effect = requireGraphName(p, "effect");
        fx::EffectDef compiled;
        Json response = graphEvaluation(sourceGraph(p, ws),
            p.has("overrides") ? p["overrides"] : Json::object(), &compiled);
        if (!response.getBool("ok")) return response;
        const fs::path target = effectFile(ws, effect);
        if (fs::exists(target) && !p.getBool("overwrite"))
            throw std::runtime_error("fx/" + effect + ".json exists; set overwrite=true to replace");
        writeFile(target, fx::effectToJson(compiled).dump(2) + "\n");
        noteGraph(ws, "SpectraFX graph baked: " + name + " -> " + effect);
        response.set("effect", strGraph(effect));
        response.set("summary", strGraph("Baked " + name + " to fx/" + effect + ".json"));
        response.set("next", strGraph("fx_preview {\"effect\":\"" + effect + "\"}"));
        return response;
    });
    // The same GPU preview used by SpectraStudio is available through MCP
    // when the tool call is forwarded to a running native Studio. Standalone
    // MCP keeps full graph authoring but reports the absent graphics device.
    // No RHI pointer, arbitrary file path or source code crosses this API.
    add("fx_graph_preview",
        "Control the *real native Studio* SpectraFX GPU/CPU particle preview. "
        "Actions: status, load, play, pause, step, seek (0..3600 ticks), "
        "restart, parameter (named scalar), stop, cameraReset, cameraOrbit, cameraZoom, "
        "cameraPreset (perspective/front/side/top), stage (background/grid/axes), "
        "playback (speed 0.25..4 and loop), burst (count 1..8), "
        "clear (GPU-safe deferred reset), capture (maxSide 128..768), "
        "captureStatus (one bounded native JPEG readback, not a stream), replayRestore, replayCancel. "
        "For load use a graph object or graphName with draft=true for a private draft. "
        "Optional expectedProjectPath and expectedGeneration reject stale commands. "
        "The preview is presentation only; no game ECS writes or offscreen fake renders. "
        "Requires the native Studio GPU viewport to be visible; standalone MCP "
        "returns a clear unavailable error.",
        R"({"properties":{"action":{"type":"string","enum":["status","load","play","pause","step","seek","restart","parameter","stop","cameraReset","cameraOrbit","cameraZoom","cameraPreset","stage","playback","burst","clear","capture","captureStatus","replayRestore","replayCancel"]},
            "graph":{"type":"object"},"graphName":{"type":"string"},
            "expectedProjectPath":{"type":"string"},"expectedGeneration":{"type":"integer","minimum":0},
            "camera":{"type":"object"},"parameters":{"type":"object"},"seed":{"type":"integer"},
            "gpuShaderSeed":{"type":"integer"},"gpuAllocationSeed":{"type":"integer"},"gpuBirthRandom":{"type":"boolean"},
            "draft":{"type":"boolean"},"tick":{"type":"integer","minimum":0,"maximum":3600},
            "play":{"type":"boolean"},"name":{"type":"string"},"value":{"type":"number"},
            "dx":{"type":"number"},"dy":{"type":"number"},"amount":{"type":"number"},
            "preset":{"type":"string","enum":["perspective","front","side","top"]},
            "background":{"type":"string","enum":["dark","neutral","light","black"]},
            "grid":{"type":"boolean"},"axes":{"type":"boolean"},
            "speed":{"type":"number"},"loop":{"type":"boolean"},
            "count":{"type":"integer","minimum":1,"maximum":8},
            "maxSide":{"type":"integer","minimum":128,"maximum":768}},
            "required":["action"]})",
        [&ws](const Json& p) {
            requireGame(ws);
            const std::string action=p.getStr("action");
            if(action!="status"&&action!="load"&&action!="play"&&
               action!="pause"&&action!="step"&&action!="seek"&&
               action!="restart"&&action!="parameter"&&action!="stop"&&
               action!="cameraReset"&&action!="cameraOrbit"&&action!="cameraZoom"&&
               action!="cameraPreset"&&action!="stage"&&action!="playback"&&
               action!="burst"&&action!="clear"&&
               action!="capture"&&action!="captureStatus"&&action!="replayRestore"&&action!="replayCancel")
                throw std::runtime_error("unknown SpectraFX preview action");
            Json command=p;
            if(action=="load") {
                if(p.has("graph")&&p.has("graphName"))
                    throw std::runtime_error("use either graph or graphName, not both");
                if(!p.has("graph")) {
                    const std::string graphName=requireGraphName(p,"graphName");
                    const fs::path file=p.getBool("draft")
                        ?draftFile(ws,graphName):graphFile(ws,graphName);
                    std::error_code ec;
                    const auto root=fs::weakly_canonical(fs::path(ws.project.dir),ec);
                    if(ec)throw std::runtime_error("cannot resolve active project path");
                    const auto resolved=fs::weakly_canonical(file,ec);
                    if(ec)throw std::runtime_error("cannot resolve VFX graph file");
                    const auto rel=resolved.lexically_relative(root);
                    if(rel.empty()||rel.is_absolute()||
                       (rel.begin()!=rel.end()&&(*rel.begin()).string()==".."))
                        throw std::runtime_error("preview graph reference escapes the active project");
                    command.set("graph",Json::parse(readFile(file)));
                }
                if(!command["graph"].isObject())
                    throw std::runtime_error("preview requires a Graph v2 JSON object");
            }
            return ws.spectraPreview(command);
        });
    registerSpectraFunctionOps(ws);
}

} // namespace aiverse::editor
