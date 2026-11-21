// fx_preview: spawn a particle effect in the editor's Scene (or, during a play-test, Game) view at a
// point, so an agent can look at it with view_screenshot. Also the per-frame particle tick shared by
// both views (previews, the play-test's event effects, particle lights). Core fx ops: core/FxOps.cpp.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "Editor.h"
#include "core/FxOps.h"
#include "engine/fx/SpectraFX.h"

namespace aiverse::editor {

namespace {
constexpr float kOrbit = 2.0f, kOrbitSpeed = 1.6f;   // fx_preview {moving}: like the Effects window's "movement"
void orbit(fx::ParticleSystem& ps, fx::FxHandle h, fx::float3 c, float t) {
    ps.move(h, {c.x + std::cos(t * kOrbitSpeed) * kOrbit, c.y, c.z + std::sin(t * kOrbitSpeed) * kOrbit},
            {-std::sin(t * kOrbitSpeed), 0, std::cos(t * kOrbitSpeed)});
}
} // namespace

void Editor::tickFx(const render::RTSCamera& cam, int w, int h, const std::vector<game::SceneObject>* objects) {
    const bool active = playing() || !fxPreview_.name.empty() || gameFx_.stats().particles > 0 || gameFx_.stats().instances > 0;
    if (!active) { fxTime_ = time_; return; }
    float dt = fxTime_ < 0 ? 0.0f : float(time_ - fxTime_);
    if (playing()) dt = playPaused ? 0.0f : dt * playSpeed;
    fxTime_ = time_;
    gameFx_.setView(cam, r3d_.viewProj(), w, h);
    gameFx_.setLightDir(r3d_.lighting.sunDir);
    if (playing() && dt > 0) gameFxBridge_.update(*sim_, objects, dt);
    if (fxFrozen_) dt = 0;
    if (fxPreview_.loop && !fxPreview_.name.empty() && !gameFx_.playing(fxPreview_.h) && time_ >= fxPreview_.next && dt > 0) {
        int id = gameFx_.effect(fxPreview_.name);
        fxPreview_.h = id < 0 ? fx::FxHandle{} :
            gameFx_.playWithProgramParameters(id,fxPreview_.pos,{0,1,0},
                fxPreview_.scale,fxPreview_.tint,fxPreview_.programParams);
        const fx::EffectDef* d = gameFx_.def(id);
        fxPreview_.next = time_ + (d && !d->looping() ? std::min(8.0, double(d->lifetime()) + 0.5) : 0.5);
    }
    if (fxPreview_.moving && dt > 0) { fxPreview_.moveT += dt; orbit(gameFx_, fxPreview_.h, fxPreview_.pos, fxPreview_.moveT); }
    gameFx_.update(dt);
    std::vector<fx::FxPointLight> lights;
    gameFx_.collectLights(lights);
    for (auto& l : lights) r3d_.lights.add(l.pos, l.radius, l.color, l.intensity);
}

void Editor::registerFxPreviewOps() {
    ws_.registerOp({"fx_preview",
        "Play a particle effect in the editor so you can look at it: spawns fx/<effect>.json at x, z (default: the view's centre; y = "
        "height above the ground) in the Scene view, or in the Game view during a play-test, and moves that view's camera there "
        "('distance' optional). 'advance' pre-runs it (seconds) and 'freeze' holds it still: then view_screenshot {window: 'scene'} "
        "shows that moment. 'loop' replays one-shots; 'moving' circles it 2 m around the point (trails, per-metre emitters); 'pitch' "
        "camera angle in degrees (90 = from above, 25 = from the side); 'scale', 'color' (\"#rrggbbaa\" tint), 'quality' (low | medium | "
        "high: how weak GPUs see it, also affects the play-test). {stop: true} removes the preview.",
        Json::parse(R"({"properties": {"effect": {"type": "string"}, "x": {"type": "number"}, "z": {"type": "number"}, "y": {"type": "number"},
            "scale": {"type": "number"}, "color": {"type": "string"}, "advance": {"type": "number"}, "freeze": {"type": "boolean"},
            "loop": {"type": "boolean"}, "moving": {"type": "boolean"}, "pitch": {"type": "number"}, "distance": {"type": "number"}, "quality": {"type": "string", "enum": ["low", "medium", "high"]},
            "stop": {"type": "boolean"}}})"),
        [this](const Json& p) -> Json {
            Json r = Json::object();
            if (p.getBool("stop")) {
                gameFx_.kill(fxPreview_.h);
                stopSpectraGpuPreview();
                fxPreview_ = {};
                fxFrozen_ = false;
                r.set("ok", Json::fromBool(true));
                r.set("summary", Json::string("Превью эффекта убрано"));
                return r;
            }
            stopSpectraGpuPreview(); // a CPU fx_preview replaces Metal preview
            if (ws_.project.dir.empty()) throw std::runtime_error("no game is open (open_game)");
            const std::string name = p.getStr("effect");
            if (!validFxName(name) || name == "fx") throw std::runtime_error("'effect' must be an effect name (fx_list)");
            // an fx_set / import this frame has not been applied yet: load the new files now
            if (std::any_of(ws_.changes.begin(), ws_.changes.end(), [](const Change& c) { return c.kind == Change::Fx; })) reloadFx();
            if (p.has("quality")) {
                fx::FxQuality q;
                if (!fx::qualityFromName(p.getStr("quality"), q)) throw std::runtime_error("quality: low, medium or high");
                gameFx_.setQuality(q);
            }
            gameFx_.clearErrors();
            const int id = gameFx_.effect(name);
            if (id < 0) {
                std::string why;
                for (auto& e : gameFx_.errors()) why += "\n" + e;
                throw std::runtime_error("effect '" + name + "' cannot be played:" + (why.empty() ? std::string(" not found") : why));
            }
            fx::float4 tint{1, 1, 1, 1};
            if (p.has("color") && !fx::parseColor(p["color"], tint)) throw std::runtime_error("color must be \"#rrggbb\" or \"#rrggbbaa\"");
            render::RTSCamera& cam = playing() ? gameCam : sceneCam;
            const float x = p.has("x") ? float(p["x"].toDouble()) : cam.target.x, z = p.has("z") ? float(p["z"].toDouble()) : cam.target.z;
            const float ground = sim_ ? view_.heightAt(simModule()->state(*sim_).grid, x, z) : view_.heightAt(grid_, x, z);
            const fx::float3 pos{x, ground + float(p["y"].toDouble(0)), z};
            cam.target = {x, ground, z};
            if (p.has("distance")) cam.distance = std::clamp(float(p["distance"].toDouble()), cam.minDistance, cam.maxDistance);
            else if (cam.distance > 22) cam.distance = std::max(cam.minDistance, 14.0f);
            if (p.has("pitch")) cam.pitch = std::clamp(float(p["pitch"].toDouble()), 10.0f, 89.0f) * 3.14159265f / 180.0f;
            if (!playing()) sceneFlying_ = false;
            // culling uses the camera of the last frame: give it the new one before play()
            Viewport* vp = playing() ? gameViewport : sceneViewport;
            const int w = vp && vp->w > 0 ? vp->w : 1280, h = vp && vp->h > 0 ? vp->h : 720;
            gameFx_.setView(cam, cam.proj(float(w) / float(h), dev_ && dev_->info().clipDepthZeroToOne) * cam.view(), w, h);
            gameFx_.kill(fxPreview_.h);
            const float scale = std::clamp(float(p["scale"].toDouble(1)), 0.05f, 20.0f);
            fx::FxHandle hnd = gameFx_.play(id, pos, {0, 1, 0}, scale, tint);
            if (!hnd.valid())
                throw std::runtime_error(gameFx_.quality() == fx::FxQuality::Off ? "particles are switched off (quality off)" : "the effect was not started (instance budget full)");
            const fx::EffectDef* d = gameFx_.def(id);
            const float advance = std::clamp(float(p["advance"].toDouble(0)), 0.0f, 10.0f);
            const bool moving = p.getBool("moving");
            float moveT = 0;
            for (int i = 0, n = int(std::lround(advance * 60)); i < n; ++i) {
                if (moving) orbit(gameFx_, hnd, pos, moveT += 1.0f / 60.0f);
                gameFx_.update(1.0f / 60.0f);
            }
            fxFrozen_ = p.getBool("freeze");
            fxPreview_ = {name, pos, scale, tint, p.getBool("loop"), hnd, time_ + (d && !d->looping() ? std::min(8.0, double(d->lifetime()) + 0.5) : 0.5),
                          moving, moveT};
            fxTime_ = time_;
            r.set("ok", Json::fromBool(true));
            r.set("effect", Json::string(name));
            r.set("x", Json::number(double(pos.x), 2));
            r.set("y", Json::number(double(pos.y), 2));
            r.set("z", Json::number(double(pos.z), 2));
            r.set("view", Json::string(playing() ? "game" : "scene"));
            r.set("particles", Json::number(int64_t(gameFx_.stats().particles)));
            r.set("quality", Json::string(fx::qualityName(gameFx_.quality())));
            if (d) {
                r.set("emitters", Json::number(int64_t(d->emitters.size())));
                r.set("looping", Json::fromBool(d->looping()));
                if (!d->looping()) r.set("lifetime", Json::number(double(d->lifetime()), 2));
            }
            r.set("next", Json::string(std::string("view_screenshot {\"window\": \"") + (playing() ? "game" : "scene") + "\"}"));
            r.set("summary", Json::string("Превью эффекта " + name));
            return r;
        },
        true});

    // SpectraFX graph-v1 preview: compile and play entirely in editor memory.
    // Unlike fx_graph_bake this writes no temporary fx/*.json files.
    ws_.registerOp({"fx_graph_preview",
        "Compile SpectraFX graph v1 in memory and preview it in the Scene/Game view. "
        "Accepts graph JSON, optional exposed-parameter overrides and x/y/z (absolute world position). "
        "The current preview is replaced; old fx/*.json files remain untouched.",
        Json::parse(R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"overrides":{"type":"object"},
            "x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},
            "advance":{"type":"number"},"loop":{"type":"boolean"},"freeze":{"type":"boolean"},"gpu":{"type":"boolean"},"instanceValues":{"type":"object"}},"required":["graph"]})"),
        [this](const Json& p)->Json {
            if(ws_.project.dir.empty())throw std::runtime_error("open a game project first");
            fx::EffectDef compiled;
            std::vector<std::string> errors;
            const Json overrides=p.has("overrides")?p["overrides"]:Json::object();
            if(!spectra::compileGraph(p["graph"],overrides,compiled,errors)) {
                std::string why="SpectraFX graph compile failed:";
                for(const auto& e:errors)why+="\n"+e;
                throw std::runtime_error(why);
            }
            // The graph stays compiled once. Each preview instance receives
            // its own eight scalar VM inputs before the first particle Spawn,
            // including prewarmed particles.
            spectra::InstanceParameterTable definitions;
            if(!spectra::loadInstanceParameterTable(
                p["graph"]["instanceParameters"],definitions,errors))
                throw std::runtime_error(errors.empty()?
                    "invalid SpectraFX instance parameters":errors.back());
            std::array<float,8> instanceParams{};
            for(const auto& [name,def]:definitions) {
                (void)name;
                instanceParams[def.slot]=def.initial;
            }
            const Json& values=p["instanceValues"];
            if(!values.isNull()&&!values.isObject())
                throw std::runtime_error("instanceValues must be a JSON object");
            if(values.isObject())
                for(const auto& [name,value]:values.members()) {
                    const auto it=definitions.find(name);
                    if(it==definitions.end() || !value.isNumber() ||
                       !spectra::checkInstanceParameterValue(it->second,
                           float(value.toDouble())))
                        throw std::runtime_error("invalid SpectraFX instanceValues."+name);
                    instanceParams[it->second.slot]=float(value.toDouble());
                }
            render::RTSCamera& cam=playing()?gameCam:sceneCam;
            // x/y/z become the camera target and the emitter position: reject
            // non-numbers, NaN/inf and absurd magnitudes before using them.
            const auto coordinate=[&](const char* key,float fallback)->float {
                if(!p.has(key) || p[key].isNull())return fallback;
                const double v=p[key].toDouble(std::nan(""));
                if(!p[key].isNumber() || !std::isfinite(v) || std::abs(v)>1.0e7)
                    throw std::runtime_error(std::string("fx_graph_preview: ")+key+
                        " must be a finite number within +-1e7");
                return float(v);
            };
            const fx::float3 pos{
                coordinate("x",cam.target.x),
                coordinate("y",cam.target.y),
                coordinate("z",cam.target.z)
            };
            cam.target={pos.x,pos.y,pos.z};
            if(!playing())sceneFlying_=false;
            Viewport* vp=playing()?gameViewport:sceneViewport;
            const int width=vp&&vp->w>0?vp->w:1280;
            const int height=vp&&vp->h>0?vp->h:720;
            if(p.getBool("gpu")) {
                if(!dev_ || dev_->info().backend!=gfx::Backend::Metal ||
                   !dev_->supportsCompute())
                    throw std::runtime_error("SpectraFX GPU preview requires a Metal compute device");
                spectra::GraphRuntimeOptions options;
                options.preference=spectra::ComputePreference::RequireGpu;
                options.gpuProgram=true;options.gpuMaterial=true;
                const auto eligibility=spectra::inspectGraphGpuEligibility(compiled,options);
                if(!eligibility.eligible)
                    throw std::runtime_error("SpectraFX Metal GPU preview rejected: "+eligibility.reason);
                gameFx_.kill(fxPreview_.h);
                fxPreview_={};fxFrozen_=false;
                queueSpectraGpuPreview(compiled,instanceParams,pos,
                    playing()&&!ws_.project.isFps(),p.getBool("loop"));
                Json response=Json::object();
                response.set("ok",Json::fromBool(true));
                response.set("queued",Json::fromBool(true));
                response.set("backend",Json::string("Metal"));
                response.set("view",Json::string(playing()&&!ws_.project.isFps()?"game":"scene"));
                response.set("summary",Json::string("Metal preview will start before the next scene render pass"));
                return response;
            }
            stopSpectraGpuPreview();
            gameFx_.setView(cam,cam.proj(float(width)/float(height),
                dev_&&dev_->info().clipDepthZeroToOne)*cam.view(),width,height);

            gameFx_.kill(fxPreview_.h);
            const std::string runtimeName="__spectrafx_editor_preview";
            const int id=gameFx_.define(runtimeName,compiled);
            if(id<0)throw std::runtime_error("SpectraFX preview definition rejected");
            const fx::FxHandle handle=gameFx_.playWithProgramParameters(
                id,pos,{0,1,0},1.f,{1,1,1,1},instanceParams);
            if(!handle.valid())throw std::runtime_error("SpectraFX preview could not start (culling or particle quality)");
            double advanceSeconds=p["advance"].toDouble(0.12);
            if(!std::isfinite(advanceSeconds))advanceSeconds=0.12; // NaN would make the loop count UB
            const float advance=std::clamp(float(advanceSeconds),0.f,10.f);
            for(int i=0;i<int(advance*60.f);++i)gameFx_.update(1.f/60.f);
            fxFrozen_=p.getBool("freeze");
            fxPreview_={runtimeName,pos,1.f,{1,1,1,1},p.getBool("loop"),handle,
                time_+(compiled.looping()?0.5:std::min(8.0,double(compiled.lifetime())+0.5)),false,0.f};
            fxPreview_.programParams=instanceParams;
            fxTime_=time_;
            Json response=Json::object();
            response.set("ok",Json::fromBool(true));
            response.set("name",Json::string(p.getStr("name","SpectraFX graph")));
            response.set("emitters",Json::number(int64_t(compiled.emitters.size())));
            response.set("particles",Json::number(int64_t(gameFx_.stats().particles)));
            response.set("view",Json::string(playing()?"game":"scene"));
            response.set("next",Json::string(playing()?"view_screenshot {\"window\":\"game\"}":"view_screenshot {\"window\":\"scene\"}"));
            return response;
        },true});
    // PHYSIS «Живая природа» in the Scene view (PhysisNaturePreview.h).
    ws_.registerOp({"physis_nature_preview",
        "Show PHYSIS wildfire/wind as real scene presentation without a play-test: TerraWeave grass that burns to charred "
        "stubble, SpectraFX flames/embers/smoke bent by the wind, fire light and fire/wind sound. Uses the map's authored "
        "physis block in a PRIVATE preview World (never the game, never saved); a running game's PHYSIS is shown read-only. "
        "Fields are optional: enabled, playing, speed, windSpeed (m/s), windDirection (degrees the wind blows towards, 0=+X), "
        "humidity 0..1, cellMeters (0 = map grid, else detailed preview cell), grass, flames, sound, volume, "
        "shoreSand (m of bare sand along authored NEREID water: no grass, no fuel; default 7), "
        "ignite {x,z} (scene metres; preview only), advanceTicks (0..600, preview only), reset. Then view_screenshot {window:'scene'}.",
        Json::parse(R"({"properties":{"enabled":{"type":"boolean"},"playing":{"type":"boolean"},"speed":{"type":"number"},
            "windSpeed":{"type":"number"},"windDirection":{"type":"number"},"humidity":{"type":"number"},
            "cellMeters":{"type":"integer","minimum":0,"maximum":64},"grass":{"type":"boolean"},"flames":{"type":"boolean"},
            "sound":{"type":"boolean"},"volume":{"type":"number"},"shoreSand":{"type":"number"},
            "ignite":{"type":"object","properties":{"x":{"type":"number"},"z":{"type":"number"}},"required":["x","z"]},
            "advanceTicks":{"type":"integer","minimum":0,"maximum":600},"reset":{"type":"boolean"}},"additionalProperties":false})"),
        [this](const Json& p) -> Json {
            auto& nature = physisNaturePreview_;
            auto& st = nature.settings;
            const auto num = [&](const char* key, float lo, float hi, float& out) {
                if (!p.has(key)) return false;
                const double v = p[key].toDouble(out);
                if (!std::isfinite(v)) throw std::invalid_argument(std::string(key) + " must be finite");
                out = std::clamp(float(v), lo, hi);
                return true;
            };
            if (p.has("enabled")) { const bool was = st.enabled; st.enabled = p.getBool("enabled"); if (st.enabled && !was) nature.requestReset(); }
            if (p.has("playing")) st.playing = p.getBool("playing");
            if (p.has("grass")) st.grass = p.getBool("grass");
            if (p.has("flames")) st.flames = p.getBool("flames");
            if (p.has("sound")) st.sound = p.getBool("sound");
            num("speed", 0.f, 8.f, st.speed);
            num("volume", 0.f, 1.f, st.volume);
            bool wind = num("windSpeed", 0.f, 60.f, st.windSpeed);
            wind = num("windDirection", -360.f, 360.f, st.windDirectionDeg) || wind;
            if (num("humidity", 0.f, 1.f, st.humidity)) nature.requestReset();
            if (num("shoreSand", 0.f, 40.f, st.shoreSandMeters)) nature.requestReset();
            if (p.has("cellMeters")) { st.previewCellMeters = int(std::clamp<int64_t>(p.getInt("cellMeters"), 0, 64)); nature.requestReset(); }
            if (wind) nature.applyWindSettings();
            if (p.getBool("reset")) nature.requestReset();
            Json r = Json::object();
            const bool needsWorld = p.has("ignite") || p.getInt("advanceTicks", 0) > 0;
            if (needsWorld) {
                if (!st.enabled) throw std::runtime_error("enable the nature preview first (enabled: true)");
                // Build the preview World now so agents can act in one call.
                nature.setWater(authoredWater());
                nature.prepareSource(gameFx_, doc(), sim_ ? &sim_->world() : nullptr,
                    [this](float x, float z) { return surfaceHeightAt(x, z); });
                if (nature.usingLive()) throw std::runtime_error("the running game's PHYSIS is read-only");
            }
            if (p.has("ignite")) {
                const auto& at = p["ignite"];
                std::string why;
                const bool ok = nature.ignite(float(at["x"].toDouble()), float(at["z"].toDouble()), &why);
                r.set("ignited", Json::fromBool(ok));
                if (!ok) r.set("igniteError", Json::string(why));
            }
            const int64_t ticks = std::clamp<int64_t>(p.getInt("advanceTicks", 0), 0, 600);
            if (ticks > 0) r.set("advanced", Json::number(int64_t(nature.advance(int(ticks)))));
            r.set("ok", Json::fromBool(true));
            r.set("enabled", Json::fromBool(st.enabled));
            r.set("source", Json::string(nature.usingLive() ? "live (read-only)" : "preview"));
            r.set("tick", Json::number(int64_t(nature.tick())));
            r.set("flames", Json::number(int64_t(nature.flames())));
            r.set("smoke", Json::number(int64_t(nature.smoke())));
            r.set("voices", Json::number(int64_t(nature.voices())));
            r.set("grassPatches", Json::number(int64_t(nature.grassPatches())));
            r.set("burningAreaM2", Json::number(double(nature.burningArea()), 1));
            if (!nature.status().empty()) r.set("status", Json::string(nature.status()));
            r.set("next", Json::string("view_screenshot {\"window\":\"scene\"}"));
            return r;
        }, true});
}

} // namespace aiverse::editor
