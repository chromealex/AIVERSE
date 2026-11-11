#include "Workload.h"
#include "engine/cloth/ClothRender.h"
#include "engine/platform/Platform.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "engine/data/Json.h"
#include "imgui.h"
#include "engine/render/AnimationLod.h"
#include "engine/render/QualityGovernor.h"
#include <fstream>
#include <sstream>
using namespace aiverse;
namespace {
struct Demo {
    int count=256,cells=16,workers=8,renderCells=71,frames=0,warmup=120,tick=0;
    std::string report,language="ru";
    bool paused=false,sharing=true,distanceLod=true,autoLod=true;
    render::QualityGovernor governor;
    std::vector<cloth::LodHint> hints;
    int windGroups=17,gustUntil=-1;
    float originalWind=1;
    std::unique_ptr<clothdemo::Workload> workload;
    cloth::Renderer renderer;
    render::ImGuiLayer gui;
    render::RTSCamera camera;
    i18n::Localization loc;
    std::vector<double> simTimes,uploadTimes,callbackTimes,intervalTimes;
    clothdemo::Clock::time_point previous;
    gfx::Device* device=nullptr;
    void init(gfx::Device& d){
        for(auto* samples:{&simTimes,&uploadTimes,&callbackTimes,&intervalTimes})samples->reserve(size_t(frames));
        device=&d;workload=std::make_unique<clothdemo::Workload>(count,cells,workers,4,sharing,windGroups);
        if(!renderer.init(d,count,cells,renderCells))throw std::runtime_error("cloth renderer: "+d.lastError());
        for(auto lang:{"ru","en"}){std::string text;if(platform::readContentFile(AIVERSE_SOURCE_DIR "/engine/assets/lang/"+std::string(lang)+".json",text))loc.addTable(lang,Json::parse(text));}
        loc.setLanguage(language);gui.init(d,AIVERSE_SOURCE_DIR "/engine/assets/fonts/ui.ttf",16);
        float extent=float(workload->columns)*4;
        camera.target={(extent-1)/2,1.5f,(extent-4)/2};camera.pitch=.9f;camera.yaw=.12f;camera.distance=extent*1.85f;camera.zFar=1000;camera.zNear=.1f;
        governor.reset(3,3);hints.resize(size_t(count));
        previous=clothdemo::Clock::now();
    }
    void frame(gfx::Device& d,double dt){
        auto begin=clothdemo::Clock::now();double interval=clothdemo::ms(previous,begin);previous=begin;
        if(gustUntil>=0&&tick>=gustUntil){workload->cloths[0].windScale=originalWind;gustUntil=-1;}
        governor.update(dt);
        const auto viewProj=camera.proj(float(platform::framebufferWidth())/platform::framebufferHeight(),d.info().clipDepthZeroToOne)*camera.view();
        workload->lod.setMinimumLevel(autoLod?unsigned(governor.stepsDown()/2):0);
        if(autoLod)for(size_t i=0;i<hints.size();++i){auto& c=workload->cloths[i];auto& s=c.grid().settings;
            auto center=c.origin()+cloth::Vec{s.width/2,-s.height/2,0};
            auto resolved=render::projectedAnimationLod(viewProj,center,center+cloth::Vec{0,s.height/2,0});
            hints[i]={resolved.interval==0?0:resolved.interval<=1.f/30?1:3,resolved.visible};
        }
        if(!paused)workload->step(tick,camera.eye(),distanceLod,autoLod?std::span<const cloth::LodHint>(hints):std::span<const cloth::LodHint>{});auto simEnd=clothdemo::Clock::now();
        d.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());auto uploadBegin=clothdemo::Clock::now();renderer.upload(workload->cloths,workload->batch.representatives());auto uploadEnd=clothdemo::Clock::now();
        gfx::PassDesc pass;pass.clearColor[0]=.035f;pass.clearColor[1]=.05f;pass.clearColor[2]=.08f;d.beginPass(pass);
        renderer.draw(camera.proj(float(d.framebufferWidth())/d.framebufferHeight(),d.info().clipDepthZeroToOne)*camera.view());
        gui.newFrame(dt);ImGui::SetNextWindowPos({16,16},ImGuiCond_Once);
        ImGui::Begin(loc.tr("clothDemo.title").c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(loc.tr("clothDemo.count",i18n::Args().num("count",count).num("triangles",2*renderCells*renderCells)).c_str());
        ImGui::TextUnformatted(loc.tr("clothDemo.grid",i18n::Args().num("particles",(cells+1)*(cells+1)).num("workers",workload->batch.workers())).c_str());
        ImGui::TextUnformatted(loc.tr("clothDemo.groups",i18n::Args().num("solves",workload->batch.stats().simulatedObjects).num("copies",workload->batch.stats().copiedObjects)).c_str());
        auto levels=workload->lod.counts();
        ImGui::TextUnformatted(loc.tr("clothDemo.lodCounts",i18n::Args().num("a",levels[0]).num("b",levels[1]).num("c",levels[2]).num("d",levels[3])).c_str());
        ImGui::Text("XPBD: %.2f ms | %s: %.2f ms",clothdemo::ms(begin,simEnd),loc.tr("clothDemo.frame").c_str(),interval);
        if(!frames){
            ImGui::Checkbox(loc.tr("clothDemo.autoLod").c_str(),&autoLod);
            ImGui::Checkbox(loc.tr("clothDemo.lod").c_str(),&distanceLod);
            if(ImGui::Checkbox(loc.tr("clothDemo.sharing").c_str(),&sharing))workload->batch.setSharing(sharing);
            if(ImGui::Button(loc.tr(paused?"clothDemo.resume":"clothDemo.pause").c_str()))paused=!paused;
            ImGui::SameLine();if(ImGui::Button(loc.tr("clothDemo.reset").c_str())){for(auto& c:workload->cloths)c.reset();if(gustUntil>=0)workload->cloths[0].windScale=originalWind;gustUntil=-1;tick=0;workload->lod.reset();}
            if(ImGui::Button(loc.tr("clothDemo.gust").c_str())){if(gustUntil<0)originalWind=workload->cloths[0].windScale;workload->cloths[0].windScale=originalWind+3;gustUntil=tick+60;workload->lod.requestUpdate(0);}
            ImGui::SliderFloat(loc.tr("clothDemo.orbit").c_str(),&camera.yaw,-3.14f,3.14f);
            ImGui::SliderFloat(loc.tr("clothDemo.distance").c_str(),&camera.distance,5,300);
        }
        ImGui::End();gui.render();d.endPass();d.endFrame();auto end=clothdemo::Clock::now();
        if(frames&&tick>=warmup){simTimes.push_back(clothdemo::ms(begin,simEnd));uploadTimes.push_back(clothdemo::ms(uploadBegin,uploadEnd));callbackTimes.push_back(clothdemo::ms(begin,end));intervalTimes.push_back(interval);}
        ++tick;if(frames&&tick>=warmup+frames)platform::requestQuit();
    }
    void shutdown(){
        if(frames){std::ostringstream out;out<<std::setprecision(6)<<"{\"objects\":"<<count<<",\"physical_cells\":"<<cells<<",\"render_triangles_per_object\":"<<2*renderCells*renderCells<<",\"triangles_total\":"<<renderer.triangles()<<",\"workers\":"<<workload->batch.workers()<<",\"frames\":"<<simTimes.size()<<",\"warmup\":"<<warmup<<",\"framebuffer\":["<<device->framebufferWidth()<<','<<device->framebufferHeight()<<"],\"upload_bytes\":"<<renderer.uploadBytes()<<",";
            out<<"\"auto_lod\":"<<(autoLod?"true":"false")<<",";
            out<<"\"distance_lod\":"<<(distanceLod?"true":"false")<<",";
            out<<"\"sharing\":"<<(sharing?"true":"false")<<",\"wind_groups\":"<<windGroups<<",\"simulated_objects\":"<<workload->batch.stats().simulatedObjects<<",\"copied_objects\":"<<workload->batch.stats().copiedObjects<<",";
            clothdemo::metric(out,"simulation_ms",simTimes);out<<',';clothdemo::metric(out,"upload_cpu_ms",uploadTimes);out<<',';clothdemo::metric(out,"callback_cpu_ms",callbackTimes);out<<',';clothdemo::metric(out,"frame_interval_ms",intervalTimes);
            out<<",\"frame_samples_ms\":[";for(size_t i=0;i<intervalTimes.size();++i){if(i)out<<',';out<<intervalTimes[i];}out<<"],\"max_stretch_ratio\":"<<workload->stretch()<<",\"healthy\":"<<(workload->healthy()?"true":"false")<<"}\n";
            std::cout<<out.str();if(!report.empty()){std::ofstream f(report);f<<out.str();if(!f)std::cerr<<"Failed to write benchmark report\n";}
        }
        renderer.shutdown();gui.shutdown();workload.reset();
    }
} demo;
}
int aiverseMain(int argc,char** argv){try{
    for(int i=1;i<argc;++i){std::string key=argv[i];if(i+1>=argc)throw std::invalid_argument("missing option value");std::string value=argv[++i];
        if(key=="--objects")demo.count=std::stoi(value);else if(key=="--cells")demo.cells=std::stoi(value);else if(key=="--workers")demo.workers=std::stoi(value);else if(key=="--render-cells")demo.renderCells=std::stoi(value);
        else if(key=="--frames")demo.frames=std::stoi(value);else if(key=="--warmup")demo.warmup=std::stoi(value);else if(key=="--auto-lod")demo.autoLod=std::stoi(value)!=0;else if(key=="--lod")demo.distanceLod=std::stoi(value)!=0;else if(key=="--share")demo.sharing=std::stoi(value)!=0;else if(key=="--wind-groups")demo.windGroups=std::stoi(value);else if(key=="--report")demo.report=value;else if(key=="--lang")demo.language=value;else throw std::invalid_argument("unknown option "+key);
    }
    if(demo.frames<0||demo.frames>100000||demo.warmup<0||demo.warmup>100000)throw std::invalid_argument("benchmark frame count");
    platform::AppConfig config;config.title="AIVERSE XPBD Cloth";config.width=1280;config.height=800;
    platform::AppCallbacks callbacks;callbacks.init=[](gfx::Device& d){demo.init(d);};callbacks.frame=[](gfx::Device& d,double dt){demo.frame(d,dt);};callbacks.event=[](const platform::Event& e){demo.gui.handleEvent(e);};callbacks.shutdown=[](){demo.shutdown();};
    return platform::run(config,callbacks);
}catch(const std::exception& e){std::cerr<<"cloth demo: "<<e.what()<<'\n';return 1;}}
