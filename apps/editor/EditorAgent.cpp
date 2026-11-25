#include "engine/world/RegionOwnershipDiagnostics.h"
// Agent link: the editor serves MCP tool calls forwarded by aiversemcp over localhost, implements the
// editor-only tools (screenshots, camera, play-test, device simulator, windows), and launches the
// game client on a simulated device.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include "Devices.h"
#include "Editor.h"
#include "NatureGizmos.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // stb_image_write uses sprintf (deprecated in Apple's SDK)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "third_party/stb_image_write/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {
std::string base64(const std::vector<uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < in.size() ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return out;
}
void pngWrite(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}
Json reply(const Json& id, Json result) {
    Json r = Json::object();
    r.set("id", id);
    r.set("result", std::move(result));
    return r;
}
float cameraNumber(const Json& args,const char* key,float fallback) {
    if(!args.has(key))return fallback;
    const double value=args[key].toDouble();
    if(!args[key].isNumber()||!std::isfinite(value)||std::abs(value)>1000000.0)
        throw std::runtime_error(std::string("camera ")+key+" must be a finite number within +/-1000000");
    return float(value);
}
} // namespace

void Editor::pollAgentLink() {
    if (!server_.listening()) return;
    // Protocol 2 (docs/mcp-agent-link.md): the first message of a connection must be
    // {"method":"hello","token":<contents of net::agentTokenPath(port)>}. Anything that is
    // not JSON, an unknown first message or a wrong token closes the connection, so a
    // browser page (no-cors POST to 127.0.0.1) or another local user cannot run tools.
    std::vector<int> dropped;
    auto isDropped = [&](int c) { return std::find(dropped.begin(), dropped.end(), c) != dropped.end(); };
    auto reject = [&](int c, const Json& id, const std::string& why) {
        Json err = Json::object();
        err.set("error", Json::string(why));
        server_.send(c, reply(id, err).dump(-1));
        server_.drop(c);
        dropped.push_back(c);
    };
    for (auto& m : server_.poll()) {
        if (isDropped(m.client)) continue;
        Json msg;
        try { msg = Json::parse(m.line); } catch (...) { server_.drop(m.client); dropped.push_back(m.client); continue; }
        if (!msg.isObject()) { server_.drop(m.client); dropped.push_back(m.client); continue; }
        const Json& id = msg["id"];
        const std::string method = msg.getStr("method");
        if (method == "hello") {
            if (!server_.tokenPublished()) { reject(m.client, id, "agent link token file could not be written: " + net::agentTokenPath(server_.port()).string()); continue; }
            if (!net::tokenEquals(msg.getStr("token"), server_.token())) { reject(m.client, id, "agent link: wrong token"); continue; }
            server_.setAuthenticated(m.client);
            Json ok = Json::object();
            ok.set("ok", Json::fromBool(true));
            ok.set("protocol", Json::number(int64_t(2)));
            server_.send(m.client, reply(id, ok).dump(-1));
            continue;
        }
        if (!server_.authenticated(m.client)) {
            reject(m.client, id, "agent link: send {\"method\":\"hello\",\"token\":...} first (token in " +
                                 net::agentTokenPath(server_.port()).string() + "); update aiversemcp");
            continue;
        }
        if (method != "call") { server_.send(m.client, reply(id, Json::parse(R"({"error": "unknown method"})")).dump(-1)); continue; }
        const std::string name = msg["params"].getStr("name");
        const Json& args = msg["params"]["arguments"];
        if (name == "view_screenshot") {
            if(!shots_.empty() || captureArmed_) {
                server_.send(m.client,reply(id,Json::parse(R"({"error":"another screenshot is pending; retry after its reply"})")).dump(-1));continue;
            }
            auto camera=sceneCam;
            const auto window=args.getStr("window",args.has("device")?"game":"all");
            const bool clean=args.getBool("clean",window=="scene");
            int requestedWidth=0,requestedHeight=0;
            bool positioned=false;
            try {
                if((args.has("window")&&!args["window"].isString()) ||
                   (window!="all" && window!="scene" && window!="game"))throw std::runtime_error("window must be all|scene|game");
                if(args.has("clean") && args["clean"].type()!=Json::Type::Bool)throw std::runtime_error("clean must be boolean");
                if(clean && window!="scene")throw std::runtime_error("clean capture requires window scene");
                if(args.has("width")!=args.has("height"))throw std::runtime_error("capture width and height must be supplied together");
                if(args.has("width")) {
                    if(!clean)throw std::runtime_error("capture dimensions require a clean scene capture");
                    if(!args["width"].isNumber() || !args["height"].isNumber())throw std::runtime_error("capture dimensions must be numbers");
                    const auto width=args["width"].toDouble(),height=args["height"].toDouble();
                    if(!std::isfinite(width) || !std::isfinite(height) || width<256 || height<256 || width>4096 || height>4096 ||
                        width!=std::floor(width) || height!=std::floor(height) || double(width)*height>8388608)
                        throw std::runtime_error("capture dimensions must be integer256..4096 and at most8 megapixels");
                    requestedWidth=int(width);requestedHeight=int(height);
                }
                if(args.has("x")!=args.has("z"))throw std::runtime_error("camera x and z must be supplied together");
                if(args.has("x")) {
                    camera.target={cameraNumber(args,"x",0),cameraNumber(args,"y",0),cameraNumber(args,"z",0)};
                    positioned=true;
                } else if(args.has("y")) {
                    camera.target.y=cameraNumber(args,"y",camera.target.y);positioned=true;
                }
                if(args.has("distance"))camera.distance=std::clamp(cameraNumber(args,"distance",camera.distance),camera.minDistance,camera.maxDistance);
                if(args.has("pitch"))camera.pitch=std::clamp(cameraNumber(args,"pitch",0),-89.0f,89.0f)*0.0174533f;
                if(args.has("yaw"))camera.yaw=cameraNumber(args,"yaw",0)*0.0174533f;
            } catch(const std::exception& error) {
                Json result=Json::object();result.set("error",Json::string(error.what()));
                server_.send(m.client,reply(id,std::move(result)).dump(-1));continue;
            }
            // Apply only after the entire request validates. Camera inspection
            // never edits entity transforms or marks the document dirty.
            sceneCam=camera;
            if(positioned)sceneFlying_=false;
            if (args.has("device")) gameDevice = args.getStr("device");
            Json metadata=Json::object();
            if(clean){metadata.set("clean",Json::fromBool(true));metadata.set("requested_width",Json::number(int64_t(requestedWidth)));metadata.set("requested_height",Json::number(int64_t(requestedHeight)));}
            shots_.push_back({m.client,id.integer(),window,std::move(metadata)});
            // Do not put a notification over the requested scene/game image.
            if(args.getStr("window","all")=="all")ws_.log("agent", "Скриншот редактора");
            continue;
        }
        if ((name == "modeling_preview" || name == "animation_preview") && (!shots_.empty() || captureArmed_)) {
            server_.send(m.client, reply(id, Json::parse(R"({"error":"another screenshot is pending; retry after its reply"})")).dump(-1));
            continue;
        }
        Json result = ws_.call(name, args, "agent");
        if ((name == "modeling_preview" || name == "animation_preview") && !result.has("error")) {
            shots_.push_back({m.client, id.integer(), "all", result});
            continue;
        }
        server_.send(m.client, reply(id, result).dump(-1));
    }
}

