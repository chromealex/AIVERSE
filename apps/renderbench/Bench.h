// Fixed-workload rendering benchmark. No simulation, adaptive quality, or asset normalization.
#pragma once
#include "engine/platform/Platform.h"
#include "engine/render/PostFx.h"
#include "engine/render/ScreenLighting.h"
#include "SceneAssets.h"
#include "engine/render/StaticPointBounce.h"
#include "CameraTour.h"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <thread>

namespace aiverse::bench {
using namespace render;
using Clock = std::chrono::steady_clock;
inline double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
struct Options {
    std::string scene, output="benchmark.json", profile="baseline", capture;
    int warmup=90, samples=240, camera=-1;
    std::string tourPath; float tourStart=0;
    std::string captureSequence; float motionYaw=0, freezeTime=-1, previewSeconds=0, motionSpeed=0.018f;
    static Options parse(int argc,char** argv) {
        Options o;
        for(int i=1;i<argc;++i) {
            std::string k=argv[i];if(i+1>=argc)throw std::runtime_error("missing option value: "+k);
            std::string v=argv[++i];
            if(k=="--scene")o.scene=v;else if(k=="--output")o.output=v;else if(k=="--profile")o.profile=v;
            else if(k=="--tour")o.tourPath=v;else if(k=="--tour-start")o.tourStart=std::stof(v);
            else if(k=="--capture")o.capture=v;else if(k=="--warmup")o.warmup=std::stoi(v);
            else if(k=="--preview-seconds")o.previewSeconds=std::stof(v);else if(k=="--motion-speed")o.motionSpeed=std::stof(v);
            else if(k=="--sequence")o.captureSequence=v;else if(k=="--motion-yaw")o.motionYaw=std::stof(v);else if(k=="--freeze-time")o.freezeTime=std::stof(v);
            else if(k=="--samples")o.samples=std::stoi(v);else if(k=="--camera")o.camera=std::stoi(v);
            else throw std::runtime_error("unknown option: "+k);
        }
        if(!o.tourPath.empty())o.camera=0;
        if(!std::isfinite(o.tourStart)||o.tourStart<0)throw std::runtime_error("invalid tour start");
        if(!o.tourPath.empty()&&(!o.captureSequence.empty()||o.motionYaw!=0))throw std::runtime_error("tour cannot use frame-step validation motion or sequence capture");
        if(o.scene.empty()||o.warmup<1||o.samples<30||o.samples>100000||o.warmup>10000||o.camera < -1)throw std::runtime_error("require --scene; warmup 1..10000; samples 30..100000; camera >= 0 (or omit)");
        if(o.profile!="baseline"&&o.profile!="hdr"&&o.profile!="low"&&o.profile!="full")throw std::runtime_error("profile: baseline, hdr, full or low");
        if(!std::isfinite(o.motionYaw)||!std::isfinite(o.freezeTime)||std::abs(o.motionYaw)>0.1f||o.freezeTime < -1)throw std::runtime_error("invalid validation motion/time");
        if(!o.captureSequence.empty()&&o.camera<0)throw std::runtime_error("sequence capture requires one --camera");
        if(!std::isfinite(o.previewSeconds)||o.previewSeconds<0||o.previewSeconds>3600||!std::isfinite(o.motionSpeed)||std::abs(o.motionSpeed)>1)throw std::runtime_error("invalid preview duration/speed");
        if(o.previewSeconds>0&&(!o.captureSequence.empty()||o.camera<0))throw std::runtime_error("preview requires one camera and no sequence readback");
        return o;
    }
};
struct Bench {
    Options options;
    Json config,report=Json::object(),rows=Json::array();
    platform::HostedView content;
    Renderer3D renderer;
    PostFx post;
    ScreenLighting screen;
    SceneAssets additions;
    CameraTour tour; int tourShot=-1;
    std::unique_ptr<Model> model;
    std::vector<RTSCamera> cameras;
    std::vector<std::string> cameraNames;
    std::map<uint64_t,size_t> tags;
    gfx::PassDesc output;
    std::function<void(gfx::Device::CaptureFn)> readOutput;
    int width=1280,height=720,view=0,frame=0,drain=0,sequenceCaptures=0;
    bool sequencePending=false, ownsFrame=true;
    uint64_t tag=0;
    bool done=false,failed=false,gpuSupported=true,captureRequested=false,captured=false;
    std::string error;
    Clock::time_point lastProgress=Clock::now(), previewStart;
    explicit Bench(Options o):options(std::move(o)) {
        config=Json::parseFile(options.scene);
        if(!options.tourPath.empty()){
            auto track=Json::parseFile(options.tourPath);tour.load(track);
            if(options.tourStart>=tour.duration)throw std::runtime_error("tour start is past the end");
            options.previewSeconds=float(options.previewSeconds>0?std::min(double(options.previewSeconds),tour.duration-options.tourStart):tour.duration-options.tourStart);
            report.set("cameraTour",track);
        }
        width=int(config.getInt("width",1280));height=int(config.getInt("height",720));
        if(width<64||height<64||width>8192||height>8192)throw std::runtime_error("invalid resolution");
        content.contentRoot=std::filesystem::absolute(options.scene).parent_path().string();
        if(!options.captureSequence.empty())std::filesystem::create_directories(options.captureSequence);
        content.width=float(width);content.height=float(height);content.fbWidth=width;content.fbHeight=height;
    }
    static float3 v3(const Json& j) { if(!j.isArray()||j.size()!=3)throw std::runtime_error("expected xyz vector");return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())}; }
    static bool secondaryUv(const Json& j) {
        if(j.isObject())for(const auto& [key,value]:j.members()) {
            if(key=="texCoord"&&value.integer()!=0)return true;
            if(secondaryUv(value))return true;
        }
        if(j.isArray())for(const auto& value:j.items())if(secondaryUv(value))return true;
        return false;
    }
    void init(gfx::Device& d) {
        platform::setHostedView(&content);
        if(!options.captureSequence.empty())d.setScreenReadable(true);
        try {
            for(size_t i=0;i<config["cameras"].size();++i) {
                if(options.camera>=0&&int(i)!=options.camera)continue;
                auto& j=config["cameras"][i];RTSCamera c;c.target=v3(j["target"]);auto delta=v3(j["eye"])-c.target;
                c.distance=length(delta);if(c.distance<0.001f)throw std::runtime_error("degenerate camera");
                c.pitch=std::asin(delta.y/c.distance);c.yaw=std::atan2(delta.x,delta.z);
                c.fovY=float(j["fovYDegrees"].toDouble(config["fovYDegrees"].toDouble(60)))*3.14159265f/180;
                c.zNear=float(config["near"].toDouble(.1));c.zFar=float(config["far"].toDouble(200));
                cameras.push_back(c);cameraNames.push_back(j.getStr("name"));
            }
            if(cameras.empty())throw std::runtime_error("no selected cameras");
            if(!renderer.init(d)||!post.init(d)||(options.profile=="full"&&!screen.init(d,post)))throw std::runtime_error(d.lastError());
            bool unusedSecondaryUv=false;
            const auto path=config.getStr("model");
            if(std::filesystem::path(path).extension()==".gltf") {
                std::string text;
                if(platform::readContentFile(path,text))unusedSecondaryUv=!secondaryUv(Json::parse(text)["materials"]);
            }
            report.set("unusedSecondaryUvVerified",Json::fromBool(unusedSecondaryUv));
            std::vector<std::string> warnings;
            const auto start=Clock::now();model=loadModel(config.getStr("model"),platform::readContentFile,&warnings);
            Json warn=Json::array(),notes=Json::array();
            for(auto& w:warnings) {
                // Explicit benchmark cameras/lighting replace these imported nodes; keep the audit trail.
                const bool replaced=w.ends_with("cameras are ignored")||w.ends_with("cameras and lights are ignored")||w.ends_with("extension KHR_lights_punctual is ignored")||(unusedSecondaryUv&&w.ends_with("TEXCOORD_1 is ignored"));
                (replaced?notes:warn).push(Json::string(w));std::fprintf(stderr,"asset %s: %s\n",replaced?"note":"warning",w.c_str());
            }
            const float normalScale=float(config["mainNormalScale"].toDouble(1));
            if(!std::isfinite(normalScale)||normalScale<0||normalScale>2)throw std::runtime_error("mainNormalScale must be in 0..2");
            for(auto& material:model->materials)material.normalScale*=normalScale;
            additions.applyGraphs(*model,config["materialGraphs"]);
            const bool bakePointShadows=config.getBool("staticPointShadows",false)&&options.profile!="low";
            if(bakePointShadows)appendShadowGeometry(additions.shadowGeometry,*model);
            additions.load(d,config,warn,notes,bakePointShadows);
            const size_t primaryLightCount=additions.lights.size();
            if(config.has("staticPointBounce")&&!bakePointShadows&&options.profile!="low")throw std::runtime_error("staticPointBounce requires static point shadows");
            std::vector<StaticPointShadow> pointShadows;
            StaticDiffuseField diffuseField;
            if(bakePointShadows){
                if(!config.getBool("staticPointLightGrid",false)||additions.lights.size()>32)throw std::runtime_error("static point shadows require a grid and at most 32 lights");
                const double requested=config["pointShadowResolution"].toDouble(128);
                if(!std::isfinite(requested)||requested<16||requested>256||std::floor(requested)!=requested)throw std::runtime_error("pointShadowResolution must be an integer in 16..256");
                const int resolution=int(requested);
                if(additions.lights.size()*size_t(6*resolution*resolution+1)>4194304)throw std::runtime_error("point shadow maps exceed atlas budget");
                std::fprintf(stderr,"Baking static point shadows: %zu triangles, %zu lights, %d pixels per face\n",additions.shadowGeometry.triangles.size(),additions.lights.size(),resolution);
                const auto shadowStart=Clock::now();
                additions.shadowGeometry.build();
                for(const auto& light:additions.lights)pointShadows.push_back(additions.shadowGeometry.bake(light.pos,light.radius,resolution));
                report.set("staticPointShadowBakeMs",Json::number(ms(shadowStart,Clock::now())));
                if(config.has("staticPointBounce")){
                    const auto& settings=config["staticPointBounce"];
                    const double samples=settings["samples"].toDouble(16);
                    if(!std::isfinite(samples)||samples<4||samples>32||samples!=std::floor(samples))throw std::runtime_error("bounce samples must be an integer in 4..32");
                    const auto bounceStart=Clock::now();
                    auto bounce=bakePointBounce(additions.shadowGeometry,additions.lights,int(samples),float(settings["radius"].toDouble(5)),float(settings["strength"].toDouble(1)));
                    diffuseField=bakeDiffuseField(additions.shadowGeometry,additions.lights,bounce,float(settings["spacing"].toDouble(2)));
                    report.set("staticDiffuseProbes",Json::number(int64_t(diffuseField.probes.size())));
                    report.set("staticBounceSources",Json::number(int64_t(bounce.size())));
                    report.set("staticBounceBakeMs",Json::number(ms(bounceStart,Clock::now())));
                    std::fprintf(stderr,"Static bounce: %zu surface emitters\n",bounce.size());
                }
                report.set("staticPointShadowMaps",Json::number(int64_t(pointShadows.size())));
            }
            additions.shadowGeometry=StaticShadowGeometry{};
            if(config.getBool("staticPointLightGrid",false)&&options.profile!="low"&&!renderer.lights.setStaticPointLights(additions.lights,float(config["staticLightCellSize"].toDouble(2)),pointShadows.empty()?nullptr:&pointShadows,diffuseField.probes.empty()?nullptr:&diffuseField))throw std::runtime_error("static light grid upload failed");
            report.set("additions",additions.audit);
            report.set("sourcePointLights",Json::number(int64_t(primaryLightCount)));
            report.set("totalGridLights",Json::number(int64_t(additions.lights.size())));
            report.set("assetWarnings",warn);report.set("importNotes",notes);
            report.set("vertices",Json::number(int64_t(model->vertices.size())));report.set("indices",Json::number(int64_t(model->indices.size())));
            uint64_t triangles=0;
            for(const auto& node:model->nodes)if(node.mesh>=0)for(const auto& primitive:model->meshes[size_t(node.mesh)].primitives)triangles+=primitive.indexCount/3;
            for(const auto& entry:additions.audit.items())triangles+=uint64_t(entry["triangles"].integer());
            report.set("sceneTriangles",Json::number(int64_t(triangles)));
            report.set("materials",Json::number(int64_t(model->materials.size())));report.set("textures",Json::number(int64_t(model->images.size())));
            auto bounds=[](float3 v){Json j=Json::array();j.push(Json::number(double(v.x)));j.push(Json::number(double(v.y)));j.push(Json::number(double(v.z)));return j;};
            report.set("boundsMin",bounds(model->boundsMin));report.set("boundsMax",bounds(model->boundsMax));
            if(!uploadModel(d,*model))throw std::runtime_error("model upload failed: "+d.lastError());
            report.set("loadAndUploadMs",Json::number(ms(start,Clock::now())));
            const auto& optim=config["modelOptimization"];
            renderer.setModelLodEnabled(optim.getBool("lod",true));
            renderer.setModelCullingEnabled(optim.getBool("culling",true));
            renderer.setModelInstancingEnabled(optim.getBool("instancing",true));
            renderer.setShadowQuality(options.profile=="low"?ShadowQuality::Low:ShadowQuality::High);
            renderer.setMaterialQuality(options.profile=="low"?MaterialQuality::Medium:MaterialQuality::High);
            renderer.setTraceEnabled(options.profile=="full");renderer.setModelReflectionsEnabled(options.profile=="full");
            renderer.lights.setQuality(options.profile=="low"||additions.lights.empty()?LightQuality::Off:LightQuality::High);
            renderer.lighting.sunDir=v3(config["sunDirection"]);renderer.lighting.sunColor=v3(config["sunColor"]);
            renderer.lighting.sunIntensity=float(config["sunIntensity"].toDouble());
            renderer.lighting.skyColor=v3(config["skyColor"]);renderer.lighting.groundColor=v3(config["groundColor"]);
            renderer.lighting.fogDensity=float(config["fogDensity"].toDouble(0));
            if(config.has("fogColor"))renderer.lighting.fogColor=v3(config["fogColor"]);
            PostFxSettings s;s.hdr=options.profile=="low"?HdrMode::Off:HdrMode::On;
            if(options.profile=="hdr"||options.profile=="full")s=parsePostFx(Json::parse(R"({"hdr":"on","effects":[{"type":"bloom","threshold":1.5,"intensity":0.25},{"type":"fxaa"}]})"),Json());
            if(options.profile=="full")s=parsePostFx(Json::parse(R"({"hdr":"on","effects":[{"type":"bloom","threshold":1.5,"intensity":0.25}]})"),Json());
            if(options.profile!="low"&&config.has("postfx"))s=parsePostFx(config["postfx"],Json());
            screen.setDiffuseBounceEnabled(!(bakePointShadows&&config.has("staticPointBounce")));
            screen.setEnabled(options.profile=="full");
            post.setSettings(s);post.setQuality(options.profile=="low"?PostFxQuality::Low:PostFxQuality::High);
            report.set("schema",Json::number(int64_t(1)));report.set("engine",Json::string("AIVERSE"));
#if defined(__aarch64__) || defined(__arm64__)
            report.set("binaryArchitecture",Json::string("arm64"));
#elif defined(__x86_64__) || defined(_M_X64)
            report.set("binaryArchitecture",Json::string("x86_64"));
#else
            report.set("binaryArchitecture",Json::string("other"));
#endif
            report.set("revision",Json::string(AIVERSE_BENCH_REVISION));report.set("build",Json::string(AIVERSE_BENCH_BUILD));
            std::string hardware=d.info().name;
            if(auto slash=hardware.find(" / ");slash!=std::string::npos)hardware.resize(slash);
            if(auto space=hardware.find(' ');space!=std::string::npos)hardware=hardware.substr(space+1);
            report.set("hardware",Json::string(hardware));
            report.set("gpuTimer",Json::string(d.info().backend==gfx::Backend::Metal?"Metal command-buffer GPUStartTime/GPUEndTime":"GL_TIME_ELAPSED; may include CPU submission gaps"));
            report.set("device",Json::string(d.info().name));report.set("profile",Json::string(options.profile));
            report.set("scene",config);report.set("warmupPerCamera",Json::number(int64_t(options.warmup)));
            report.set("samplesPerCamera",Json::number(int64_t(options.samples)));
            report.set("presentation",Json::string(readOutput?"offscreen GL; no swap":"platform window; refresh pacing may cap throughput"));
            std::printf("AIVERSE render benchmark: %s, %zu cameras, %d x %d\n",d.info().name.c_str(),cameras.size(),width,height);
            std::printf("%s\n",report.dump().c_str());std::fflush(stdout);
        } catch(const std::exception& e) {fail(e.what());}
        platform::setHostedView(nullptr);lastProgress=Clock::now();
    }
    void fail(std::string s) {failed=true;done=true;error=std::move(s);std::fprintf(stderr,"benchmark: %s\n",error.c_str());}
    void poll(gfx::Device& d) {
        for(auto t:d.takeGpuTimings())if(auto it=tags.find(t.tag);it!=tags.end()) {
            rows.at(it->second).set("gpuMs",t.valid?Json::number(t.milliseconds,6):Json());tags.erase(it);lastProgress=Clock::now();
        }
    }
    void draw(gfx::Device& d) {
        if(done)return;
        poll(d);
        // Match the native three-frame queue without inserting GPU waits into a timed frame.
        if(tags.size()>=3){if(ms(lastProgress,Clock::now())>30000)fail("GPU timer completion timeout");else std::this_thread::sleep_for(std::chrono::milliseconds(1));return;}
        const bool measuring=options.previewSeconds<=0&&view<int(cameras.size())&&frame>=options.warmup&&frame<options.warmup+options.samples;
        const int ci=std::min(view,int(cameras.size())-1);
        const auto start=Clock::now();if(ownsFrame)d.beginFrame(width,height);const auto encodeStart=Clock::now();
        const uint64_t thisTag=++tag;
        bool timed=false;if(measuring){timed=d.timeGpuFrame(thisTag);gpuSupported&=timed;}
        if(frame==0&&view<int(cameras.size())){screen.reset();additions.selectLights(cameras[size_t(ci)].eye());}
        if(options.previewSeconds>0&&frame==options.warmup&&view<int(cameras.size()))previewStart=Clock::now();
        const double previewTime=previewStart==Clock::time_point{}?0.0:std::clamp(ms(previewStart,Clock::now())/1000.0,0.0,double(options.previewSeconds));
        auto camera=cameras[size_t(ci)];
        camera.yaw+=options.previewSeconds>0?options.motionSpeed*float(previewTime):options.motionYaw*float(frame);
        if(!tour.shots.empty()){
            int shot;auto pose=tour.sample(options.tourStart+previewTime,shot);
            pose.zNear=camera.zNear;pose.zFar=camera.zFar;camera=pose;
            if(shot!=tourShot){screen.reset();additions.selectLights(camera.eye());tourShot=shot;}
        }
        renderer.beginScene(screen.jittered(camera,width,height),width,height);
        ModelDraw md;md.model=model.get();renderer.drawModel(md);
        additions.draw(renderer,options.freezeTime>=0?options.freezeTime:options.previewSeconds>0?float(previewTime+options.tourStart):float(view<int(cameras.size())?frame:options.warmup+options.samples-1)/60.0f,options.profile!="low");
        auto pass=output;auto clear=SceneAssets::vector3(config["clearColor"],{.1f,.15f,.23f});pass.clearColor[0]=clear.x;pass.clearColor[1]=clear.y;pass.clearColor[2]=clear.z;
        renderer.render(pass,options.profile=="full"?static_cast<PostProcess*>(&screen):static_cast<PostProcess*>(&post));renderer.resolvePostFx();
        if(!report.has("renderSettings")) {
            Json settings=Json::object();settings.set("linearHdr",Json::fromBool(post.linearScene()));
            settings.set("shadows",Json::string(options.profile=="low"?"low":"high"));
            settings.set("shadowResolution",Json::number(int64_t(options.profile=="low"?1024:2048)));
            settings.set("shadowPcfSamples",Json::number(int64_t(options.profile=="low"?4:16)));
            settings.set("materials",Json::string(options.profile=="low"?"medium":"high"));
            settings.set("bloomFxaa",Json::fromBool(options.profile=="hdr"));
            settings.set("gi",Json::string(report.has("staticBounceSources")?"baked single diffuse bounce":screen.running()?"screen-space diffuse gather (16 samples)":"hemisphere ambient only"));
            settings.set("taa",Json::fromBool(screen.running()));
            settings.set("bloom",Json::fromBool(options.profile=="hdr"||options.profile=="full"));
            settings.set("animationStepSeconds",Json::number(1.0/60.0));
            settings.set("lightAttenuation",Json::string(renderer.lights.staticPointLightsActive()?"3D finite-radius attenuation / (1 + distance squared); linear RGB; unshadowed":"RTS finite-radius approximation; not photometric glTF"));
            settings.set("staticPointLightGrid",Json::fromBool(renderer.lights.staticPointLightsActive()));
            settings.set("pointLightBudget",Json::number(int64_t(options.profile=="low"?0:renderer.lights.staticPointLightsActive()?additions.lights.size():std::min(additions.lights.size(),LightSystem::kMaxLights))));settings.set("rayTracing",Json::fromBool(false));
            settings.set("ssr",Json::fromBool(renderer.ssrActive()));settings.set("dynamicResolution",Json::fromBool(false));
            settings.set("modelOptimization",config["modelOptimization"]);
            settings.set("validationMotionYawPerFrame",Json::number(double(options.motionYaw),8));
            settings.set("validationFreezeTime",Json::number(double(options.freezeTime)));
            settings.set("validationSequenceCapture",Json::fromBool(!options.captureSequence.empty()));
            settings.set("tourStart",Json::number(double(options.tourStart)));
            settings.set("previewSeconds",Json::number(double(options.previewSeconds)));
            settings.set("previewMotionRadiansPerSecond",Json::number(double(options.motionSpeed)));
            report.set("renderSettings",settings);
        }
        const auto encodeEnd=Clock::now();
        if(measuring&&!options.captureSequence.empty()&&!sequencePending&&sequenceCaptures<16) {
            sequencePending=true;
            const auto path=std::filesystem::path(options.captureSequence)/(std::to_string(ci)+"-"+std::to_string(sequenceCaptures)+".ppm");
            auto cb=[this,path](int w,int h,std::vector<uint8_t> px){
                if(w!=width||h!=height||px.size()!=size_t(w*h*4)){fail("invalid sequence capture");return;}
                std::ofstream file(path,std::ios::binary);file<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4)file.write(reinterpret_cast<const char*>(px.data()+i),3);
                if(!file)fail("cannot write sequence capture");
                sequencePending=false;++sequenceCaptures;
            };
            if(readOutput)readOutput(cb);else d.captureNextFrame(cb);
        }
        if(view>=int(cameras.size())&&!captureRequested&&!options.capture.empty()) {
            captureRequested=true;
            auto cb=[this](int w,int h,std::vector<uint8_t> px){
                if(w!=width||h!=height||px.size()!=size_t(w*h*4)){fail("invalid capture");return;}
                std::ofstream f(options.capture,std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4)f.write(reinterpret_cast<const char*>(px.data()+i),3);
                captured=bool(f);if(!captured)fail("cannot write capture");
            };
            if(readOutput)readOutput(cb);else d.captureNextFrame(cb);
        }
        d.endPass();if(ownsFrame)d.endFrame();const auto end=Clock::now();
        if(measuring){
            Json r=Json::object();r.set("camera",Json::string(cameraNames[size_t(ci)]));r.set("sample",Json::number(int64_t(frame-options.warmup)));
            r.set("cpuEncodeMs",Json::number(ms(encodeStart,encodeEnd),6));r.set("cpuFrameMs",Json::number(ms(start,end),6));
            r.set("gpuMs",Json());r.set("drawCalls",Json::number(int64_t(renderer.drawCalls())));
            const auto& stats=renderer.modelStats();
            r.set("submittedModelTriangles",Json::number(int64_t(stats.triangles)));
            r.set("culledModelNodes",Json::number(int64_t(stats.culledNodes)));
            r.set("instancedModelDraws",Json::number(int64_t(stats.instancedDraws)));
            Json lods=Json::array();for(auto n:stats.lodNodes)lods.push(Json::number(int64_t(n)));r.set("modelLodNodes",lods);
            r.set("postPasses",Json::number(int64_t(post.stats().passes+screen.passCount())));
            if(timed)tags.emplace(thisTag,rows.size());
            rows.push(std::move(r));
        }
        if(view<int(cameras.size())) {
            ++frame;
            if(options.previewSeconds>0 ? previewTime>=options.previewSeconds : frame==options.warmup+options.samples){++view;frame=0;}
        }
        else if((tags.empty()&&(options.capture.empty()||captured))||++drain>120) {
            if(!tags.empty()||(!options.capture.empty()&&!captured))fail("GPU/capture completion timeout");else done=true;
        }
        if(!renderer.modelError().empty())fail(renderer.modelError());
        if(!screen.lastError().empty())fail(screen.lastError());
        if(!post.lastError().empty())fail(post.lastError());
    }
    void shutdown(gfx::Device& d) {
        if(!options.captureSequence.empty()&&(sequenceCaptures!=16||sequencePending))fail("incomplete validation sequence; increase samples");
        if(!options.captureSequence.empty())d.setScreenReadable(false);
        poll(d);report.set("gpuTimingSupported",Json::fromBool(gpuSupported));report.set("complete",Json::fromBool(done&&!failed));
        report.set("error",Json::string(error));report.set("frames",rows);
        std::ofstream f(options.output);f<<report.dump()<<"\n";if(!f)fail("cannot write report");
        if(model)releaseModel(d,*model);
        additions.release(d);screen.shutdown();post.shutdown();renderer.shutdown();platform::setHostedView(nullptr);
    }
};
}
