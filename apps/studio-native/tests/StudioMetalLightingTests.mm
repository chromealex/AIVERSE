#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "apps/studio-native/StudioScene.h"
#include "apps/studio-native/StudioLightBakeJob.h"
#include "apps/studio-native/StudioLightBakeFiles.h"
#include "apps/studio-native/StudioLightingPreview.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <chrono>
using namespace aiverse;
int aiverseMain(int argc,char** argv) { @autoreleasepool {
    try {
        if(argc!=2)throw std::runtime_error("capture directory required");
        std::filesystem::create_directories(argv[1]);
        [NSApplication sharedApplication];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,160,160)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        CAMetalLayer* layer=[CAMetalLayer layer];layer.framebufferOnly=NO;
        window.contentView.wantsLayer=YES;window.contentView.layer=layer;
        [window orderFront:nil];
        studio::StudioScene scene(gfx::createMetalDevice((__bridge void*)layer));
        editor::MapDoc doc;doc.reset(8,8);
        // This GPU fixture explicitly authors a legacy terrain receiver.
        doc.data.extra.set("terrainPresentation",Json::string("legacy"));
        doc.data.extra.set("lights",Json::parse(R"([{"x":4,"z":4,"height":2,"radius":12,"intensity":6,"color":[1,0.3,0.1]}])"));
        const Json empty;
        {
            const auto project=std::filesystem::absolute(argv[1])/"baked-scene";
            std::filesystem::create_directories(project/"field.bake");
            const std::string asset="content/column.obj";
            std::filesystem::create_directories(project/"content");
            std::filesystem::remove(project/(asset+".lmuv"));
            std::ofstream(project/asset)<<"v -1 0 -1\nv 1 0 -1\nv 0 2 0\nv 0 0 1\nf 1 3 2\nf 2 3 4\nf 4 3 1\nf 1 2 4\n";
            auto read=[&](const std::string& path,std::string& bytes){
                std::ifstream in(project/path,std::ios::binary);if(!in)return false;
                bytes.assign(std::istreambuf_iterator<char>(in),{});return true;
            };
            editor::DocEntity column;column.type="column";column.x=2;column.z=4;
            doc.data.entities.push_back(column);
            column.x=6;column.type="runner";doc.data.entities.push_back(column);
            const auto rules=Json::parse(R"({"types":{"column":{"view":{"model":"content/column.obj"}},"runner":{"move":{"speed":3},"view":{"model":"content/column.obj"}}}})");
            const auto parsed=game::loadRules(rules);
            const auto originalLights=doc.data.extra["lights"];
            auto lights=originalLights;
            lights.push(Json::parse(R"({"x":1,"z":3,"bake":"mixed"})"));
            lights.push(Json::parse(R"({"x":5,"z":7,"bake":"baked","type":"spot"})"));
            doc.data.extra.set("lights",lights);
            doc.path=(project/"field.json").string();
            auto draw=[&]{scene.frame(160,160,doc,false,"","",empty,0,1.5f,project.string(),rules);};
            std::optional<bake::CapturedBake> cold;
            for(int i=0;i<5 && !cold;++i)cold=scene.prepareLightBake(doc,project.string(),parsed,rules,1.5f);
            if(!cold || scene.framesDrawn()!=0 || cold->draws.size()!=1 || cold->lights.size()!=2)
                throw std::runtime_error("Cold bake required a presented frame");
            draw();
            auto input=scene.captureLightBake(doc,project.string(),parsed);
            if(input.nx!=17 || input.nz!=17 || input.heights.size()!=289 || input.draws.size()!=1 ||
               input.draws.front().path!=asset || input.lights.size()!=2 || input.lights[0].bakedOnly || !input.lights[1].bakedOnly)
                throw std::runtime_error("Unexpected Studio bake capture");
            if(cold->heights!=input.heights || cold->draws.front().path!=input.draws.front().path ||
               cold->draws.front().nodeWorld.size()!=input.draws.front().nodeWorld.size() ||
               cold->sun.color.x!=input.sun.color.x || cold->sky.sky.y!=input.sky.sky.y)
                throw std::runtime_error("Cold and displayed capture disagree");
            std::cout<<"Captured a cold Studio map with zero presented frames\n";
            auto copy=doc;
            try {
                (void)scene.captureLightBake(copy,project.string(),parsed);
                throw std::logic_error("Accepted stale document for baking");
            } catch(const std::runtime_error&) {}
            input.folder="field.bake";input.settings.pageSize=64;input.settings.texelsPerUnit=2;
            input.settings.samples=8;input.settings.threads=2;input.settings.mode=bake::BakedMode::Subtractive;
            input.settings.probeSpacing=2;input.settings.probeHeight=2;input.settings.probeSamples=16;
            input.sun.on=false;input.sky.sky=input.sky.ground={.1f,.15f,2.f};
            studio::StudioLightBakeJob job;
            auto finish=[&]{
                for(int i=0;i<2000;++i){
                    if(auto result=job.poll())return *result;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                throw std::runtime_error("Timed out waiting for Studio background bake");
            };
            std::promise<void> entered,release;
            const auto gate=release.get_future().share();
            bool gated=false;
            job.start(input,[&](const auto& file,auto& bytes){
                if(!gated){gated=true;entered.set_value();gate.wait();}
                return read(file,bytes);
            },[](const auto&,const auto&){return true;});
            const bool started=entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready;
            job.cancel();release.set_value();
            if(!started || finish().error!="cancelled")throw std::runtime_error("Background cancellation failed");
            studio::StudioLightBakeFiles output(project,input.folder);
            job.start(input,[&](const auto& file,auto& bytes){return output.read(file,bytes);},
                [&](const auto& file,const auto& bytes){return output.stage(file,bytes);});
            const auto result=finish();
            if(job.progress().fraction<=0)throw std::runtime_error("Missing background bake progress");
            if(!result.error.empty())throw std::runtime_error(result.error);
            const std::string header=input.folder+"/lightmap.json";
            std::string previous;
            const bool hadHeader=read(header,previous);
            std::ofstream(project/header,std::ios::app)<<" ";
            try {
                output.publish();
                throw std::logic_error("Published over a changed light bake header");
            } catch(const std::runtime_error& e) {
                if(std::string(e.what())!="Baked output changed during calculation")throw;
            }
            if(std::filesystem::exists(project/(asset+".lmuv")))throw std::runtime_error("Published UVs before conflict check");
            if(hadHeader)std::ofstream(project/header,std::ios::binary)<<previous;
            else std::filesystem::remove(project/header);
            output.publish();
            std::string published;
            if(!read(header,published))throw std::runtime_error("Missing published bake");
            std::filesystem::remove(project/(asset+".lmuv"));
            studio::StudioLightBakeFiles recovery(project,input.folder);
            input.oldHash=result.hash;
            job.start(input,[&](const auto& file,auto& bytes){return recovery.read(file,bytes);},
                [&](const auto& file,const auto& bytes){return recovery.stage(file,bytes);});
            const auto recovered=finish();
            if(!recovered.unchanged || !recovered.generatedUvs)throw std::runtime_error("Did not recover missing UVs");
            recovery.publishRecoveredUvs();
            std::string restored;
            if(!read(header,restored) || restored!=published || !std::filesystem::exists(project/(asset+".lmuv")))
                throw std::runtime_error("Recovered UV publication changed baked output");
            std::cout<<"Recovered a missing model UV sidecar on an unchanged bake\n";
            std::cout<<"Conflicting bake header preserved, staged UV and atlas published on retry\n";
            std::cout<<"Captured static column, excluded moving unit, sampled 289 terrain heights\n";
            scene.setBakedSceneEnabled(true);
            for(int i=0;i<100 && (scene.bakedSceneStatus().getInt("lightmapped")<2 || !scene.bakedSceneStatus().getInt("probed"));++i){
                draw();std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if(!scene.bakedSceneStatus().getBool("loaded") || scene.bakedSceneStatus().getInt("lightmapped")<2 || !scene.bakedSceneStatus().getInt("probed"))
                throw std::runtime_error("Missing baked terrain draw: "+scene.bakedSceneStatus().dump(-1));
            auto capture=[&](const char* name){
                auto sample=std::make_shared<std::promise<std::vector<uint8_t>>>();auto ready=sample->get_future();
                scene.captureNextFrame([sample](int,int,std::vector<uint8_t> pixels){sample->set_value(std::move(pixels));});
                for(int i=0;i<80 && ready.wait_for(std::chrono::milliseconds(4))!=std::future_status::ready;++i)draw();
                if(ready.wait_for(std::chrono::seconds(2))!=std::future_status::ready)throw std::runtime_error("Bake framebuffer timeout");
                auto pixels=ready.get();std::ofstream file(project/name,std::ios::binary);file<<"P6\n160 160\n255\n";
                for(size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(&pixels[i]),3);
                return pixels;
            };
            std::cout<<"Terrain, matching and moved model receivers: "<<scene.bakedSceneStatus().dump(-1)<<"\n";
            const auto baked=capture("baked.ppm");
            scene.setBakedSceneEnabled(false);draw();
            const auto realtime=capture("realtime.ppm");
            size_t changed=0;
            for(size_t i=0;i<std::min(baked.size(),realtime.size());i+=4)
                if(std::abs(int(baked[i+2])-int(realtime[i+2]))>6)++changed;
            if(changed<100 || scene.bakedSceneStatus().getBool("loaded"))throw std::runtime_error("Scene bake toggle had no visible effect");
            scene.setBakedSceneEnabled(true);
            doc.path=(project/"other.json").string();draw();
            if(scene.bakedSceneStatus().getBool("loaded"))throw std::runtime_error("Scene leak across maps");
            std::cout<<"Baked terrain draws verified, blue channel changed at "<<changed<<" pixels\n";
            doc.data.extra.set("lights",originalLights);
            scene.setBakedSceneEnabled(false);doc.path.clear();doc.data.entities.clear();++doc.revision;
        }

        {
            // A second Scene instance owns the Lighting panel drawable and camera.
            // The document is shared read-only, as it is in the Studio host tick.
            NSWindow* panel=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,0,320,180)
                styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
            CAMetalLayer* target=[CAMetalLayer layer];target.framebufferOnly=NO;
            panel.contentView.wantsLayer=YES;panel.contentView.layer=target;[panel orderFront:nil];
            studio::StudioScene preview(gfx::createMetalDevice((__bridge void*)target));
            const auto original=doc.toJson();
            const auto mainFrames=scene.framesDrawn();
            double means[2]{};
            for(int night=0;night<2;++night){
                render::Lighting env;
                if(night){env.sunIntensity=.02f;env.skyColor={.02f,.02f,.04f};env.groundColor={.01f,.01f,.02f};}
                studio::PreviewDayLighting day;day.night=float(night);
                preview.setWeatherPreview(env,day);
                auto draw=[&]{preview.frame(320,180,doc,false,"","",empty,0);};
                draw();
                auto sample=std::make_shared<std::promise<std::vector<uint8_t>>>();
                auto result=sample->get_future();
                preview.captureNextFrame([sample](int,int,std::vector<uint8_t> rgba){sample->set_value(std::move(rgba));});
                for(int i=0;i<50&&result.wait_for(std::chrono::milliseconds(4))!=std::future_status::ready;++i)draw();
                if(result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)throw std::runtime_error("Weather framebuffer timeout");
                const auto pixels=result.get();
                if(pixels.size()!=320*180*4)throw std::runtime_error("Weather framebuffer size");
                std::ofstream file(std::filesystem::path(argv[1])/(night?"weather-night.ppm":"weather-day.ppm"),std::ios::binary);
                file<<"P6\n320 180\n255\n";
                for(size_t i=0;i<pixels.size();i+=4){
                    file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                    means[night]+=pixels[i]+pixels[i+1]+pixels[i+2];
                }
                means[night]/=320*180*3;
            }
            std::cout<<"Independent weather framebuffer means "<<means[0]<<" "<<means[1]<<"\n";
            if(means[0]-means[1]<2 || !(doc.toJson()==original) || scene.framesDrawn()!=mainFrames)
                throw std::runtime_error("Weather preview did not render independently");
        }
        bool fps=false;
        auto frame=[&](){scene.frame(160,160,doc,fps,"","",empty,0);};
        std::vector<uint8_t> litPixels,unlitPixels;
        for(const char* mode:{"cpu","gpu","off","unlit"}) {
            const std::string indirect=std::string(mode)=="unlit"?"off":mode;
            if(std::string(mode)=="unlit")doc.data.extra.set("lights",Json::array());
            doc.data.extra.set("renderLighting",Json::parse(std::string("{\"indirect\":\"")+indirect+"\",\"direct\":\"luma\",\"probes\":4,\"layers\":2,\"spacing\":2,\"samples\":8}"));doc.touched();
            for(int i=0;i<150;++i) {
                frame();
                if(scene.lightingStatus().find("converging")==std::string::npos)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const auto status=scene.lightingStatus();
            if(status.find("Direct: Luma")==std::string::npos || status.find("converging")!=std::string::npos)
                throw std::runtime_error(status);
            if(indirect!="off" && status.find(std::string("GI: ")+(std::string(mode)=="cpu"?"CPU":"GPU"))==std::string::npos)
                throw std::runtime_error(status);
            bool captured=false,valid=false;
            scene.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
                captured=true;valid=w==160&&h==160&&pixels.size()==size_t(w*h*4);
                if(!valid)return;
                uint64_t sum=0;std::ofstream file(std::filesystem::path(argv[1])/(std::string(mode)+".ppm"),std::ios::binary);
                file<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4){file.write(reinterpret_cast<const char*>(pixels.data()+i),3);sum+=pixels[i]+pixels[i+1]+pixels[i+2];}
                valid=sum>uint64_t(w*h*10);
                if(std::string(mode)=="off")litPixels=pixels;
                if(std::string(mode)=="unlit")unlitPixels=pixels;
            });
            for(int i=0;i<10&&!captured;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            if(!captured||!valid)throw std::runtime_error("native viewport capture invalid");
            std::cout<<status<<" PASS\n";
        }
        if(litPixels.size()!=unlitPixels.size() || litPixels.empty())throw std::runtime_error("missing on/off captures");
        int64_t energy=0;size_t changed=0;
        for(size_t i=0;i<litPixels.size();i+=4) {
            int delta=0;for(int c=0;c<3;++c)delta+=int(litPixels[i+c])-int(unlitPixels[i+c]);
            energy+=delta;if(delta>3)++changed;
        }
        std::cout<<"Luma light removal delta "<<energy<<" in "<<changed<<" pixels\n";
        if(energy<100 || changed<30)throw std::runtime_error("authored Luma source did not light preview pixels");
        // Turn off and restore to exercise resource retirement and view invalidation.
        doc.data.extra.set("renderLighting",Json::object());doc.touched();frame();
        if(scene.lightingStatus().find("GI: off · Direct: standard")==std::string::npos)throw std::runtime_error("disable failed");
        // FPS has no RTS size and may live entirely in negative coordinates.
        std::string error;
        if(!doc.loadJson(Json::parse(R"({"name":"Negative FPS",
          "colliders":[{"min":[-24,-0.2,-16],"max":[-16,0,-8]}],
          "spawns":[[-20,1,-12]],"players":[{"name":"P1","team":1}],
          "lights":[{"x":-20,"z":-12,"height":2,"radius":12,"intensity":6,"color":[1,0.3,0.1]}],
          "renderLighting":{"indirect":"gpu","direct":"luma","probes":4,"layers":2,"spacing":3,"samples":8}
        })"),&error))throw std::runtime_error(error);
        const auto preview=studio::studioLightingPreview(doc,true,1.5f);
        if(!preview || preview->boundsMax.x>=0 || preview->boundsMax.z>=0 ||
           preview->boundsMin.x>-24 || preview->boundsMin.z>-16)
            throw std::runtime_error("FPS preview includes unrelated RTS terrain");
        fps=true;scene.resetCamera();
        for(int i=0;i<150;++i) {
            frame();
            if(scene.lightingStatus().find("converging")==std::string::npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(scene.lightingStatus().find("GI: GPU")==std::string::npos ||
           scene.lightingStatus().find("Direct: Luma")==std::string::npos ||
           scene.lightingStatus().find("converging")!=std::string::npos)
            throw std::runtime_error("FPS GPU convergence: "+scene.lightingStatus());
        // A center-screen hit proves that the camera follows the negative scene.
        if(scene.pick(80,80,160,160,doc,true).empty())
            throw std::runtime_error("FPS camera misses authored scene");
        for(const std::string domain:{"particle","material","postfx"})for(int channel=0;channel<2;++channel){
            Json graph=Json::parse(R"({"version":1,"domain":"material","nodes":[{"op":"constant","id":"swatch","value":[1,0,0,1]}],"outputs":{"color":"swatch","emission":"swatch"}})");
            graph.set("domain",Json::string(domain));
            if(domain!="material")graph.find("outputs")->erase("emission");
            if(channel==1)graph.find("nodes")->at(0).set("value",Json::parse("[0,1,0,1]"));
            Json factors=Json::object();factors.set("studioGraphDraft",graph);
            auto draw=[&]{scene.frame(160,160,doc,true,"","Shader Graph / test",factors,(domain=="material"?10:domain=="postfx"?20:30)+channel);};
            for(int i=0;i<15;++i)draw();
            auto sample=std::make_shared<std::promise<std::vector<uint8_t>>>();
            auto result=sample->get_future();
            scene.captureNextFrame([sample](int,int,std::vector<uint8_t> rgba){sample->set_value(std::move(rgba));});
            for(int i=0;i<50&&result.wait_for(std::chrono::milliseconds(4))!=std::future_status::ready;++i)draw();
            if(result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)throw std::runtime_error("graph swatch capture timeout");
            const auto pixels=result.get();
            if(pixels.size()!=160*160*4)throw std::runtime_error("graph swatch capture size");
            int red=0,green=0;
            for(int y=72;y<88;++y)for(int x=72;x<88;++x){
                const auto i=(y*160+x)*4;red+=pixels[i];green+=pixels[i+1];
            }
            if(domain=="particle"){
                int matching=0;
                for(size_t i=0;i<pixels.size();i+=4)
                    if(int(pixels[i+channel])-pixels[i+1-channel]>32)++matching;
                if(matching<12)throw std::runtime_error("Missing particle GPU swatch: "+scene.lightingStatus());
            } else if((channel==0?red-green:green-red)<256*8)
                throw std::runtime_error("material graph swatch did not change on GPU: "+scene.lightingStatus()+" red="+std::to_string(red)+" green="+std::to_string(green));
        }
        scene.frame(160,160,doc,true,"","Material",Json::object(),1);
        if(scene.lightingStatus().find("Material preview · GI: off")==std::string::npos)
            throw std::runtime_error("material preview reports stale GI");
        frame();
        if(scene.lightingStatus().find("Material preview")!=std::string::npos)
            throw std::runtime_error("scene status not restored");
        std::cout<<"Negative-coordinate FPS and material transition PASS\n";
        // Interactive viewport: an unchanged scene is not redrawn; a camera or
        // document change is drawn on the next frame.
        scene.setContinuousRedraw(false);
        for(int i=0;i<400 && scene.framesSkipped()==0;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        if(scene.framesSkipped()==0)throw std::runtime_error("idle viewport never skipped a frame: "+scene.lightingStatus());
        {
            const auto drawn=scene.framesDrawn(),skipped=scene.framesSkipped();
            frame();frame();
            if(scene.framesDrawn()!=drawn || scene.framesSkipped()!=skipped+2)throw std::runtime_error("idle frames redrawn");
            scene.orbit(.05f,0);frame();
            if(scene.framesDrawn()!=drawn+1)throw std::runtime_error("camera change not redrawn");
            doc.touched();frame();
            if(scene.framesDrawn()!=drawn+2)throw std::runtime_error("document change not redrawn");
        }
        std::cout<<"Idle frame skipping PASS\n";
        {
            const studio::StudioSceneDocument requested{"demo","field.json","history",2,doc.revision};
            auto stale=requested;--stale.epoch;
            auto received=std::make_shared<std::promise<bool>>();
            auto ready=received->get_future();
            editor::SceneScreenshotOptions options;options.clean=true;options.width=357;options.height=259;
            scene.requestMapCapture(requested,options,[received](int w,int h,std::vector<uint8_t> pixels){
                received->set_value(w==357 && h==259 && pixels.size()==357*259*4);
            });
            auto draw=[&](int w,int h){scene.frame(w,h,doc,fps,"","",empty,0);};
            scene.renderMapOutput(stale,160,160,[&](int w,int h){
                if(w!=160 || h!=160)throw std::runtime_error("Stale map consumed export dimensions");
                draw(w,h);
            });
            scene.renderMapOutput(requested,160,160,draw);
            for(int i=0;i<80 && ready.wait_for(std::chrono::milliseconds(4))!=std::future_status::ready;++i)frame();
            if(ready.wait_for(std::chrono::seconds(1))!=std::future_status::ready || !ready.get())
                throw std::runtime_error("Matching map failed to consume the queued Scene export");
        }
        std::cout<<"Scene export waits for matching document lifetime PASS\n";
        std::cout<<"Studio Metal lighting PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Studio Metal lighting FAIL: "<<e.what()<<"\n";return 1;}
} }
