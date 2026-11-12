#include "engine/render/Geometry.h"
#include "engine/render/GeometryScene.h"

namespace geometrytest {
render::Model grid(int n=32) {
    using namespace render;Model m;m.nodes.emplace_back();m.nodes[0].mesh=0;m.meshes.emplace_back();
    for(int y=0;y<=n;++y)for(int x=0;x<=n;++x){ModelVertex v;v.pos={float(x)/n*10-5,float(y)/n*10-5,0};v.nrm={0,0,1};m.vertices.push_back(v);}
    for(int y=0;y<n;++y)for(int x=0;x<n;++x){uint32_t a=uint32_t(y*(n+1)+x),b=a+1,c=a+uint32_t(n+1),d=c+1;m.indices.insert(m.indices.end(),{a,b,c,b,d,c});}
    Primitive p;p.indexCount=uint32_t(m.indices.size());p.vertexCount=uint32_t(m.vertices.size());m.meshes[0].primitives.push_back(p);m.finalize();return m;
}
std::string boxObj(){return "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 2 3 7 6\nf 1 5 8 4\n";}
}
TEST(geometry_cluster_coverage_lod_boundaries) {
    using namespace render;auto m=geometrytest::grid();auto original=m.indices;prepareGeometry(m,64);
    CHECK(m.geometry && !m.geometry->primitives[0].empty());
    std::vector<std::array<uint32_t,3>> source,fine;
    for(size_t i=0;i<original.size();i+=3)source.push_back({original[i],original[i+1],original[i+2]});
    size_t coarse=0;
    for(auto& c:m.geometry->primitives[0]){
        CHECK(c.indexCount<=64*3 && c.coarseCount<=c.indexCount && c.error>=0);
        std::map<std::pair<uint32_t,uint32_t>,int> edges;
        for(uint32_t k=0;k<c.indexCount;k+=3){std::array<uint32_t,3> t;for(int j=0;j<3;++j)t[j]=m.indices[c.firstIndex+k+uint32_t(j)];fine.push_back(t);for(int j=0;j<3;++j)++edges[std::minmax(t[j],t[(j+1)%3])];}
        std::set<uint32_t> reduced;for(uint32_t k=0;k<c.coarseCount;++k)reduced.insert(m.indices[c.coarseFirst+k]);
        for(auto [e,count]:edges)if(count!=2){CHECK(reduced.count(e.first));CHECK(reduced.count(e.second));}
        coarse+=c.coarseCount;
    }
    std::sort(source.begin(),source.end());std::sort(fine.begin(),fine.end());CHECK(source==fine);CHECK(coarse<original.size());
    const auto n=m.indices.size();prepareGeometry(m);CHECK(m.indices.size()==n);
    auto animated=geometrytest::grid();animated.clips.emplace_back();prepareGeometry(animated);CHECK(animated.geometry->primitives[0].empty());CHECK(animated.geometry->occluders[0].empty());
    auto morph=geometrytest::grid();morph.meshes[0].primitives[0].morphTargets=1;prepareGeometry(morph);CHECK(morph.geometry->primitives[0].empty());
    auto clip=mat4::ortho(-10,10,-10,10,.1f,100,false)*mat4::translate({0,0,-20});
    CHECK(geometryErrorPixels(clip,m.boundsMin,m.boundsMax,1,1000,1000)>=50);
    CHECK(geometryErrorPixels(clip,m.boundsMin,m.boundsMax,1,2000,2000)>=100);
}
TEST(geometry_occlusion_holes_near_plane_backfaces_reset) {
    using namespace render;GeometryOcclusion occ;
    for(bool zero:{false,true}){
        occ.clear(zero);const mat4 clip;
        CHECK(!occ.hidden(clip,{-.1f,-.1f,.7f},{.1f,.1f,.8f}));
        // One large real triangle covers the complete target, not only its centre sample.
        const std::array<float3,3> tri={float3{-1,-1,.2f},float3{1,-1,.2f},float3{0,1,.2f}};
        occ.triangle(clip,tri,false);CHECK(occ.hidden(clip,{-.1f,-.1f,.7f},{.1f,.1f,.8f}));
        CHECK(!occ.hidden(clip,{-.1f,-.1f,.1f},{.1f,.1f,.3f}));
        CHECK(!occ.hidden(clip,{-.8f,.7f,.7f},{-.6f,.8f,.8f}));
        CHECK(!occ.hidden(clip,{-2,-.1f,.7f},{.1f,.1f,.8f}));
        CHECK(!occ.hidden(clip,{-.1f,-.1f,-2},{.1f,.1f,.8f}));
        occ.clear(zero);occ.triangle(clip,{tri[2],tri[1],tri[0]},false);CHECK(!occ.hidden(clip,{-.1f,-.1f,.7f},{.1f,.1f,.8f}));
        occ.triangle(clip,{float3{-1,-1,-2},tri[1],tri[2]});CHECK(!occ.hidden(clip,{-.1f,-.1f,.7f},{.1f,.1f,.8f}));
    }
}
TEST(geometry_renderer_clusters_and_occlusion) {
    using namespace render;entityfxtest::RecordingDevice dev;Renderer3D r;CHECK(r.init(dev));r.setShadowQuality(ShadowQuality::Off);
    auto wall=geometrytest::grid();prepareGeometry(wall);CHECK(uploadModel(dev,wall));
    auto target=geometrytest::grid(2);CHECK(uploadModel(dev,target));
    RTSCamera cam;cam.target={0,0,0};cam.pitch=0;cam.distance=20;
    auto frame=[&](bool occ,bool occluder,float dissolve){r.beginScene(cam,800,600);ModelDraw a;a.model=&wall;a.geometry.clusters=true;a.geometry.occluder=occluder;a.dissolve=dissolve;r.drawModel(a);
        ModelDraw b;b.model=&target;b.world=mat4::translate({-2,0,-3})*mat4::scale({.02f,.02f,.02f});b.geometry.occlusion=occ;r.drawModel(b);r.render(gfx::PassDesc{});return r.geometryStats();};
    const auto clustered=frame(false,true,0);CHECK(clustered.occluded==0);CHECK(clustered.clustersTested>0);
    // Dense small source triangles intentionally provide little full-cell coverage. A low-poly wall works better.
    auto simple=geometrytest::grid(1);prepareGeometry(simple);releaseModel(dev,wall);wall=std::move(simple);uploadModel(dev,wall);
    CHECK(frame(true,true,0).occluded>0);
    CHECK(frame(true,false,0).occluded==0);
    CHECK(frame(true,true,.5f).occluded==0);
    wall.materials[0].alpha=AlphaMode::Mask;CHECK(frame(true,true,0).occluded==0);
    releaseModel(dev,wall);releaseModel(dev,target);r.shutdown();
}
TEST(geometry_hlod_scene_switch_and_validation) {
    using namespace render;entityfxtest::RecordingDevice dev;Renderer3D r;CHECK(r.init(dev));r.setShadowQuality(ShadowQuality::Off);
    auto json=Json::parse(R"({"version":1,"hlod":{"enabled":true,"maxPixels":256,"errorPixels":2},"instances":[{"id":"a","model":"box.obj","position":[-3,0,0]},{"id":"b","model":"box2.obj","position":[3,0,0]}]})");
    GeometryScene scene;scene.load(dev,json,[](const std::string& f,std::string& s){if(f!="box.obj"&&f!="box2.obj")return false;s=geometrytest::boxObj();return true;});CHECK(scene.stats().proxies==1);
    RTSCamera cam;cam.pitch=0;cam.distance=10;r.beginScene(cam,800,600);scene.draw(r);r.render(gfx::PassDesc{});CHECK(scene.stats().submitted==2);
    cam.distance=200;r.beginScene(cam,800,600);scene.draw(r);r.render(gfx::PassDesc{});CHECK(scene.stats().submitted==1);
    cam.distance=10;r.beginScene(cam,800,600);scene.draw(r);r.render(gfx::PassDesc{});CHECK(scene.stats().submitted==2);
    for(auto bad:{R"({"version":1,"instances":[{"id":"x","model":"../secret.obj"}]})",R"({"version":1,"instances":[{"id":"x","model":"box.obj","scale":[-1,1,1]}]})",R"({"version":1,"instances":[],"hlod":{"enabled":"yes"}})"}){bool caught=false;try{GeometryScene::validate(Json::parse(bad));}catch(...){caught=true;}CHECK(caught);}
    for(auto bad:{R"({"clusters":"yes"})",R"({"errorPixels":0})",R"({"typo":true})"}){bool caught=false;try{parseGeometryPolicy(Json::parse(bad));}catch(...){caught=true;}CHECK(caught);}
    json.member("instances").at(1).set("model",Json::string("box.obj"));
    scene.load(dev,json,[](const std::string&,std::string& out){out=geometrytest::boxObj();return true;});
    CHECK(scene.stats().proxies==0); // identical low-poly sources already use one instanced draw
    scene.shutdown(dev);r.shutdown();
}
#ifdef AIVERSE_TESTS_EDITOR_CORE
TEST(geometry_mcp_analyze_apply_scene_roundtrip) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"aiverse-geometry-mcp-test";
    fs::create_directories(root);
    fs::copy(kSkirmish,root/"game",fs::copy_options::recursive|fs::copy_options::overwrite_existing);
    {std::ofstream f(root/"game"/"box.obj");f<<geometrytest::boxObj();}
    editor::Workspace ws;std::string err;CHECK(ws.openGame((root/"game").string(),&err));
    CHECK(ws.call("geometry_catalog",Json::object()).has("policies"));
    const auto a=ws.call("geometry_analyze",Json::parse(R"({"model":"box.obj","context":"interior","platform":"mobile"})"));
    CHECK(!a.has("error")&&a.getInt("triangles")==12&&a["suggestedPolicy"].getBool("occluder"));
    CHECK(ws.call("geometry_analyze",Json::parse(R"({"model":"../outside.obj"})")).has("error"));
    const auto applied=ws.call("geometry_policy_set",Json::parse(R"({"type":"peasant","policy":{"clusters":true,"occlusion":true}})"));
    CHECK(!applied.has("error"));CHECK(ws.project.rulesJson["types"]["peasant"]["view"]["geometry"].getBool("clusters"));
    CHECK(ws.call("geometry_policy_set",Json::parse(R"({"type":"peasant","policy":{"clusters":"bad"}})")).has("error"));
    const auto saved=ws.call("geometry_scene_set",Json::parse(R"({"scene":{"version":1,"hlod":{"enabled":true},"instances":[{"id":"a","model":"box.obj"}]}})"));
    CHECK(!saved.has("error"));CHECK(ws.project.manifest.getStr("geometryScene")=="geometry.json");
    const auto got=ws.call("geometry_scene_get",Json::object());CHECK(got["scene"]["instances"].size()==1);
    const auto invalid=ws.call("geometry_scene_set",Json::parse(R"({"scene":{"version":1,"instances":[{"id":"a","model":"missing.obj"}]}})"));
    CHECK(invalid.has("error"));CHECK(ws.call("geometry_scene_get",Json::object())["scene"]==got["scene"]);
    fs::remove_all(root);
}
#endif

TEST(geometry_surface_proxy_near_far_and_hlod) {
    using namespace render;entityfxtest::RecordingDevice dev;Renderer3D renderer;CHECK(renderer.init(dev));renderer.setShadowQuality(ShadowQuality::Off);
    auto data=Json::parse(R"({"version":1,"hlod":{"enabled":true},"instances":[{"id":"a","model":"source.obj","proxy":{"model":"proxy.obj","maxPixels":96}},{"id":"b","model":"source.obj","position":[4,0,0]}]})");
    GeometryScene scene;scene.load(dev,data,[](const std::string&,std::string& out){out=geometrytest::boxObj();return true;});
    CHECK(scene.stats().proxies==1); // parent HLOD must not bypass the explicit proxy threshold
    RTSCamera cam;cam.pitch=0;cam.distance=8;
    auto frame=[&](){renderer.beginScene(cam,800,600);scene.draw(renderer);renderer.render(gfx::PassDesc{});return scene.stats().surfaceProxiesSubmitted;};
    CHECK(frame()==0);cam.distance=200;CHECK(frame()==1);cam.distance=8;CHECK(frame()==0);
    scene.shutdown(dev);renderer.shutdown();
}
