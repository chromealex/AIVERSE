#pragma once
// Asset-free lighting/occlusion reference fixture; intentionally simple geometry.
namespace aiverse::test {
inline void initFogLandscape(HdrScene& s,gfx::Device& d,const std::string& which){
    s.fogLandscape=true;const bool valley=which.starts_with("valley");
    auto height=[&](float x,float z){return valley?.035f*x*x+1.2f*std::sin(z*.16f)*std::sin(x*.13f):.5f*std::sin(x*.21f)+.4f*std::cos(z*.18f);};
    MeshBuilder terrain,trees,rocks;
    const int nx=70,nz=80;
    for(int z=0;z<=nz;++z)for(int x=0;x<=nx;++x){
        float wx=float(x)-35,wz=float(z)-55;
        float3 n=normalize(float3{height(wx-.1f,wz)-height(wx+.1f,wz),.2f,height(wx,wz-.1f)-height(wx,wz+.1f)});
        terrain.addVertex({wx,height(wx,wz),wz},n,0xffffffffu);
    }
    for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){uint32_t a=z*(nx+1)+x,b=a+1,c=a+nx+1,e=c+1;terrain.addTriangle(a,c,b);terrain.addTriangle(b,c,e);}
    uint32_t rng=17;auto random=[&](){rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.f;};
    for(int i=0;i<140;++i){
        float x=(random()-.5f)*64,z=random()*72-52;
        if(std::abs(x)<4&&z> -24)continue;
        float y=height(x,z),h=3+random()*5,r=.6f+h*.12f;
        trees.cylinder({x,y,z},.12f,h,6);
        for(int layer=0;layer<3;++layer)trees.cone({x,y+h*(.18f+.21f*layer),z},r*(1.f-.18f*layer),h*.57f,9);
    }
    for(int i=0;i<24;++i){float x=(random()-.5f)*58,z=random()*70-48,r=.8f+random()*1.3f;rocks.sphere({x,height(x,z)+r*.2f,z},r,5,8);}
    // Tall silhouettes reveal fog depth and foreground preservation.
    for(int i=0;i<6;++i){float x=float(i%2?1:-1)*(5+i*.8f),z=8-i*6.f;rocks.cylinder({x,height(x,z),z},.8f,3+i*.4f,6,true,.35f);}
    s.fogTerrain=terrain.upload(d);s.fogTrees=trees.upload(d);s.fogRocks=rocks.upload(d);
    s.camera.target={0,valley?5.f:1.4f,-13};s.camera.distance=valley?42.f:28.f;s.camera.pitch=valley?.25f:.11f;s.camera.yaw=.16f;
    if(which.find("above")!=std::string::npos){s.camera.pitch=.65f;s.camera.distance=45;}
    s.renderer.lighting.sunDir={-.6f,.5f,.5f};s.renderer.lighting.sunColor={.78f,.86f,1};s.renderer.lighting.sunIntensity=valley?1.1f:.65f;
    s.renderer.lighting.skyColor={.32f,.4f,.52f};s.renderer.lighting.groundColor={.15f,.19f,.16f};
    s.post.localFog().clear();LocalFogSettings settings;settings.enabled=!which.ends_with("-off");settings.ambient=valley?.045f:.04f;settings.anisotropy=.15f;settings.selfShadow=which.ends_with("-unshadowed")?0:1;settings.shadowDistance=24;s.post.setLocalFog(settings);
    FogVolume bank;bank.id=1;bank.region.shape=FogShape::Obb;bank.region.center={0,valley?6.f:3.f,-14};
    bank.region.halfExtents={48,valley?18.f:10.f,65};bank.region.feather=valley?5.f:3.f;
    bank.density=valley?.34f:.25f;bank.color={.72f,.8f,.88f};bank.structure.amount=1;
    bank.structure.scale=valley?float3{13,7,11}:float3{8,3.5f,7};bank.structure.coverage=valley?.50f:.48f;
    bank.structure.softness=.17f;bank.structure.warp=.85f;bank.structure.heightVariation=valley?9.f:4.f;bank.structure.heightBase=valley?1.f:.2f;bank.structure.heightFalloff=valley?.15f:.5f;
    bank.structure.wind={.6f,0,.2f};
    const FogRegion revealBounds=bank.region;
    // Discrete banks preserve open sightlines and expose their 3D upper surface.
    bank.region.shape=FogShape::Sphere;bank.region.feather=valley?5.f:3.f;
    bank.density=valley?.5f:.32f;bank.structure.coverage=valley?.55f:.60f;bank.structure.softness=.12f;bank.structure.edgeErosion=.5f;
    bank.structure.scale=valley?float3{6,4,6}:float3{3.5f,2.5f,3.5f};bank.structure.amount=1;
    bank.structure.heightFalloff=valley?.035f:.18f;bank.structure.heightVariation=valley?4.f:2.f;
    for(int i=0;i<3;++i){
        bank.id=uint32_t(i+1);
        bank.region.center=valley?float3{float(i-1)*7,3.f+float(i%2)*2,-12.f-float(i)*15}:float3{float(i-1)*7,-.5f,-7.f-float(i)*13};
        bank.region.radius=valley?(i==1?11.f:8.f):(i==1?6.f:4.8f);
        if(!s.post.localFog().upsert(bank)){s.failed=true;return;}
    }
    FogVolume haze;haze.id=4;haze.region=revealBounds;haze.region.feather=5;
    haze.density=.004f;haze.structure.amount=.7f;haze.structure.scale={18,5,18};
    haze.structure.coverage=.72f;haze.structure.heightBase=0;haze.structure.heightFalloff=.3f;
    haze.color=bank.color;haze.structure.wind=bank.structure.wind;
    if(!s.post.localFog().upsert(haze)){s.failed=true;return;}
    s.post.setLocalFogTime(which.ends_with("-wind")?8:0);
    if(which.ends_with("-reveal")){FogReveal r;r.id=1;r.region=revealBounds;r.region.feather=0;s.post.localFog().upsert(r);}
}
inline void drawFogLandscape(HdrScene& s){
    s.renderer.drawMesh(s.fogTerrain,{}, {.22f,.28f,.18f,1},kDrawUnlit);
    s.renderer.drawMesh(s.fogTrees,{}, {.09f,.18f,.12f,1},kDrawUnlit);
    s.renderer.drawMesh(s.fogRocks,{}, {.3f,.34f,.3f,1},kDrawUnlit);
}
}