bool Editor::cleanSceneCaptureRequested() const {
    return std::any_of(shots_.begin(),shots_.end(),[](const auto& shot){return shot.device=="scene" && shot.metadata.getBool("clean");});
}
std::pair<int,int> Editor::sceneCaptureSize(int width,int height) const {
    for(const auto& shot:shots_)if(shot.device=="scene" && shot.metadata.getBool("clean")) {
        const int requested=int(shot.metadata.getInt("requested_width"));
        if(requested>0)return {requested,int(shot.metadata.getInt("requested_height"))};
        // Default: current viewport resolution, bounded for readback payloads.
        const double scale=std::min({1.0,4096.0/std::max(1,std::max(width,height)),
            std::sqrt(8388608.0/std::max(1.0,double(width)*height))});
        return {std::max(1,int(width*scale)),std::max(1,int(height*scale))};
    }
    return {width,height};
}

void Editor::deliverShots(gfx::Device& dev) {
    if (shots_.empty() || captureArmed_) return;
    captureArmed_ = true;
    auto pending = shots_;
    shots_.clear();
    if(pending.size()==1 && pending[0].device=="scene" && pending[0].metadata.getBool("clean")) {
        const auto request=pending[0];auto lifetime=captureLifetime_;
        const auto fail=[&](const char* message){captureArmed_=false;Json result=Json::object();result.set("error",Json::string(message));server_.send(request.client,reply(Json::number(request.id),result).dump(-1));};
        if(!sceneViewport || sceneViewport->drawnFrame!=ImGui::GetFrameCount() || !sceneViewport->color.valid()) {
            fail("Requested Scene viewport is unavailable; editor-frame fallback refused");return;
        }
        const bool accepted=dev.captureTexture(sceneViewport->color,
            [this,lifetime,request](int width,int height,std::vector<uint8_t> pixels){
                if(!lifetime->load(std::memory_order_acquire))return;
                captureArmed_=false;Json result=request.metadata;
                if(width<=0 || height<=0 || uint64_t(width)*height>8388608 || pixels.size()<size_t(width)*height*4) {
                    result.set("error",Json::string("Scene texture readback failed"));
                } else {
                    std::vector<uint8_t> rgb(size_t(width)*height*3),png;
                    for(size_t p=0;p<size_t(width)*height;++p)for(size_t c=0;c<3;++c)rgb[p*3+c]=pixels[p*4+c];
                    stbi_write_png_to_func(pngWrite,&png,width,height,3,rgb.data(),width*3);
                    result.set("png_base64",Json::string(base64(png)));
                    result.set("width",Json::number(int64_t(width)));result.set("height",Json::number(int64_t(height)));
                    result.set("summary",Json::string("Clean Scene render, without editor overlays"));
                }
                server_.send(request.client,reply(Json::number(request.id),result).dump(-1));
            });
        if(!accepted)fail("Clean Scene texture capture is unsupported by this backend; editor-frame fallback refused");
        return;
    }
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    // crop rectangles in pixels, computed now (the windows were laid out this frame)
    struct Crop { int x, y, w, h; };
    std::vector<Crop> crops;
    for (auto& s : pending) {
        Viewport* vp = s.device == "game" ? gameViewport : s.device == "scene" ? sceneViewport : nullptr;
        if (vp && vp->pointsW > 0) crops.push_back({int(vp->originX * scale), int(vp->originY * scale), int(vp->pointsW * scale), int(vp->pointsH * scale)});
        else crops.push_back({0, 0, 0, 0});
    }
    auto lifetime = captureLifetime_;
    dev.captureNextFrame([this, lifetime, pending, crops](int w, int h, std::vector<uint8_t> px) {
        // The asynchronous screenshot result may arrive after Editor has
        // been destroyed. Check shared lifetime state BEFORE accessing this.
        if (!lifetime->load(std::memory_order_acquire)) return;
        captureArmed_ = false;
        for (size_t k = 0; k < pending.size(); ++k) {
            Json r = pending[k].metadata.isObject() ? pending[k].metadata : Json::object();
            if (w <= 0 || px.empty()) {
                r.set("error", Json::string("screenshots are not supported by this graphics backend"));
            } else if((pending[k].device=="scene"||pending[k].device=="game") && (crops[k].w<=0||crops[k].h<=0)) {
                r.set("error",Json::string("Requested viewport is unavailable; full editor capture refused"));
            } else {
                Crop c = crops[k];
                if (c.w <= 0 || c.h <= 0) c = {0, 0, w, h};
                c.x = std::clamp(c.x, 0, w - 1); c.y = std::clamp(c.y, 0, h - 1);
                c.w = std::min(c.w, w - c.x); c.h = std::min(c.h, h - c.y);
                // downscale large frames so the image stays a reasonable MCP payload
                int step = std::max(1, (std::max(c.w, c.h) + 1599) / 1600);
                int ow = c.w / step, oh = c.h / step;
                std::vector<uint8_t> out(size_t(ow) * size_t(oh) * 3);
                for (int y = 0; y < oh; ++y)
                    for (int x = 0; x < ow; ++x) {
                        const uint8_t* s = &px[(size_t(c.y + y * step) * size_t(w) + size_t(c.x + x * step)) * 4];
                        uint8_t* d = &out[(size_t(y) * size_t(ow) + size_t(x)) * 3];
                        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    }
                std::vector<uint8_t> png;
                stbi_write_png_to_func(pngWrite, &png, ow, oh, 3, out.data(), ow * 3);
                r.set("png_base64", Json::string(base64(png)));
                r.set("width", Json::number(int64_t(ow)));
                r.set("height", Json::number(int64_t(oh)));
                std::string where = pending[k].device == "game" ? "окно «Игра»" : pending[k].device == "scene" ? "окно «Сцена»" : "весь редактор";
                r.set("summary", Json::string("Скриншот: " + where));
            }
            server_.send(pending[k].client, reply(Json::number(pending[k].id), r).dump(-1));
        }
    });
}

