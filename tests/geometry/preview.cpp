// Real-backend visual regression scene. Run with AIVERSE_SCREENSHOT and AIVERSE_EXIT_AFTER_FRAMES.
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
int aiverseMain(int argc,char** argv){
    const bool enabled=argc<2||std::string(argv[1])!="off";
    const std::string mode=argc>2?argv[2]:"hlod";
    render::Model wall,target;
    auto makeGrid=[](int n,float extent){render::Model m;m.nodes.emplace_back();m.nodes[0].mesh=0;m.meshes.emplace_back();
        for(int y=0;y<=n;++y)for(int x=0;x<=n;++x){render::ModelVertex v;v.pos={float(x)/n*extent-extent*.5f,float(y)/n*extent-extent*.5f,0};v.nrm={0,0,1};m.vertices.push_back(v);}
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){uint32_t a=uint32_t(y*(n+1)+x),b=a+1,c=a+uint32_t(n+1),d=c+1;m.indices.insert(m.indices.end(),{a,b,c,b,d,c});}
        render::Primitive p;p.indexCount=uint32_t(m.indices.size());p.vertexCount=uint32_t(m.vertices.size());m.meshes[0].primitives.push_back(p);m.finalize();return m;};
    render::Renderer3D renderer;render::GeometryScene scene;gfx::Device* device=nullptr;
    platform::AppConfig config;config.title="AIVERSE Geometry Verification";config.width=960;config.height=640;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& dev){device=&dev;renderer.init(dev);renderer.setShadowQuality(render::ShadowQuality::Medium);
        if(mode!="hlod") {wall=makeGrid((mode=="clusters"||mode=="lod")?64:1,mode=="clusters"?60.f:10.f);target=makeGrid(1,1);
            wall.materials[0].baseColor={.2f,.6f,.8f,1};target.materials[0].baseColor={1,.1f,.1f,1};
            render::prepareGeometry(wall);
            if(mode=="lod"){uint32_t fine=0,coarse=0;float error=0;for(auto& c:wall.geometry->primitives[0]){fine+=c.indexCount;coarse+=c.coarseCount;error=std::max(error,c.error);}std::printf("cluster asset fine=%u coarse=%u error=%f\n",fine/3,coarse/3,error);}
            render::uploadModel(dev,wall);render::uploadModel(dev,target);return;}
        Json j=Json::parse(R"({"version":1,"hlod":{"enabled":false,"maxPixels":512},"instances":[]})");j.member("hlod").set("enabled",Json::fromBool(enabled));
        for(int z=0;z<6;++z)for(int x=0;x<8;++x){Json i=Json::object();i.set("id",Json::string(std::to_string(z*8+x)));i.set("model",Json::string("box"+std::to_string(z*8+x)+".obj"));
            Json pos=Json::array();pos.push(Json::number(int64_t(x*4-14)));pos.push(Json::number(int64_t(1)));pos.push(Json::number(int64_t(z*4-10)));i.set("position",pos);
            if(enabled)i.set("geometry",Json::parse(R"({"clusters":true,"occlusion":true,"occluder":true})"));j.member("instances").push(i);}
        const std::string obj="v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 2 3 7 6\nf 1 5 8 4\n";
        scene.load(dev,j,[obj](const std::string&,std::string& out){out=obj;return true;});
    };
    int frame=0;
    cb.frame=[&](gfx::Device& dev,double){render::RTSCamera cam;cam.target={0,0,0};cam.distance=80;cam.pitch=.6f;cam.yaw=.4f;if(mode!="hlod"){cam.distance=mode=="lod"?800:20;cam.pitch=0;cam.yaw=0;}
        renderer.beginScene(cam,dev.framebufferWidth(),dev.framebufferHeight());
        if(mode=="hlod")scene.draw(renderer);
        else {render::ModelDraw d;d.model=&wall;d.geometry.clusters=enabled&&(mode=="clusters"||mode=="lod");d.geometry.errorPixels=2;d.geometry.occluder=enabled&&mode=="occlusion";renderer.drawModel(d);
            d.model=&target;d.geometry={};d.geometry.occlusion=enabled;d.world=render::mat4::translate({-2,0,-3})*render::mat4::scale({.2f,.2f,.2f});renderer.drawModel(d);}
        gfx::PassDesc p;p.clearColor[0]=.08f;p.clearColor[1]=.1f;p.clearColor[2]=.14f;renderer.render(p);
        if(++frame==3){const auto& s=renderer.geometryStats();std::printf("geometry enabled=%d instances=%u proxies=%u submitted=%u draws=%u occluded=%u clusters=%u coarse=%u\n",enabled,scene.stats().instances,scene.stats().proxies,scene.stats().submitted,renderer.drawCalls(),s.occluded,s.clustersTested,s.coarseClusters);}
    };
    cb.shutdown=[&]{scene.shutdown(*device);render::releaseModel(*device,wall);render::releaseModel(*device,target);renderer.shutdown();};return platform::run(config,cb);
}
