#include "SurfaceProxy.h"
#include <algorithm>
#include <array>
#include <limits>

namespace aiverse::modeling {
namespace {
float linear(float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); }
float srgb(float v) { return v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - .055f; }
uint8_t byte(float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255)); }
float component(float3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
float4 sample(const Model& m, int id, float2 uv, bool color = false) {
    if (id < 0) return {1,1,1,1};
    require(size_t(id) < m.images.size(), "surface proxy: invalid texture index");
    const auto& im = m.images[size_t(id)];
    require(im.width > 0 && im.height > 0 && im.rgba.size() == size_t(im.width)*size_t(im.height)*4,
            "surface proxy: texture CPU pixels unavailable");
    auto coord = [&](int v, int n) { return im.repeat ? (v % n + n) % n : std::clamp(v, 0, n-1); };
    auto texel = [&](int x, int y) {
        const size_t k = (size_t(coord(y,im.height))*im.width + coord(x,im.width))*4;
        float4 c{im.rgba[k]/255.f,im.rgba[k+1]/255.f,im.rgba[k+2]/255.f,im.rgba[k+3]/255.f};
        if (color) { c.x=linear(c.x); c.y=linear(c.y); c.z=linear(c.z); }
        return c;
    };
    // Reduce UV before integer conversion, including large authored tiling values.
    uv.x=im.repeat ? uv.x-std::floor(uv.x) : std::clamp(uv.x,0.f,1.f);
    uv.y=im.repeat ? uv.y-std::floor(uv.y) : std::clamp(uv.y,0.f,1.f);
    const float x=uv.x*im.width-.5f,y=uv.y*im.height-.5f;
    if(im.nearest) return texel(int(std::floor(x+.5f)),int(std::floor(y+.5f)));
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;
    float4 c{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){auto t=texel(ix+i,iy+j);float w=(i?fx:1-fx)*(j?fy:1-fy);
        c.x+=t.x*w;c.y+=t.y*w;c.z+=t.z*w;c.w+=t.w*w;}
    return c;
}
struct Triangle { ModelVertex v[3]; int material; };
struct Face { float3 u,v,n; float loU,hiU,loV,hiV,depth; };
float edge(float2 a,float2 b,float2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
}
Build bakeSurfaceProxy(const Model& src, const Json& options) {
    require(options.isObject(), "surface proxy: options must be an object");
    for(auto& [k,v]:options.members())require(k=="mode"||k=="axis"||k=="resolution", "surface proxy: unknown option '"+k+"'");
    for(const char* key:{"mode","axis"})require(!options.has(key)||options[key].isString(),std::string("surface proxy: string required for ")+key);
    const auto mode=options.getStr("mode","box"),axis=options.getStr("axis","auto");
    require(mode=="plane"||mode=="box", "surface proxy: mode must be plane or box");
    require(axis=="auto"||axis=="x"||axis=="y"||axis=="z", "surface proxy: axis must be auto, x, y or z");
    int resolution=options.getInt("resolution",128);
    require(!options.has("resolution")||(options["resolution"].isNumber()&&options["resolution"].toDouble()==resolution),"surface proxy: integer resolution required");
    require(resolution>=16&&resolution<=512, "surface proxy: resolution must be 16..512 per face");
    require(src.skins.empty()&&src.clips.empty()&&src.variants.empty(), "surface proxy: animated models and material variants are unsupported");
    require(src.vertices.size()<=2000000&&src.indices.size()<=6000000,"surface proxy: source budget exceeded");
    for(const auto& mt:src.materials){
        require(mt.shading==ShadingModel::Pbr&&!mt.shaderGraph&&!mt.transparent&&mt.alpha!=AlphaMode::Blend&&
                mt.team==TeamMode::None&&mt.fixedLight.w==0,"surface proxy: requires static PBR opaque/masked materials without team colour or graphs");
        require(mt.emissive.x==0&&mt.emissive.y==0&&mt.emissive.z==0&&mt.emissiveTex<0,
                "surface proxy: emissive materials are unsupported");
        for(int id:{mt.baseTex,mt.mrTex,mt.normalTex,mt.occlusionTex})if(id>=0)(void)sample(src,id,{});
    }
    std::vector<Triangle> triangles;
    float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(int ni:src.order){
        const auto& node=src.nodes.at(size_t(ni));if(node.mesh<0)continue;
        const mat4 xf=src.bindWorld.at(size_t(ni)),normal=xf.normalMatrix();
        const float det=dot(xf.transformDir({1,0,0}),cross(xf.transformDir({0,1,0}),xf.transformDir({0,0,1})));
        require(std::isfinite(det)&&std::abs(det)>1e-12f,"surface proxy: degenerate node transform");
        for(const auto& p:src.meshes.at(size_t(node.mesh)).primitives){
            require(!p.morphTargets&&p.jointPalette.empty(),"surface proxy: morph/skin primitives unsupported");
            require(p.indexCount%3==0&&size_t(p.firstIndex)+p.indexCount<=src.indices.size(),"surface proxy: invalid primitive");
            require(p.material>=0&&size_t(p.material)<src.materials.size(),"surface proxy: invalid material");
            for(uint32_t i=0;i<p.indexCount;i+=3){Triangle t{};t.material=p.material;
                for(int k=0;k<3;++k){auto v=src.vertices.at(src.indices[p.firstIndex+i+k]);
                    v.pos=xf.transformPoint(v.pos);v.nrm=normalize(normal.transformDir(v.nrm));
                    auto tangent=normalize(xf.transformDir({v.tangent.x,v.tangent.y,v.tangent.z}));
                    v.tangent={tangent.x,tangent.y,tangent.z,v.tangent.w*(det<0?-1.f:1.f)};
                    require(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y)&&std::isfinite(v.pos.z)&&length(v.pos)<1e7f&&
                            std::isfinite(v.uv.x)&&std::isfinite(v.uv.y),"surface proxy: nonfinite/out-of-range vertex");
                    t.v[k]=v;mn=min3(mn,v.pos);mx=max3(mx,v.pos);
                }
                if(det<0)std::swap(t.v[1],t.v[2]);
                triangles.push_back(t);
                require(triangles.size()<=1000000,"surface proxy: transformed triangle budget exceeded");
            }
        }
    }
    require(!triangles.empty(),"surface proxy: empty source");
    float3 extent=mx-mn;
    int thin=extent.x<=extent.y&&extent.x<=extent.z?0:extent.y<=extent.z?1:2;
    int selected=axis=="auto"?thin:axis=="x"?0:axis=="y"?1:2;
    if(mode=="plane")require(component(extent,selected)<=std::max({extent.x,extent.y,extent.z})*.25f,
                              "surface proxy: plane requires a thin source (thickness <= 25% of largest extent); use box");
    std::vector<Face> faces;
    const float3 units[3]={{1,0,0},{0,1,0},{0,0,1}};
    for(int a=0;a<3;++a)if(mode=="box"||a==selected)for(int sign:{1,-1}){
        Face f{};f.n=units[a]*float(sign);f.u=units[(a+1)%3];f.v=cross(f.n,f.u);
        f.loU=f.loV=1e30f;f.hiU=f.hiV=f.depth=-1e30f;
        for(int c=0;c<8;++c){float3 p{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z};
            f.loU=std::min(f.loU,dot(p,f.u));f.hiU=std::max(f.hiU,dot(p,f.u));
            f.loV=std::min(f.loV,dot(p,f.v));f.hiV=std::max(f.hiV,dot(p,f.v));f.depth=std::max(f.depth,dot(p,f.n));}
        if(f.hiU-f.loU>1e-6f&&f.hiV-f.loV>1e-6f)faces.push_back(f);
    }
    require(!faces.empty(),"surface proxy: source has no surface area");
    // A four-pixel gutter isolates charts and dilates RGB without closing alpha holes.
    constexpr int gutter=4;const int tile=resolution+2*gutter,cols=3,rows=int((faces.size()+2)/3);
    Build result;result.model=std::make_unique<Model>();auto& out=*result.model;
    for(const char* name:{"baseColor","normal","metallicRoughnessOcclusion"}){
        Image im;im.name=name;im.width=cols*tile;im.height=rows*tile;im.repeat=false;
        im.rgba.resize(size_t(im.width)*im.height*4);out.images.push_back(std::move(im));}
    Material material;material.name="surfaceProxy";material.baseTex=0;material.normalTex=1;material.mrTex=2;material.occlusionTex=2;
    material.metallic=material.roughness=1;material.alpha=AlphaMode::Mask;material.vertexColors=false;
    out.materials.push_back(material);out.nodes.emplace_back();out.nodes[0].mesh=0;
    // RTS LOD maps nodes by name. Baked vertices already include all source transforms.
    std::string rootName="__aiverse_surface_proxy";while(src.findNode(rootName)>=0)rootName+="_";
    out.nodes[0].name=rootName;out.meshes.emplace_back();
    uint64_t work=0,covered=0;float maxDepth=0;
    for(size_t fi=0;fi<faces.size();++fi){
        const auto f=faces[fi];const int tx=int(fi%cols)*tile,ty=int(fi/cols)*tile;
        std::vector<float> depth(size_t(resolution)*resolution,-std::numeric_limits<float>::infinity());
        auto index=[&](int x,int y){return (size_t(ty+gutter+y)*out.images[0].width+tx+gutter+x)*4;};
        for(const auto& t:triangles){
            float2 q[3];for(int k=0;k<3;++k)q[k]={(dot(t.v[k].pos,f.u)-f.loU)/(f.hiU-f.loU)*resolution,
                                               (dot(t.v[k].pos,f.v)-f.loV)/(f.hiV-f.loV)*resolution};
            const float area=edge(q[0],q[1],q[2]);const auto& mt=src.materials[size_t(t.material)];
            if(std::abs(area)<1e-9f||(!mt.doubleSided&&area<=0))continue;
            int x0=std::clamp(int(std::floor(std::min({q[0].x,q[1].x,q[2].x}))),0,resolution-1);
            int x1=std::clamp(int(std::ceil(std::max({q[0].x,q[1].x,q[2].x}))),0,resolution-1);
            int y0=std::clamp(int(std::floor(std::min({q[0].y,q[1].y,q[2].y}))),0,resolution-1);
            int y1=std::clamp(int(std::ceil(std::max({q[0].y,q[1].y,q[2].y}))),0,resolution-1);
            work+=uint64_t(x1-x0+1)*(y1-y0+1);require(work<=100000000,"surface proxy: raster budget exceeded; lower resolution or split source");
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
                float2 p{float(x)+.5f,float(y)+.5f};float w[3]={edge(q[1],q[2],p)/area,edge(q[2],q[0],p)/area,edge(q[0],q[1],p)/area};
                if(w[0]<-1e-6f||w[1]<-1e-6f||w[2]<-1e-6f)continue;
                float d=0;float2 uv{};float3 n{},tangent{};float4 vc{};float hand=0;
                for(int k=0;k<3;++k){const auto& v=t.v[k];d+=dot(v.pos,f.n)*w[k];uv.x+=v.uv.x*w[k];uv.y+=v.uv.y*w[k];n+=v.nrm*w[k];
                    tangent+=float3{v.tangent.x,v.tangent.y,v.tangent.z}*w[k];hand+=v.tangent.w*w[k];
                    vc.x+=float(v.color&255)/255*w[k];vc.y+=float((v.color>>8)&255)/255*w[k];
                    vc.z+=float((v.color>>16)&255)/255*w[k];vc.w+=float(v.color>>24)/255*w[k];}
                auto& z=depth[size_t(y)*resolution+x];if(d<=z)continue;
                float u=uv.x*mt.uvScale.x,v=uv.y*mt.uvScale.y,c=std::cos(mt.uvRotation),s=std::sin(mt.uvRotation);
                uv={mt.uvOffset.x+c*u-s*v,mt.uvOffset.y+s*u+c*v};
                auto base=sample(src,mt.baseTex,uv,true);base.x*=mt.baseColor.x;base.y*=mt.baseColor.y;base.z*=mt.baseColor.z;base.w*=mt.baseColor.w;
                if(mt.vertexColors){base.x*=vc.x;base.y*=vc.y;base.z*=vc.z;base.w*=vc.w;}
                if(mt.alpha==AlphaMode::Mask&&base.w<mt.alphaCutoff)continue;
                z=d;n=normalize(n);tangent=normalize(tangent-n*dot(tangent,n));
                if(mt.normalTex>=0){auto tex=sample(src,mt.normalTex,uv);auto bitangent=cross(n,tangent)*(hand<0?-1.f:1.f);
                    n=normalize(tangent*((tex.x*2-1)*mt.normalScale)+bitangent*((tex.y*2-1)*mt.normalScale)+n*(tex.z*2-1));}
                if(area<0)n=-n;
                auto mr=sample(src,mt.mrTex,uv),ao=sample(src,mt.occlusionTex,uv);
                const size_t at=index(x,y);auto& b=out.images[0].rgba;auto& normalMap=out.images[1].rgba;auto& packed=out.images[2].rgba;
                b[at]=byte(srgb(base.x));b[at+1]=byte(srgb(base.y));b[at+2]=byte(srgb(base.z));b[at+3]=255;
                normalMap[at]=byte(dot(n,f.u)*.5f+.5f);normalMap[at+1]=byte(dot(n,f.v)*.5f+.5f);normalMap[at+2]=byte(dot(n,f.n)*.5f+.5f);normalMap[at+3]=255;
                packed[at]=byte(1-mt.occlusionStrength+mt.occlusionStrength*ao.x);packed[at+1]=byte(mr.y*mt.roughness);packed[at+2]=byte(mr.z*mt.metallic);packed[at+3]=255;
            }
        }
        size_t faceCovered=0;for(float d:depth)if(std::isfinite(d)){++faceCovered;maxDepth=std::max(maxDepth,f.depth-d);}
        if(!faceCovered)continue;
        covered+=faceCovered;
        // Dilation propagates attributes only; coverage alpha stays untouched, including holes.
        std::vector<uint8_t> valid(size_t(tile)*tile);
        for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x)valid[size_t(y+gutter)*tile+x+gutter]=out.images[0].rgba[index(x,y)+3]!=0;
        for(int pass=0;pass<gutter;++pass){auto next=valid;
            for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){size_t k=size_t(y)*tile+x;if(valid[k])continue;
                for(auto off:std::array<std::array<int,2>,4>{{{{-1,0}},{{1,0}},{{0,-1}},{{0,1}}}}){int xx=x+off[0],yy=y+off[1];
                    if(xx<0||xx>=tile||yy<0||yy>=tile||!valid[size_t(yy)*tile+xx])continue;
                    for(auto& im:out.images){size_t a=(size_t(ty+y)*im.width+tx+x)*4,b=(size_t(ty+yy)*im.width+tx+xx)*4;
                        for(int channel=0;channel<3;++channel)im.rgba[a+channel]=im.rgba[b+channel];}
                    next[k]=1;break;}}
            valid=std::move(next);}
        uint32_t first=uint32_t(out.vertices.size());
        for(int corner:{0,1,3,2}){float u=corner&1?f.hiU:f.loU,v=corner&2?f.hiV:f.loV;
            ModelVertex vert;vert.pos=f.u*u+f.v*v+f.n*f.depth;vert.nrm=f.n;vert.tangent={f.u.x,f.u.y,f.u.z,1};
            vert.uv={float(tx+gutter+((corner&1)?resolution:0))/out.images[0].width,
                     float(ty+gutter+((corner&2)?resolution:0))/out.images[0].height};out.vertices.push_back(vert);}
        out.indices.insert(out.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    }
    require(!out.indices.empty(),"surface proxy: no visible covered texels");
    Primitive primitive;primitive.indexCount=uint32_t(out.indices.size());primitive.vertexCount=uint32_t(out.vertices.size());out.meshes[0].primitives.push_back(primitive);out.finalize();
    auto& r=result.report;r=Json::object();r.set("sourceTriangles",Json::number(int64_t(triangles.size())));r.set("proxyTriangles",Json::number(int64_t(out.indices.size()/3)));
    r.set("mode",Json::string(mode));r.set("axis",Json::string(selected==0?"x":selected==1?"y":"z"));
    r.set("coverage",Json::number(double(covered)/(faces.size()*resolution*resolution),6));r.set("sampledDepthError",Json::number(double(maxDepth),6));
    r.set("textureBytes",Json::number(int64_t(out.images[0].rgba.size()*3)));
    r.set("warning",Json::string("Directional surface approximation for distant rigid scenery. No parallax or relighting of hidden surfaces. Coverage is sampled; inspect holes, silhouette and oblique views. sampledDepthError is not a certified geometric error bound."));
    return result;
}
}