void Editor::launchOnDevice(const std::string& device, bool landscape) {
    if (doc().path.empty() || doc().dirty) {
        std::string f = doc().path.empty() ? ws_.mapsDir() + "/_editor_test.json" : doc().path;
        saveMap(f);
    }
    std::string exe = opt_.gameExe;
    if (exe.empty()) { status = "Не найден исполняемый файл игры (AiverseGame)"; return; }
    std::string cmd = "\"" + exe + "\" --map-file \"" + doc().path + "\" --device " + device + (landscape ? ":landscape" : ":portrait");
#if defined(_WIN32)
    cmd = "start \"\" " + cmd;
#else
    cmd = "AIVERSE_CONTENT=\"" + ws_.project.dir + "\" " + cmd + " >/dev/null 2>&1 &";
#endif
    if (std::system(cmd.c_str()) != 0) status = "Не удалось запустить игру";
    else status = "Игра запущена: " + device;
}

void Editor::registerGuiOps() {
    auto add = [this](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws_.registerOp({name, desc, Json::parse(schema), std::move(fn), true});
    };
    auto okSummary = [](const std::string& s) { Json r = Json::object(); r.set("ok", Json::fromBool(true)); r.set("summary", Json::string(s)); return r; };
    add("geometry_stats", "Geometry counters for the last rendered editor view, reset each frame. Optional completedGpuFrameMs and viewportGpuTimings are separate asynchronous completed GPU-query intervals, not derived from counters.", "{}", [this](const Json&) {
        const auto& s = r3d_.geometryStats(); Json r=Json::object();
        r.set("clustersTested",Json::number(int64_t(s.clustersTested)));
        r.set("clustersCulled",Json::number(int64_t(s.clustersCulled)));
        r.set("coarseClusters",Json::number(int64_t(s.coarseClusters)));
        r.set("occluded",Json::number(int64_t(s.occluded)));
        r.set("occluderTriangles",Json::number(int64_t(s.occluderTriangles)));
        r.set("drawCalls",Json::number(int64_t(r3d_.drawCalls())));
        // Wall-clock frame interval (ImGui's 60-frame average): includes CPU work,
        // drawable waits and vsync, unlike completedGpuFrameMs.
        r.set("frameIntervalMs",Json::number(ImGui::GetIO().Framerate>0?1000.0/double(ImGui::GetIO().Framerate):0.0));
        r.set("cameraModelTriangles",Json::number(int64_t(r3d_.profile().cameraModelTriangles)));
        r.set("cameraMeshTriangles",Json::number(int64_t(r3d_.profile().cameraMeshTriangles)));
        r.set("opaqueSceneCapture",Json::fromBool(r3d_.ssrActive()));
        r.set("ssrRaymarch",Json::fromBool(r3d_.ssrActive() && r3d_.traceActive()));
        r.set("nereidConfiguredSurfaces",Json::number(int64_t(nereidPreview_.configuredSurfaces())));
        r.set("nereidWaterDraws",Json::number(int64_t(nereidPreview_.scene().drawStats().draws)));
        r.set("nereidWaterTriangles",Json::number(int64_t(nereidPreview_.scene().drawStats().triangles)));
        r.set("nereidMapWaterDraws",Json::number(int64_t(view_.nereidMapWater().drawStats().draws)));
        r.set("nereidMapWaterTriangles",Json::number(int64_t(view_.nereidMapWater().drawStats().triangles)));
        r.set("waterRenderer",Json::string(view_.terrainRenderer().waterRenderer()==game::TerrainLook::WaterRenderer::Nereid?"nereid":"legacy"));
        if(!view_.terrainRenderer().nereidWaterStatus().empty())r.set("nereidWaterRefused",Json::string(view_.terrainRenderer().nereidWaterStatus()));
        r.set("sunShadowModelTriangles",Json::number(int64_t(r3d_.profile().triangles[0])));
        {   // Static geometry scene: opt-in STRATA virtual geometry counters.
            const auto& g=view_.geometryScene().stats();auto st=Json::object();
            st.set("enabled",Json::fromBool(view_.geometryScene().strataPortableRenderingEnabled()));
            st.set("portableSources",Json::number(int64_t(g.strataPortableSources)));
            st.set("portableDeclined",Json::number(int64_t(g.strataPortableDeclined)));
            st.set("portableDraws",Json::number(int64_t(g.strataPortableDraws)));
            st.set("portableFallbacks",Json::number(int64_t(g.strataPortableFallbacks)));
            st.set("portableClusters",Json::number(int64_t(g.strataPortableClusters)));
            st.set("nodes",Json::number(int64_t(g.strataNodes)));
            st.set("failures",Json::number(int64_t(g.strataFailures)));
            st.set("stagedSources",Json::number(int64_t(g.strataSources)));
            st.set("skippedSources",Json::number(int64_t(g.strataSkippedSources)));
            st.set("seamUnsafeDeclined",Json::number(int64_t(g.strataSeamUnsafeDeclined)));
            st.set("globalBudgetDeclined",Json::number(int64_t(g.strataGlobalBudgetDeclined)));
            st.set("instances",Json::number(int64_t(view_.geometryScene().instanceCount())));
            st.set("sharedModels",Json::number(int64_t(g.strataPortableSharedModels)));
            st.set("sharedReuses",Json::number(int64_t(g.strataPortableSharedReuses)));
            st.set("autoLodSources",Json::number(int64_t(g.strataAutoLodSources)));
            st.set("autoStrataSources",Json::number(int64_t(g.strataAutoStrataSources)));
            {   // strata.select auto: one line per source, model and reason.
                auto list=Json::array();
                for(const auto& d:view_.geometryScene().strataSelectDecisions())list.push(Json::string(d));
                st.set("select",list);
                auto warn=Json::array();
                for(const auto& w:view_.geometryScene().warnings())
                    if(w.find("Strata")!=std::string::npos||w.find("STRATA")!=std::string::npos)warn.push(Json::string(w.substr(0,400)));
                st.set("warnings",warn);
                st.set("stageMs",Json::number(double(g.strataStageMs)));
                st.set("cookedSources",Json::number(double(g.strataCookedSources)));
                st.set("cookStaleSources",Json::number(double(g.strataCookStaleSources)));
                st.set("cookWrittenSources",Json::number(double(g.strataCookWrittenSources)));
                st.set("cookReadMs",Json::number(double(g.strataCookedLoadMs)));
                st.set("buildMs",Json::number(double(g.strataBuildMs)));
                st.set("compactUploads",Json::number(int64_t(g.strataCompactUploads)));
                st.set("compactReuses",Json::number(int64_t(g.strataCompactReuses)));
            }
            r.set("strata",st);
        }
        // Reuse completed host-owned queries only; do not poll, wait or add
        // GPU timestamps from the agent callback. Keep viewport and whole
        // frame scopes separate and omit unsupported/absent samples.
        const double frameGpu=dev_?dev_->gpuFrameMs():0;
        if(std::isfinite(frameGpu)&&frameGpu>0)
            r.set("completedGpuFrameMs",Json::number(frameGpu,6));
        auto viewportGpu=Json::array();
        for(const auto& timing:prisma_.viewGpuTimings()) {
            auto sample=Json::object();
            sample.set("viewId",Json::number(int64_t(timing.viewId)));
            sample.set("milliseconds",Json::number(timing.lastGpuMs,6));
            sample.set("sampleSequence",Json::number(int64_t(timing.sampleSequence)));
            viewportGpu.push(sample);
        }
        r.set("viewportGpuTimings",viewportGpu);
        const auto& model=r3d_.modelStats();auto levels=Json::array();for(auto count:model.cameraLodNodes)levels.push(Json::number(int64_t(count)));r.set("cameraLodNodes",levels);r.set("lodChains",Json::number(int64_t(model.cameraLodChains)));r.set("lodBlocked",Json::number(int64_t(model.cameraLodBlocked)));r.set("shadowLimitedLod",Json::number(int64_t(model.cameraShadowLimitedLod)));return r;
    });
    add("view_pick","Read-only visual mesh hit at normalized Scene coordinates u,v; uses the same picking path as mouse selection.",
        R"({"properties":{"u":{"type":"number"},"v":{"type":"number"}},"required":["u","v"]})",[this](const Json& p){
            if(!sceneViewport||!sceneViewport->color.valid())throw std::runtime_error("Scene viewport unavailable");
            if(!p["u"].isNumber()||!p["v"].isNumber())throw std::runtime_error("u,v required");
            const double u=p["u"].toDouble(),v=p["v"].toDouble();
            if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)throw std::runtime_error("u,v outside0..1");
            const auto& viewport=*sceneViewport;
            const auto selection=pickObject(viewport,sceneCam,viewport.originX+float(u)*viewport.pointsW,viewport.originY+float(v)*viewport.pointsH);
            const int index=selection.kind==Selection::Entity?selection.index:-1;
            Json result=Json::object();result.set("hit",Json::fromBool(index>=0));result.set("index",Json::number(int64_t(index)));
            if(index>=0&&size_t(index)<doc().data.entities.size())result.set("entityId",Json::string(doc().data.entities[size_t(index)].id));
            return result;
        });
    add("view_screenshot", "Screenshot of the editor (window: 'all' | 'scene' | 'game'); x,y,z set the Scene target (y defaults to zero with x,z), distance/pitch(-89..89)/yaw set its orbit. Scene defaults to clean texture capture; clean:false keeps viewport UI. Optional width/height render up to8MP without changing the map.",
        R"({"properties": {"window": {"type": "string", "enum": ["all", "scene", "game"]}, "device": {"type": "string"}, "clean":{"type":"boolean"}, "width":{"type":"integer"}, "height":{"type":"integer"}, "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}, "distance": {"type": "number"}, "pitch": {"type": "number"}, "yaw": {"type": "number"}}})",
        [](const Json&) -> Json { throw std::runtime_error("screenshots are answered asynchronously by the editor link"); });
    add("view_wind", "Editor-only wind showcase: TerraWeave forest overlay (forest), hierarchical wind masks on its trees (hierarchical), "
        "PHYSIS nature preview grass and wind (nature, grass, flames, sound, windSpeed m/s, windDirection deg towards, playing) and "
        "the wind-mask debug view (debug: off|parts|depth|weight|skeleton), lodScale 0.25..4 for forest LOD distances, treeGeometry auto|merged|perBranch (near-tree draw merging), horizon (trees to 250 m + TerraWeave Horizon cluster forest to the horizon). Never edits the map.",
        R"({"properties":{"forest":{"type":"boolean"},"hierarchical":{"type":"boolean"},"nature":{"type":"boolean"},"grass":{"type":"boolean"},"flames":{"type":"boolean"},"sound":{"type":"boolean"},"playing":{"type":"boolean"},"windSpeed":{"type":"number"},"windDirection":{"type":"number"},"grassQuality":{"type":"integer"},"lodScale":{"type":"number"},"treeGeometry":{"type":"string","enum":["auto","merged","perBranch"]},"horizon":{"type":"boolean"},"debug":{"type":"string","enum":["off","parts","depth","weight","skeleton"]}}})",
        [this](const Json& p) {
            auto flag=[&](const char* key,bool current){
                if(!p.has(key))return current;
                if(p[key].type()!=Json::Type::Bool)throw std::runtime_error(std::string(key)+" must be boolean");
                return p[key].boolean(current);
            };
            auto number=[&](const char* key,float current,float lo,float hi){
                if(!p.has(key))return current;
                const double v=p[key].toDouble();
                if(!p[key].isNumber()||!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string(key)+" out of range");
                return float(v);
            };
            render::wind::WindDebugView view=r3d_.windRigDebugView();
            if(p.has("debug")) {
                const auto d=p.getStr("debug");
                if(d=="off")view=render::wind::WindDebugView::Off;
                else if(d=="parts")view=render::wind::WindDebugView::Parts;
                else if(d=="depth")view=render::wind::WindDebugView::Depth;
                else if(d=="weight")view=render::wind::WindDebugView::Weight;
                else if(d=="skeleton")view=render::wind::WindDebugView::Skeleton;
                else throw std::runtime_error("debug must be off|parts|depth|weight|skeleton");
            }
            auto& nature=physisNaturePreview_.settings;
            const float speed=number("windSpeed",nature.windSpeed,0,60);
            const float direction=number("windDirection",nature.windDirectionDeg,-360,360);
            const int quality=int(number("grassQuality",float(nature.grassQuality),0,2));
            terraWeaveForestPreview_.setLodScale(number("lodScale",terraWeaveForestPreview_.lodScale(),0.25f,4));
            const bool hierarchical=flag("hierarchical",terraWeaveForestPreview_.hierarchicalWind());
            auto geometry=terraWeaveForestPreview_.treeGeometry();
            if(p.has("treeGeometry")) {
                const auto g=p.getStr("treeGeometry");
                if(g=="auto")geometry=terraweave::ArborTreeGeometry::Auto;
                else if(g=="merged")geometry=terraweave::ArborTreeGeometry::Merged;
                else if(g=="perBranch")geometry=terraweave::ArborTreeGeometry::PerBranch;
                else throw std::runtime_error("treeGeometry must be auto|merged|perBranch");
            }
            if(hierarchical!=terraWeaveForestPreview_.hierarchicalWind() ||
               geometry!=terraWeaveForestPreview_.treeGeometry()) {
                if(dev_)terraWeaveForestPreview_.release(*dev_);
                terraWeaveForestPreview_.setHierarchicalWind(hierarchical);
                terraWeaveForestPreview_.setTreeGeometry(geometry);
            }
            terraWeaveForestPreview_.setHorizon(flag("horizon",terraWeaveForestPreview_.horizon()));
            // Kilometre-scale Horizon needs a far clip beyond the default 1 km.
            if(terraWeaveForestPreview_.horizon())sceneCam.zFar=std::max(sceneCam.zFar,14000.0f);
            setTerraWeaveForestPreviewEnabled(flag("forest",terraWeaveForestPreview_.enabled()));
            nature.enabled=flag("nature",nature.enabled);
            nature.grass=flag("grass",nature.grass);
            nature.flames=flag("flames",nature.flames);
            nature.sound=flag("sound",nature.sound);
            nature.playing=flag("playing",nature.playing);
            nature.windSpeed=speed;nature.windDirectionDeg=direction;nature.grassQuality=quality;
            r3d_.setWindRigDebugView(view);
            Json r=Json::object();
            r.set("forest",Json::fromBool(terraWeaveForestPreview_.enabled()));
            r.set("hierarchical",Json::fromBool(terraWeaveForestPreview_.hierarchicalWind()));
            r.set("forestInstances",Json::number(int64_t(terraWeaveForestPreview_.visibleInstances())));
            r.set("horizon",Json::fromBool(terraWeaveForestPreview_.horizon()));
            r.set("nature",Json::fromBool(nature.enabled));
            r.set("summary",Json::string("Ветер: "+std::to_string(int(speed))+" м/с"));
            return r;
        });
    add("view_gizmos", "Scene direction gizmos for PHYSIS (wind, fire spread), NEREID (flow of the selected / all water) and TERRA "
        "(downhill slope of the selected terrain or around the camera target; wind at a selected tree). Booleans: physisWind, physisFire, "
        "nereidFlow, nereidAllWater, terraSlope, terraSlopeAtTarget, terraTreeWind; numbers radius (m), spacing (m). "
        "Gizmos are viewport UI: capture them with view_screenshot {window:'scene', clean:false}.",
        R"({"properties":{"physisWind":{"type":"boolean"},"physisFire":{"type":"boolean"},"nereidFlow":{"type":"boolean"},"nereidAllWater":{"type":"boolean"},"terraSlope":{"type":"boolean"},"terraSlopeAtTarget":{"type":"boolean"},"terraTreeWind":{"type":"boolean"},"erosionZones":{"type":"boolean"},"radius":{"type":"number"},"spacing":{"type":"number"},"select":{"type":"string"}}})",
        [this](const Json& p) {
            auto& g=natureGizmoSettings();
            auto flag=[&](const char* key,bool& v){if(p.has(key)){if(p[key].type()!=Json::Type::Bool)throw std::runtime_error(std::string(key)+" must be boolean");v=p[key].boolean(v);}};
            flag("physisWind",g.physisWind);flag("physisFire",g.physisFire);flag("nereidFlow",g.nereidFlow);
            flag("nereidAllWater",g.nereidAllWater);flag("terraSlope",g.terraSlope);flag("terraSlopeAtTarget",g.terraSlopeAtTarget);
            flag("terraTreeWind",g.terraTreeWind);flag("erosionZones",g.erosionZones);
            if(p.has("radius"))g.radiusMeters=std::clamp(float(p["radius"].toDouble(g.radiusMeters)),8.f,200.f);
            if(p.has("spacing"))g.spacingMeters=std::clamp(float(p["spacing"].toDouble(g.spacingMeters)),2.f,32.f);
            Json r=Json::object();
            // Optional: select a map entity by id (the gizmos of NEREID/TERRA follow the selection).
            if(p.has("select")) {
                const int index=doc().findEntity(p.getStr("select"));
                if(index<0)throw std::runtime_error("no map entity with that id");
                Selection s;s.kind=Selection::Entity;s.index=index;select(s);
                r.set("selected",Json::number(int64_t(index)));
            }
            r.set("ok",Json::fromBool(true));
            r.set("summary",Json::string("Гизмо природы обновлены"));
            return r;
        });
    add("view_ground", "Displayed ground height (m) at points [[x,z],...]: the heightfield, or on maps whose terrain is entity tiles "
        "(terrainPresentation entities) the tile meshes. Read-only; use to place cameras and markers on real terrain.",
        R"({"properties":{"points":{"type":"array","items":{"type":"array","items":{"type":"number"}}}},"required":["points"]})",
        [this](const Json& p) {
            Json heights=Json::array();
            for(const auto& q:p["points"].items()) {
                if(!q.isArray()||q.size()<2)throw std::runtime_error("points must be [[x,z],...]");
                heights.push(Json::number(double(surfaceHeightAt(float(q[0].toDouble()),float(q[1].toDouble()))),3));
            }
            Json r=Json::object();r.set("heights",heights);return r;
        });
    add("view_focus", "Move the Scene camera to look at x,y,z (y and distance optional).",
        R"({"properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}, "distance": {"type": "number"}}, "required": ["x", "z"]})", [this, okSummary](const Json& p) {
            const float x=cameraNumber(p,"x",0),y=cameraNumber(p,"y",0),z=cameraNumber(p,"z",0),distance=cameraNumber(p,"distance",-1);
            focusScene(x,z,distance);
            if(!view_.planetMode())sceneGoal_.y=y;
            return okSummary("Камера: (" + std::to_string(int(p["x"].toDouble())) + ", " + std::to_string(int(p["z"].toDouble())) + ")");
        });
    add("play_start", "Play-test the open map in the Game window: bots on every side, or the user plays slot 'human'. The user watches live. "
        "With client=true the game runs as in the game client instead (its own menus / meta screens of ui/main.lua, view scripts, script UI, input): "
        "the user plays it in the Game window.",
        R"({"properties": {"speed": {"type": "number"}, "human": {"type": "integer"}, "device": {"type": "string"}, "client": {"type": "boolean"}}})", [this, okSummary](const Json& p) {
            if (p.has("device")) gameDevice = p.getStr("device");
            if (p["client"].boolean(false)) {
                startClientPlay();
                if (!clientPlaying()) throw std::runtime_error(status);
                return okSummary("Игра как в клиенте запущена в окне «Игра»");
            }
            startPlay(int(p["human"].integer(0)));
            if (!playing()) throw std::runtime_error(status);
            playSpeed = float(p["speed"].toDouble(1));
            return okSummary("Тест запущен");
        });
    add("play_stop", "Stop the play-test (or the game running as in the client).", "{}", [this, okSummary](const Json&) { stopClientPlay(); stopPlay(); return okSummary("Тест остановлен"); });
    add("region_ownership_diagnostics", "Read last successful live region ownership reconciliation.", "{}", [this](const Json&) {
        Json r=Json::object();
        const Simulation* live=clientPlaying()?clientHost_.simulation():sim_.get();
        const auto* d=live?live->world().resourceConst<world::RegionOwnershipDiagnostics>():nullptr;
        r.set("available",Json::fromBool(d!=nullptr));
        if(!d) {r.set("reason",Json::string("No live region ownership diagnostics"));return r;}
        r.set("trackedEntities",Json::number(int64_t(d->trackedEntities)));
        r.set("scannedCars",Json::number(int64_t(d->scannedCars)));
        r.set("candidateCars",Json::number(int64_t(d->candidateCars)));
        r.set("pendingGroups",Json::number(int64_t(d->pendingGroups)));
        r.set("uncoveredSolo",Json::number(int64_t(d->uncoveredSolo)));
        r.set("uncoveredGroups",Json::number(int64_t(d->uncoveredGroups)));
        r.set("uncoveredLifecycle",Json::number(int64_t(d->uncoveredLifecycle)));
        r.set("journalOverflow",Json::fromBool(d->journalOverflow));
        r.set("soloCandidateMode",Json::fromBool(d->soloCandidateMode));
        return r;
    });
    add("play_status", "State of the play-test: time, result, per-player units/structures/resources.", "{}", [this](const Json&) {
        Json r = Json::object();
        r.set("playing", Json::fromBool(playing()));
        if (clientPlaying()) {   // as in the client: the game's own flow; a match only while one runs
            r.set("client", Json::fromBool(true));
            r.set("loading", Json::fromBool(clientHost_.loading()));
            r.set("inMatch", Json::fromBool(clientHost_.inMatch()));
            if (Simulation* cs = clientHost_.simulation()) {
                r.set("seconds", Json::number(double(cs->tick()) / cs->tickRate(), 1));
                r.set("result", Json::number(int64_t(cs->result())));
            }
            return r;
        }
        if (!sim_) return r;
        auto& gs = simModule()->state(*sim_);
        r.set("seconds", Json::number(double(sim_->tick()) / sim_->tickRate(), 1));
        r.set("result", Json::number(int64_t(sim_->result())));
        Json ps = Json::array();
        for (auto& p : sim_->players()) {
            Json pj = Json::object();
            int units = 0, structs = 0;
            sim_->world().each<game::Unit, Owner>([&](aiverse::Entity, game::Unit& u, Owner& o) { if (o.player == p.slot) (rules().types[u.type].structure ? structs : units)++; });
            pj.set("name", Json::string(p.name)); pj.set("alive", Json::fromBool(p.alive));
            pj.set("units", Json::number(int64_t(units))); pj.set("structures", Json::number(int64_t(structs)));
            Json res = Json::object();
            for (size_t k = 0; k < rules().resources.size(); ++k) res.set(rules().resources[k].id, Json::number(gs.players[p.slot].stock[k].toInt()));
            pj.set("resources", res);
            ps.push(pj);
        }
        r.set("players", ps);
        if (auto* se = simModule()->scripts()) {   // Lua script errors (file:line), newest last
            Json errs = Json::array();
            for (auto& e : se->log()) if (e.error) errs.push(Json::string("t=" + std::to_string(e.tick) + " " + e.text));
            r.set("scriptErrors", errs);
        }
        return r;
    });
    add("device_list", "Device presets of the Game window simulator: id, name, group, size in points, scale, safe-area insets "
        "(portrait and landscape), cut-out, touch. Optional 'filter' matches name/id/group.",
        R"({"properties": {"filter": {"type": "string"}}})", [](const Json& p) {
        auto lower = [](std::string v) { for (auto& c : v) c = char(std::tolower((unsigned char)c)); return v; };
        const std::string f = lower(p.getStr("filter"));
        static const char* cutNames[] = {"none", "notch", "island", "punch-center", "punch-left", "waterdrop"};
        auto insets = [](const Insets4& i) {
            Json j = Json::object();
            j.set("left", Json::number(double(i.left), 0)); j.set("top", Json::number(double(i.top), 0));
            j.set("right", Json::number(double(i.right), 0)); j.set("bottom", Json::number(double(i.bottom), 0));
            return j;
        };
        Json arr = Json::array();
        for (auto& d : kDevices) {
            if (!f.empty() && lower(d.name).find(f) == std::string::npos && lower(d.id).find(f) == std::string::npos && lower(d.group).find(f) == std::string::npos) continue;
            Json j = Json::object();
            j.set("id", Json::string(d.id)); j.set("name", Json::string(d.name)); j.set("group", Json::string(d.group));
            j.set("points", Json::string(std::to_string(d.w) + "x" + std::to_string(d.h)));
            j.set("pixels", Json::string(std::to_string(int(std::lround(d.w * d.scale))) + "x" + std::to_string(int(std::lround(d.h * d.scale)))));
            j.set("scale", Json::number(double(d.scale), 3));
            if (d.w > 0) { j.set("safePortrait", insets(d.portrait)); j.set("safeLandscape", insets(d.landscape)); }
            j.set("cutout", Json::string(cutNames[int(d.cutout)]));
            j.set("touch", Json::fromBool(d.touch));
            j.set("gpuTier", Json::number(int64_t(d.gpuTier)));
            arr.push(j);
        }
        Json r = Json::object();
        r.set("devices", arr);
        return r;
    });
    add("device_set", "Frame the Game window as a device (phone simulator) or 'none'; 'safeArea' shows the safe area overlay.",
        R"({"properties": {"device": {"type": "string"}, "orientation": {"type": "string", "enum": ["portrait", "landscape"]},
            "safeArea": {"type": "boolean"}}})", [this, okSummary](const Json& p) {
            if (p.has("device")) {
                if (!findDevice(p.getStr("device"))) throw std::runtime_error("unknown device (see device_list)");
                gameDevice = p.getStr("device");
            }
            if (p.has("orientation")) gameLandscape = p.getStr("orientation") != "portrait";
            if (p.has("safeArea")) gameSafeArea = p.getBool("safeArea", false);
            if (auto* w = window("game")) w->open = true;
            return okSummary("Устройство: " + gameDevice + (gameSafeArea ? ", safe area видна" : ""));
        });
    add("editor_windows", "List editor windows (id, title, group = top menu: scene | world | look | logic | publish | service, open).", "{}", [this](const Json&) {
        Json arr = Json::array();
        for (auto& w : windows_) {
            Json j = Json::object();
            j.set("id", Json::string(w->id())); j.set("title", Json::string(w->title())); j.set("group", Json::string(w->group()));
            j.set("open", Json::fromBool(w->open));
            arr.push(j);
        }
        Json r = Json::object();
        r.set("windows", arr);
        return r;
    });
    add("editor_window", "Open or close an editor window by id, 'resetLayout': true for the default layout, or 'workspace': a group "
        "(scene | world | look | logic | publish) — the default windows plus that group's windows in front.",
        R"({"properties": {"id": {"type": "string"}, "open": {"type": "boolean"}, "resetLayout": {"type": "boolean"},
            "workspace": {"type": "string", "enum": ["scene", "world", "look", "logic", "publish"]}}})", [this, okSummary](const Json& p) {
            if (p.getBool("resetLayout", false)) { for (auto& w : windows_) w->open = w->openByDefault(); resetLayout_ = true; return okSummary("Раскладка сброшена"); }
            if (p.has("workspace")) { openWorkspace(p.getStr("workspace")); return okSummary(status); }
            EditorWindow* w = window(p.getStr("id"));
            if (!w) throw std::runtime_error("unknown window (see editor_windows)");
            w->open = p.getBool("open", true);
            return okSummary(std::string(w->open ? "Открыто окно " : "Закрыто окно ") + w->title());
        });
    add("editor_theme", "Editor look: 'mode' dark | light, 'accent' blue | violet | teal | orange (saved with the layout).",
        R"({"properties": {"mode": {"type": "string", "enum": ["dark", "light"]}, "accent": {"type": "string", "enum": ["blue", "violet", "teal", "orange"]}}})",
        [this, okSummary](const Json& p) {
            EditorTheme::Mode m = theme_.mode();
            if (p.has("mode")) m = p.getStr("mode") == "light" ? EditorTheme::Mode::Light : EditorTheme::Mode::Dark;
            int a = theme_.accentIndex();
            for (int i = 0; i < EditorTheme::kAccentCount; ++i) if (p.getStr("accent") == EditorTheme::accent(i).id) a = i;
            theme_.set(m, a);
            return okSummary(std::string("Тема: ") + (m == EditorTheme::Mode::Light ? "светлая" : "тёмная") + ", акцент " + EditorTheme::accent(a).name);
        });
    add("editor_select", "Select something in the editor (Inspector shows it): entity index, player (1-based), area/region/type/resource/tech/faction key.",
        R"({"properties": {"kind": {"type": "string", "enum": ["entity", "player", "area", "region", "type", "resource", "tech", "faction", "map", "none"]}, "index": {"type": "integer"}, "key": {"type": "string"}}, "required": ["kind"]})",
        [this, okSummary](const Json& p) {
            std::string k = p.getStr("kind");
            Selection s;
            s.index = int(p["index"].integer(-1));
            s.key = p.getStr("key");
            s.kind = k == "entity" ? Selection::Entity : k == "player" ? Selection::Player : k == "area" ? Selection::Area : k == "region" ? Selection::Region
                   : k == "type" ? Selection::Type : k == "resource" ? Selection::Resource : k == "tech" ? Selection::Tech : k == "faction" ? Selection::Faction
                   : k == "map" ? Selection::MapProps : Selection::None;
            if (s.kind == Selection::Player) s.index -= 1;
            select(s);
            if (s.kind == Selection::Entity && s.index >= 0 && s.index < int(doc().data.entities.size())) focusScene(doc().data.entities[size_t(s.index)].x, doc().data.entities[size_t(s.index)].z, 18);
            return okSummary("Выбрано: " + k);
        });
}

} // namespace aiverse::editor
