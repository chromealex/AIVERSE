// Real-device CPU transport / atlas / model receiver acceptance.
#pragma once
namespace aiverse::test {
inline void drawDynamicGiScene(HdrScene& s,gfx::Device& d,int w,int h) {
    const int test=s.probeIndex;
    if(s.ownsFrame)d.beginFrame(w,h);
    const auto waitFrame=[&]{s.frame=0;if(s.ownsFrame)d.endFrame();};
    if(!s.dynamicGi.ready()) {
        GiMainCpuConfig cfg;
        cfg.origin={-2,-2,-2};cfg.nx=cfg.ny=cfg.nz=4;
        cfg.samples=64;cfg.traceDistance=8;
        cfg.cpuBudgetMs=100;cfg.maxProbesPerFrame=128;
        if(!s.dynamicGi.initialize(d,cfg)){s.failed=true;return;}
        GiChunkedGeometryBuilder builder;
        builder.replace(1,{{{-8,-8,2},{8,-8,2},{8,8,2},0},
                           {{-8,-8,2},{8,8,2},{-8,8,2},0}},{{1,1,1}});
        auto scene=std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
        // AIVERSE_COMPUTE_GI_FAR_WORLD: the GPU scene also holds a chunk far
        // beyond any GI ray, larger than the GPU triangle budget on its own.
        // Residency must leave it out, so GPU lighting still matches the CPU
        // reference (which never sees it) and no CPU fallback is needed.
        auto gpuScene=scene;
        const bool farWorld=s.computeGiVerify && std::getenv("AIVERSE_COMPUTE_GI_FAR_WORLD");
        if(farWorld) {
            std::vector<StaticShadowGeometry::Triangle> far;
            for(unsigned i=0;i<9000;++i) {
                const float x=5000.f+float(i%100),y=float(i/100);
                far.push_back({{x,y,2},{x+1,y,2},{x,y+1,2},0});
            }
            GiChunkedGeometryBuilder gpuBuilder;
            gpuBuilder.replace(1,{{{-8,-8,2},{8,-8,2},{8,8,2},0},
                                  {{-8,-8,2},{8,8,2},{-8,8,2},0}},{{1,1,1}});
            gpuBuilder.replace(2,std::move(far),{{1,1,1}});
            gpuScene=std::make_shared<GiChunkedGeometrySnapshot>(gpuBuilder.publish());
        }
        // AIVERSE_COMPUTE_GI_CASCADES=2|3: the GPU side runs nested cascades in
        // one atlas; the sampled point is inside cascade 0, so the pixel must
        // still match the single-lattice CPU reference.
        auto gpuCfg=cfg;
        if(const char* c=std::getenv("AIVERSE_COMPUTE_GI_CASCADES"))gpuCfg.cascades=std::atoi(c);
        if(s.computeGiVerify && (!s.computeGi.initialize(d,gpuCfg) ||
           !s.computeGi.publishGeometry(gpuScene,1))) {
            std::fprintf(stderr,"GI compute setup FAIL: %s / %s\n",d.lastError().c_str(),s.computeGi.diagnostic());
            s.failed=true;return;}
        if(farWorld) {
            const auto& r=s.computeGi.residency();
            std::printf("GI GPU residency: source %zu tris, resident %zu, excluded %zu\n",
                        r.sourceTriangles,r.relevantTriangles,r.excludedRemoteTriangles);
            if(r.sourceTriangles<=GiGpuSmallScene::kMaxTriangles || r.excludedRemoteTriangles!=9000 ||
               r.relevantTriangles!=2) {
                std::fprintf(stderr,"GI GPU residency FAIL\n");s.failed=true;return;
            }
        }
        if(!s.dynamicGi.publishGeometry(scene,1)) {
            s.failed=true;return;
        }
    }
    if(s.gridProbe!=test && (test==0 || test==4 || test==5)) {
        std::vector<GiMovingLight> sources;
        if(test!=4)sources.push_back({1,{0,0,0},{1,.5f,.25f},5,4});
        if(s.computeGiVerify && !s.computeGi.publishLights(sources)){waitFrame();return;}
        if(!s.dynamicGi.publishLights(std::move(sources))){s.failed=true;return;}
        // Small synthetic fixture only: production hosts retain bounded per-frame updates.
        for(int attempt=0;s.dynamicGi.reference().scheduler().pending() && attempt<64;++attempt)
            if(!s.dynamicGi.updateFrame({0,0,0}).canPresent){s.failed=true;return;}
        if(s.dynamicGi.reference().scheduler().pending()){s.failed=true;return;}
    }
    if(s.terrainGiVerify && !s.terrainGi.vertices.valid() && !s.terrainGi.initialize(d)) {
        std::fprintf(stderr,"Terrain GI setup FAIL: %s\n",d.lastError().c_str());s.failed=true;return;
    }
    s.gridProbe=test;
    auto& r=s.renderer;
    r.lighting.sunIntensity=0;r.lighting.skyColor={};r.lighting.groundColor={};
    r.lighting.tonemap=false;r.lighting.fogDensity=0;
    r.setShadowQuality(ShadowQuality::Off);r.setTraceEnabled(false);
    r.setModelReflectionsEnabled(false);r.lights.setQuality(LightQuality::Off);
    r.setMaterialQuality(test==2?MaterialQuality::Low:MaterialQuality::High);
    auto& material=s.probeBg->materials[0];
    material.shading=ShadingModel::Pbr;material.baseColor={1,1,1,1};
    material.roughness=1;material.metallic=0;material.vertexColors=false;
    material.occlusionTex=test==3?1:-1;
    if(std::getenv("AIVERSE_GRAPH_GI_VERIFY") && !material.shaderGraph) {
        auto graph=Json::parse(R"({"version":1,"domain":"material","nodes":[
          {"id":"uv","op":"uv"},
          {"id":"a","op":"sampleTexture","texture":"textures/a.png","inputs":["uv"]},
          {"id":"b","op":"sampleTexture","texture":"textures/b.png","inputs":["uv"]},
          {"id":"c","op":"sampleTexture","texture":"textures/c.png","inputs":["uv"]},
          {"id":"d","op":"sampleTexture","texture":"textures/d.png","inputs":["uv"]},
          {"id":"ab","op":"multiply","inputs":["a","b"]},
          {"id":"cd","op":"multiply","inputs":["c","d"]},
          {"id":"color","op":"multiply","inputs":["ab","cd"]}],"outputs":{"color":"color"}})");
        material.shaderGraph=std::make_shared<graph::Program>(graph::compile(graph,"material"));
        std::fill(std::begin(material.graphTextures),std::end(material.graphTextures),-1); // four independently bound white fallback textures
    }
    const bool verifyVt=std::getenv("AIVERSE_VT_GI_VERIFY")!=nullptr;
    if(verifyVt) {
        if(!s.giVtAtlas.valid()) {
            const uint8_t atlas[16]={128,128,128,255,128,128,128,255,128,128,128,255,128,128,128,255};
            const uint8_t table[4]={0,0,0,255};
            gfx::TextureDesc td;td.width=td.height=2;td.dimension=gfx::TextureDimension::Texture2DArray;
            td.arrayLayers=1;td.data=atlas;td.mipmaps=false;td.format=gfx::PixelFormat::RGBA8;
            s.giVtAtlas=d.createTexture(td);
            td.width=td.height=1;td.data=table;s.giVtTable=d.createTexture(td);
            if(!s.giVtAtlas.valid() || !s.giVtTable.valid()){s.failed=true;return;}
        }
        r.setModelVirtualTextureResolver([&s](const Model&,const Material&,ModelVirtualTextureBinding& binding) {
            binding.physicalAtlas=s.giVtAtlas;binding.pageTable=s.giVtTable;binding.gutterPixels=0;
            return true;
        });
    }
    PrismaSettings settings;settings.hdr=HdrMode::On;
    s.post.setSettings(settings);s.post.setQuality(PrismaQuality::High);
    r.beginScene(s.camera,w,h);
    if(s.computeGiVerify) {
        s.computeGi.updateFrame({0,0,0});
        if(!s.computeGi.canPresent()){waitFrame();return;}
    }
    // Case 1 intentionally does not reattach: beginScene must clear the previous view.
    if(test!=1 && !(s.computeGiVerify?r.setDynamicGiOverlay(s.computeGi):r.setDynamicGiOverlay(s.dynamicGi))){s.failed=true;return;}
    if(s.terrainGiVerify)r.drawCustomOpaque([&s,test](gfx::Device& device) {
        if(!s.terrainGi.draw(device,s.renderer,test==2,test!=3))s.failed=true;
    });
    else {
        ModelDraw draw;draw.model=s.probeBg.get();draw.flags=kDrawStatic;r.drawModel(draw);
        if(std::getenv("AIVERSE_ICB_GI_VERIFY")) {
            draw.world=mat4::translate({.7f,.7f,0})*mat4::scale({.1f,.1f,.1f});r.drawModel(draw);
        }
    }
    r.render(s.output,s.pipeline());r.resolvePrisma();
    if(s.frame==5) {
        if(std::getenv("AIVERSE_ICB_GI_VERIFY")) {
            const auto stats=r.gpuModelDrawStats();
            const bool ok=stats.gpuEncodedSubmissions>0 && stats.gpuEncodedCommands>=2;
            std::printf("GI ICB actual submissions %u commands %u %s\n",stats.gpuEncodedSubmissions,stats.gpuEncodedCommands,ok?"PASS":"FAIL");
            if(!ok){s.failed=true;return;}
        }
        const auto& cfg=s.dynamicGi.config();
        auto expected=sampleDynamicGi(s.dynamicGi.reference(),cfg.origin,cfg.spacing,
                                      cfg.nx,cfg.ny,cfg.nz,{0,0,0},{0,0,1});
        // Model base-color samples are decoded from sRGB before lighting.
        if(verifyVt)expected=expected*std::pow(((128.0f/255.0f)+0.055f)/1.055f,2.4f);
        if(test==1 || test==3 || test==4)expected={};
        if((test==0 || test==2 || test==5) && expected.x<.01f)s.failed=true;
        auto encode=[](float v){return v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;};
        expected={encode(expected.x),encode(expected.y),encode(expected.z)};
        auto receive=[&s,test,expected](int cw,int ch,std::vector<uint8_t> px) {
            if(cw<4 || ch<4 || px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            float actual[3]={};const float target[3]={expected.x,expected.y,expected.z};
            for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)
                for(int c=0;c<3;++c)actual[c]+=px[size_t(y*cw+x)*4+c]/(16*255.0f);
            bool ok=true;for(int c=0;c<3;++c)ok &= std::abs(actual[c]-target[c])<4/255.0f;
            std::printf("Dynamic GI GPU %d: RGB %.5f %.5f %.5f expected %.5f %.5f %.5f %s\n",
                test,actual[0],actual[1],actual[2],target[0],target[1],target[2],ok?"PASS":"FAIL");
            s.failed |= !ok;++s.probeIndex;s.frame=0;
            if(s.probeIndex==6)s.captured=true;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
}
