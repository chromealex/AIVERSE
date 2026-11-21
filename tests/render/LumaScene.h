// Actual Metal receiver -> direct compute -> HDR composite acceptance.
#pragma once
namespace aiverse::test {
inline void drawLumaScene(HdrScene& s,gfx::Device& d,int w,int h) {
    if(s.ownsFrame)d.beginFrame(w,h);
    const int test=s.probeIndex;
    if(!s.luma) {
        s.luma=std::make_unique<LumaWeavePostProcess>(s.post,s.renderer.lights);
        if(!s.luma->initialize(d) || !s.luma->publishGeometry({},1)) {
            std::fprintf(stderr,"Luma initialize FAIL: %s\n",d.lastError().c_str());s.failed=true;return;
        }
    }
    s.luma->pollGpu();
    if(s.gridProbe!=test) {
        if(s.luma->screen().busy()) {if(s.ownsFrame)d.endFrame();return;}
        std::vector<LumaLight> lights;
        if(test!=2) for(uint64_t i=1;i<=128;++i) {
            LumaLight light;light.key=i;light.position={0,0,1};light.radius=2;
            light.intensity=4.0f/128;light.color={1,.5f,.25f};lights.push_back(light);
        }
        s.lumaSources.publish(std::move(lights));
        LumaClusterConfig config;config.origin={-4,-4,-4};config.nx=config.ny=config.nz=1;
        s.lumaClusters.rebuild(s.lumaSources,config);
        if(!s.luma->publishSources(s.lumaSources,s.lumaClusters)) {s.failed=true;return;}
        GiGpuBvh geometry;
        if(test==6) {
            GiChunkedGeometryBuilder builder;
            builder.replace(1,{{{-8,-8,.5f},{8,-8,.5f},{8,8,.5f},0},
                               {{-8,-8,.5f},{8,8,.5f},{-8,8,.5f},0}},{{1,1,1}});
            geometry=GiGpuBvh::build(builder.publish());
        }
        if(!s.luma->publishGeometry(geometry,uint64_t(test)+2)){s.failed=true;return;}
        s.gridProbe=test;s.frame=0;
    }
    auto& r=s.renderer;
    r.lighting.sunIntensity=0;r.lighting.skyColor={};r.lighting.groundColor={};
    r.lighting.tonemap=false;r.setTraceEnabled(false);r.setModelReflectionsEnabled(false);
    r.lights.setQuality(test==4?LightQuality::High:LightQuality::Off);
    auto& m=s.probeBg->materials[0];m.shading=ShadingModel::Pbr;
    m.baseColor={1,1,1,1};m.metallic=(test==3||test==8)?1:0;m.roughness=1;
    PrismaSettings settings;settings.hdr=HdrMode::On;s.post.setSettings(settings);
    s.post.setQuality(PrismaQuality::High);
    s.luma->setEnabled(test!=1);
    s.luma->setQuality((test==5||test==8)?LumaWeaveQuality::High:LumaWeaveQuality::Balanced);
    r.beginScene(s.camera,w,h);
    if(test==4)r.lights.add({0,0,-1},2,{1,1,1},1); // behind surface: no direct contribution
    ModelDraw draw;draw.model=s.probeBg.get();r.drawModel(draw);
    r.render(s.output,s.luma.get());r.resolvePrisma();
    if(s.frame==12) {
        const auto stats=s.luma->stats();
        bool stateOk=(test==1 || test==4)?!stats.composited:stats.composited;
        if(test==4)stateOk &= stats.legacyConflict;
        if(test==5||test==8)stateOk &= stats.materialShaded;
        // At N=L=V, radius=2, distance=1: attenuation=.28125.
        // Balanced Lambert reflectance = incident/pi. GGX roughness=1:
        // diffuse=(1-F0)/pi, specular=F0/(4*pi), F0=.04.
        const float energy=((test==0||test==7)?1.0f:test==5?.97f:test==8?.25f:0.0f)*4*.28125f/3.14159265358979323846f;
        auto encode=[](float v){return v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;};
        const float3 expected={encode(energy),encode(energy*.5f),encode(energy*.25f)};
        auto receive=[&s,test,expected,stateOk](int cw,int ch,std::vector<uint8_t> px) {
            if(cw<4||ch<4||px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            float actual[3]={};const float target[]={expected.x,expected.y,expected.z};
            for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)
                for(int c=0;c<3;++c)actual[c]+=px[size_t(y*cw+x)*4+c]/(16*255.0f);
            bool ok=stateOk;for(int c=0;c<3;++c)ok &= std::abs(actual[c]-target[c])<5/255.0f;
            std::printf("Luma GPU %d: RGB %.5f %.5f %.5f expected %.5f %.5f %.5f state=%d %s\n",
                test,actual[0],actual[1],actual[2],target[0],target[1],target[2],stateOk,ok?"PASS":"FAIL");
            s.failed |= !ok;++s.probeIndex;s.frame=0;if(s.probeIndex==9)s.captured=true;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
}
