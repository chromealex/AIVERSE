// External asset benchmark. Same source decomposition/camera/materials for both modes.
#include "tools/modeling/SurfaceProxy.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GeometryScene.h"
#ifdef AIVERSE_BENCH_AUTOLOD
#include "AutoLod.h"
#endif
#include "engine/platform/Platform.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <set>
using namespace aiverse;
using namespace aiverse::render;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
static bool readBytes(const std::string& path,std::string& out){std::ifstream f(path,std::ios::binary);if(!f)return false;std::ostringstream s;s<<f.rdbuf();out=s.str();return true;}
static Json number(double v){return Json::number(v,6);}
static Json vector(float3 v){Json j=Json::array();for(float a:{v.x,v.y,v.z})j.push(number(a));return j;}
struct Part{std::string proxyPath;std::unique_ptr<Model> compact;Model view;std::unique_ptr<Model> proxy;float3 lo,hi;uint64_t triangles=0,proxyTriangles=0;};
static std::unique_ptr<Model> extract(const Model& source,int ni,bool pixels=true){
    auto m=std::make_unique<Model>();m->nodes.push_back(source.nodes[size_t(ni)]);m->nodes[0].parent=-1;m->nodes[0].children.clear();m->nodes[0].mesh=0;
    // Collapse ancestor transforms into this part's bind matrix after finalize.
    m->meshes.emplace_back();std::map<int,int> materials,images;std::map<uint32_t,uint32_t> vertices;
    for(auto p:source.meshes[size_t(source.nodes[size_t(ni)].mesh)].primitives){
        int mat=p.material;if(!materials.count(mat)){auto mt=source.materials[size_t(mat)];
            for(int* id:{&mt.baseTex,&mt.normalTex,&mt.mrTex,&mt.occlusionTex,&mt.emissiveTex,&mt.teamTex})if(*id>=0){int old=*id;if(!images.count(old)){images[old]=int(m->images.size());if(pixels)m->images.push_back(source.images[size_t(old)]);else{Image image;image.name=source.images[size_t(old)].name;image.borrowedTexture=true;m->images.push_back(std::move(image));}}*id=images[old];}
            materials[mat]=int(m->materials.size());m->materials.push_back(mt);}
        uint32_t first=uint32_t(m->indices.size());
        for(uint32_t k=0;k<p.indexCount;++k){auto old=source.indices[p.firstIndex+k];if(!vertices.count(old)){vertices[old]=uint32_t(m->vertices.size());m->vertices.push_back(source.vertices[old]);}m->indices.push_back(vertices[old]);}
        p.firstIndex=first;p.firstVertex=0;p.vertexCount=uint32_t(m->vertices.size());p.material=materials[mat];m->meshes[0].primitives.push_back(p);
    }
    m->finalize();m->bindWorld[0]=source.bindWorld[size_t(ni)];return m;
}
int aiverseMain(int argc,char** argv){try{
    if(argc<6){std::cerr<<"usage: aiverse_geometry_bench source.fbx cache-dir report.json off|on camera[0..2] [maxPixels=96]\n";return 2;}
    const std::string sourcePath=argv[1],output=argv[3];const fs::path cache=argv[2];const bool batch=std::string(argv[4])=="batch";bool enabled=std::string(argv[4])=="on";int camera=std::stoi(argv[5]);const float threshold=argc>6?std::stof(argv[6]):96;
    if(camera<0||camera>2||!std::isfinite(threshold)||threshold<1||threshold>512)throw std::runtime_error("invalid camera/threshold");
    const bool keepMasked=argc>7&&std::string(argv[7])=="keep-masked";
    const std::string profile=argc>8?argv[8]:"surface";
    const bool stack=profile!="surface";
    const bool useStatic=profile=="all"||profile=="static";
    const bool useLod=profile=="lod"||useStatic;
    const bool useHlod=profile=="hlod"||useStatic;
    const bool useClusters=profile=="clusters"||profile=="all";
    const bool useOcc=profile=="occlusion"||profile=="all";
    const bool useShadows=profile=="shadows"||profile=="all";
    const bool useSurfaces=profile=="surface"||useStatic;
    if(profile!="surface"&&profile!="cache"&&profile!="instances"&&profile!="bindings"&&!useLod&&!useHlod&&!useClusters&&!useOcc&&!useShadows)throw std::runtime_error("unknown profile");
#ifndef AIVERSE_BENCH_AUTOLOD
    if(useLod)throw std::runtime_error("configure AIVERSE_BUILD_AUTOLOD for LOD profile");
#endif
    fs::create_directories(cache);std::vector<std::string> warnings;
    auto start=Clock::now();auto source=loadModel(sourcePath,readBytes,&warnings);std::cerr<<"Loaded "<<source->indices.size()/3<<" triangles; "<<source->nodes.size()<<" nodes, "<<warnings.size()<<" warnings\n";
    // Bistro README defines Specular as ORM and Normal as DirectX. Adapt both workloads identically.
    std::map<std::string,int> loadedImages;size_t packedMaps=0,normalMaps=0;
    auto loadImage=[&](const fs::path& file){auto key=file.string();if(loadedImages.count(key))return loadedImages[key];std::string bytes;Image image;image.name=key;
        if(!readBytes(key,bytes)||!decodeImage(bytes,image))return -1;int id=int(source->images.size());source->images.push_back(std::move(image));loadedImages[key]=id;return id;};
    std::set<int> flipped;
    for(auto& mat:source->materials){
        if(mat.baseTex>=0){auto name=fs::path(source->images[size_t(mat.baseTex)].name).filename().string();auto at=name.find("_BaseColor");
            if(at!=std::string::npos){auto packed=name;packed.replace(at,10,"_Specular");int id=loadImage(fs::path(sourcePath).parent_path()/"Textures"/packed);
                if(id>=0){mat.mrTex=mat.occlusionTex=id;mat.metallic=mat.roughness=1;++packedMaps;}}
            if(name.find("Foliage_")==0)mat.alpha=AlphaMode::Mask;
        }
        if(mat.normalTex>=0&&flipped.insert(mat.normalTex).second){auto& im=source->images[size_t(mat.normalTex)];for(size_t i=1;i<im.rgba.size();i+=4)im.rgba[i]=255-im.rgba[i];im.compressed.reset();++normalMaps;}
    }
    Json report=Json::object(),skip=Json::array();report.set("warnings",Json::array());for(auto& w:warnings)report.member("warnings").push(Json::string(w));
    std::vector<Part> parts;uint64_t candidateTris=0,bakedTris=0;size_t candidates=0;
    for(int ni:source->order){const auto& node=source->nodes[size_t(ni)];if(node.mesh<0)continue;Part part;part.view.nodes.push_back(node);auto& n=part.view.nodes[0];n.parent=-1;n.children.clear();n.mesh=0;
        part.view.order={0};part.view.bindWorld={source->bindWorld[size_t(ni)]};part.view.meshes.push_back(source->meshes[size_t(node.mesh)]);part.view.materials=source->materials;
        part.lo={1e30f,1e30f,1e30f};part.hi={-1e30f,-1e30f,-1e30f};
        for(auto& p:part.view.meshes[0].primitives){part.triangles+=p.indexCount/3;for(int c=0;c<8;++c){auto a=p.boundsMin,b=p.boundsMax;auto v=source->bindWorld[size_t(ni)].transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});part.lo=min3(part.lo,v);part.hi=max3(part.hi,v);}}
        part.view.boundsMin=part.lo;part.view.boundsMax=part.hi;
        // Large monolithic scene shells and tiny meshes are deliberately left untouched.
        bool masked=false;for(const auto& p:part.view.meshes[0].primitives)masked|=source->materials[size_t(p.material)].alpha==AlphaMode::Mask;
        if(part.triangles>=512&&part.triangles<=100000&&!(keepMasked&&masked)){
            const auto file=cache/(std::to_string(ni)+".glb");
            try{if(!fs::exists(file)){auto local=extract(*source,ni);auto baked=modeling::bakeSurfaceProxy(*local,Json::parse(R"({"mode":"box","resolution":64})"));
                    std::ofstream out(file,std::ios::binary);out<<modeling::exportGlb(*baked.model);if(!out)throw std::runtime_error("cache write failed");}
                part.proxyPath=file.string();part.proxy=loadModel(file.string(),readBytes);part.proxyTriangles=part.proxy->indices.size()/3;++candidates;candidateTris+=part.triangles;bakedTris+=part.proxy->indices.size()/3;
            }catch(const std::exception& e){auto row=Json::object();row.set("node",Json::string(node.name));row.set("reason",Json::string(e.what()));skip.push(row);}
        }
        if(stack){part.compact=extract(*source,ni,false);
#ifdef AIVERSE_BENCH_AUTOLOD
            if(useLod)prepareBenchmarkLods(*part.compact);
#endif
            part.compact->bindWorld[0]=source->bindWorld[size_t(ni)];part.compact->boundsMin=part.lo;part.compact->boundsMax=part.hi;
        }
        parts.push_back(std::move(part));
    }
    report.set("bistroOrmMaterials",number(double(packedMaps)));report.set("directXNormalsConverted",number(double(normalMaps)));
    report.set("source",Json::string(sourcePath));report.set("sourceTriangles",number(double(source->indices.size()/3)));report.set("parts",number(double(parts.size())));report.set("candidateParts",number(double(candidates)));report.set("candidateTriangles",number(double(candidateTris)));report.set("bakedTriangles",number(double(bakedTris)));report.set("rejected",skip);report.set("loadBakeMs",number(ms(start,Clock::now())));
    report.set("proxyEnabled",Json::fromBool(enabled));report.set("camera",number(camera));report.set("maxPixels",number(threshold));report.set("keepMasked",Json::fromBool(keepMasked));
    const auto centre=(source->boundsMin+source->boundsMax)*.5f,extent=source->boundsMax-source->boundsMin;const float span=std::max(extent.x,extent.z);
    report.set("boundsMin",vector(source->boundsMin));report.set("boundsMax",vector(source->boundsMax));
    std::cerr<<"Prepared "<<parts.size()<<" parts, "<<candidates<<" proxies, bounds "<<vector(source->boundsMin).dump()<<" .. "<<vector(source->boundsMax).dump()<<"\n";
    GeometryScene scene;Renderer3D renderer;gfx::Device* device=nullptr;RTSCamera cam;cam.target=centre;cam.distance=span*(camera==2?1.8f:.85f);cam.yaw=camera==1?2.3f:.5f;cam.pitch=camera==2?.65f:.3f;cam.zFar=std::max(1000.f,span*5);cam.zNear=.1f;if(camera==1){cam.distance=span*1.3f;cam.pitch=.75f;}
    report.set("eye",vector(cam.eye()));report.set("target",vector(cam.target));report.set("fovY",number(cam.fovY));
    const int warmup=argc>9?std::stoi(argv[9]):60,samples=argc>10?std::stoi(argv[10]):120;
    if(warmup<30||warmup>10000||samples<120||samples>10000)throw std::runtime_error("invalid sampling budget");int frame=0,phase=0;std::string reportPath=output;Json rows=Json::array();std::map<uint64_t,double> gpu;auto previous=Clock::now();bool captured=false;
    platform::AppConfig cfg;cfg.title="AIVERSE Bistro geometry benchmark";cfg.width=1280;cfg.height=720;platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& dev){device=&dev;renderer.init(dev);renderer.setProfiling(true);renderer.setShadowQuality(ShadowQuality::Medium);renderer.lighting.fogDensity=0;uploadModel(dev,*source);
        for(auto& p:parts){p.view.vb=source->vb;p.view.ib=source->ib;for(auto& im:source->images){Image shared;shared.tex=im.tex;p.view.images.push_back(std::move(shared));}if(p.proxy)uploadModel(dev,*p.proxy);}
        if(stack){
            Json manifest=Json::parse(R"({"version":1,"hlod":{"enabled":true,"maxPixels":256,"errorPixels":1},"instances":[]})");
            manifest.member("hlod").set("enabled",Json::fromBool(useHlod));
            std::map<std::string,std::unique_ptr<Model>> prepared;
            size_t index=0;
            for(auto& p:parts){auto name="part"+std::to_string(index++)+".glb";auto row=Json::object();row.set("id",Json::string(name));row.set("model",Json::string(name));
                auto policy=Json::object();policy.set("clusters",Json::fromBool(useClusters));policy.set("shadowClusters",Json::fromBool(useShadows));policy.set("occlusion",Json::fromBool(useOcc));policy.set("occluder",Json::fromBool(useOcc&&p.triangles<=1024));row.set("geometry",policy);
                for(auto& image:p.compact->images)for(auto& original:source->images)if(image.name==original.name){image.tex=original.tex;break;}
                if(useSurfaces&&!p.proxyPath.empty()){auto proxyName="proxy"+name;auto proxy=Json::object();proxy.set("model",Json::string(proxyName));proxy.set("maxPixels",number(threshold));row.set("proxy",proxy);prepared[proxyName]=loadModel(p.proxyPath,readBytes);}
                manifest.member("instances").push(row);prepared[name]=std::move(p.compact);
            }
            scene.loadPrepared(dev,manifest,std::move(prepared),8000000);
            report.set("sourceVertexBudget",number(8000000));
            report.set("preparedHlodProxies",number(scene.stats().proxies));
        }
        report.set("profile",Json::string(profile));report.set("uncapped",Json::fromBool(std::getenv("AIVERSE_BENCH_UNCAPPED")&&std::string(std::getenv("AIVERSE_BENCH_UNCAPPED"))=="1"));
#if defined(__aarch64__) || defined(__arm64__)
        report.set("architecture",Json::string("arm64"));
#else
        report.set("architecture",Json::string("x86_64"));
#endif
        report.set("device",Json::string(dev.info().name));report.set("width",number(dev.framebufferWidth()));report.set("height",number(dev.framebufferHeight()));previous=Clock::now();};
    auto save=[&]{for(size_t i=0;i<rows.size();++i){auto& row=rows.at(i);auto f=uint64_t(row["frame"].toDouble());row.set("gpuMs",number(gpu.at(f)));}report.set("frames",rows);report.set("warmup",number(warmup));report.set("samples",number(samples));
        if(fs::exists(reportPath))throw std::runtime_error("refusing to overwrite report");std::ofstream f(reportPath);f<<report.dump(2)<<"\n";if(!f)throw std::runtime_error("report write failed");};
    cb.frame=[&](gfx::Device& dev,double){
        if(frame==0&&batch){camera=(phase/2)%3;enabled=((phase%2)^((phase/6)%2))!=0;cam.target=centre;cam.distance=span*(camera==2?1.8f:.85f);cam.yaw=camera==1?2.3f:.5f;cam.pitch=camera==2?.65f:.3f;if(camera==1){cam.distance=span*1.3f;cam.pitch=.75f;}
            reportPath=output+"-run"+std::to_string(phase/6+1)+"-cam"+std::to_string(camera)+(enabled?"-on.json":"-off.json");report.set("proxyEnabled",Json::fromBool(enabled));report.set("camera",number(camera));report.set("eye",vector(cam.eye()));report.set("target",vector(cam.target));}
        report.set("width",number(dev.framebufferWidth()));report.set("height",number(dev.framebufferHeight()));auto now=Clock::now();double wall=ms(previous,now);previous=now;for(auto t:dev.takeGpuTimings())if(t.valid)gpu[t.tag]=t.milliseconds;
        bool measured=frame>=warmup&&frame<warmup+samples;if(measured&&!dev.timeGpuFrame(uint64_t(frame)))throw std::runtime_error("GPU timing unavailable");
        dev.setBindingCacheEnabled(enabled&&(profile=="bindings"||useStatic));
        auto begin=Clock::now();renderer.beginScene(cam,dev.framebufferWidth(),dev.framebufferHeight());uint64_t triangles=0;size_t selected=0;
        if(stack){scene.setRuntimeOptions(enabled&&useHlod,enabled,enabled&&useSurfaces,enabled&&(profile=="cache"||useStatic));renderer.setModelLodEnabled(enabled&&useLod);renderer.setInstanceLimit(enabled&&(profile=="instances"||useStatic)?0:32);scene.draw(renderer);}
        for(auto& p:parts){bool use=enabled&&useSurfaces&&p.proxy&&geometryErrorPixels(renderer.viewProj(),p.lo,p.hi,length(p.hi-p.lo),renderer.sceneWidth(),renderer.sceneHeight(),renderer.clipDepthZeroToOne())<=threshold;
            ModelDraw d;d.model=use?p.proxy.get():&p.view;if(!stack)renderer.drawModel(d);
            if(geometryBoxVisible(renderer.viewProj(),p.lo,p.hi)){triangles+=use?p.proxyTriangles:p.triangles;if(use)++selected;}}
        gfx::PassDesc pass;pass.clearColor[0]=.1f;pass.clearColor[1]=.14f;pass.clearColor[2]=.2f;renderer.render(pass);double encode=ms(begin,Clock::now());
        if(measured){auto row=Json::object();row.set("frame",number(frame));row.set("cpuEncodeMs",number(encode));row.set("frameWallMs",number(wall));row.set("drawCalls",number(renderer.drawCalls()));
            const auto& prof=renderer.profile();row.set("submitMs",number(prof.submitMs));row.set("occlusionMs",number(prof.occlusionMs));row.set("cachedModels",number(prof.cachedModels));row.set("lodNodes",number(prof.lodNodes));row.set("sceneSubmitted",number(scene.stats().submitted));
            for(int phase=0;phase<5;++phase){const auto key=std::to_string(phase);row.set("selectMs"+key,number(prof.selectMs[phase]));row.set("sortMs"+key,number(prof.sortMs[phase]));row.set("drawMs"+key,number(prof.drawMs[phase]));row.set("draws"+key,number(prof.draws[phase]));row.set("triangles"+key,number(prof.triangles[phase]));}
            row.set("clustersTested",number(renderer.geometryStats().clustersTested));row.set("occluded",number(renderer.geometryStats().occluded));row.set("visibleProxyParts",number(double(selected)));row.set("visibleInputTriangles",number(stack?-1.0:double(triangles)));rows.push(row);}
        if(frame==warmup+samples+2)dev.captureNextFrame([&,capturePath=reportPath+".ppm"](int w,int h,std::vector<uint8_t> rgba){std::ofstream f(capturePath,std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";for(size_t i=0;i<rgba.size();i+=4)f.write(reinterpret_cast<const char*>(&rgba[i]),3);captured=bool(f);});
        if(++frame>warmup+samples+20){if(gpu.size()!=size_t(samples)||!captured)throw std::runtime_error("incomplete GPU samples/capture");save();std::cerr<<"Finished "<<reportPath<<"\n";
            if(batch&&++phase<18){frame=0;gpu.clear();rows=Json::array();captured=false;}else platform::requestQuit();}
    };
    cb.shutdown=[&]{
        scene.shutdown(*device);for(auto& p:parts)if(p.proxy)releaseModel(*device,*p.proxy);releaseModel(*device,*source);renderer.shutdown();};
    return platform::run(cfg,cb);
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
