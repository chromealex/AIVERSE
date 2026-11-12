#include "GeometryScene.h"
#include "Renderer3D.h"
#include <algorithm>
#include <numeric>
#include <set>
#include <tuple>

namespace aiverse::render {
namespace {
float3 vector(const Json& j,float3 fallback){return j.isNull()?fallback:float3{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
// Cross-model merging is safe for untextured materials with identical render properties.
bool sameUntextured(const Material& a,const Material& b){
    auto key=[](const Material& m){return std::make_tuple(m.shading,m.baseColor.x,m.baseColor.y,m.baseColor.z,m.baseColor.w,
        m.metallic,m.roughness,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,m.emissiveStrength,
        m.alpha,m.alphaCutoff,m.doubleSided,m.vertexColors,m.castShadow,m.receiveShadow,m.transparent,m.team,m.teamStrength,
        m.uvOffset.x,m.uvOffset.y,m.uvScale.x,m.uvScale.y,m.uvRotation,m.fixedLight.x,m.fixedLight.y,m.fixedLight.z,m.fixedLight.w,m.fixedShade.x,m.fixedShade.y);};
    auto plain=[](const Material& m){return !m.shaderGraph&&m.baseTex<0&&m.mrTex<0&&m.normalTex<0&&m.occlusionTex<0&&m.emissiveTex<0&&m.teamTex<0;};
    return plain(a)&&plain(b)&&key(a)==key(b);
}
bool sameBoundMaterial(Material a,Material b,const std::vector<Image>& images){
    const int ai[]={a.baseTex,a.mrTex,a.normalTex,a.occlusionTex,a.emissiveTex,a.teamTex};
    const int bi[]={b.baseTex,b.mrTex,b.normalTex,b.occlusionTex,b.emissiveTex,b.teamTex};
    for(int k=0;k<6;++k){if((ai[k]<0)!=(bi[k]<0))return false;if(ai[k]>=0){const auto x=images[size_t(ai[k])].tex,y=images[size_t(bi[k])].tex;if(!x.valid()||!(x==y))return false;}}
    a.baseTex=a.mrTex=a.normalTex=a.occlusionTex=a.emissiveTex=a.teamTex=-1;
    b.baseTex=b.mrTex=b.normalTex=b.occlusionTex=b.emissiveTex=b.teamTex=-1;
    return sameUntextured(a,b);
}
float scaleBound(const mat4& m){float sum=0;for(int r=0;r<3;++r)for(int c=0;c<3;++c)sum+=m.at(r,c)*m.at(r,c);return std::sqrt(sum);}
}
void GeometryScene::shutdown(gfx::Device& dev){for(auto& g:groups_)if(g.proxy)releaseModel(dev,*g.proxy);for(auto& [k,m]:models_)if(m)releaseModel(dev,*m);models_.clear();instances_.clear();groups_.clear();warnings_.clear();stats_={};proxyVertices_=0;}
void GeometryScene::load(gfx::Device& dev,const Json& json,const FileReader& read){loadImpl(dev,json,read,{},2000000);}
void GeometryScene::loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){if(!vertexBudget||vertexBudget>16000000)throw std::runtime_error("geometry scene: prepared vertex budget must be 1..16000000");loadImpl(dev,json,{},std::move(models),vertexBudget);}
void GeometryScene::loadImpl(gfx::Device& dev,const Json& json,const FileReader& read,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){
    validate(json);shutdown(dev);models_=std::move(models);geometryEnabled_=surfacesEnabled_=cacheEnabled_=true;
    hlod_=json["hlod"].getBool("enabled");errorPixels_=float(json["hlod"]["errorPixels"].toDouble(2));maxPixels_=float(json["hlod"]["maxPixels"].toDouble(256));
    try{
        size_t vertices=0;
        for(const auto& [name,m]:models_){
            if(!m||m->vertices.empty()||m->indices.empty()||!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: prepared models must be nonempty rigid CPU meshes");
            if(m->vb.valid()||m->ib.valid())throw std::runtime_error("geometry scene: prepared geometry must not be uploaded");
            for(const auto& mesh:m->meshes)for(const auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph source unsupported");
            vertices+=m->vertices.size();
        }
        if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
        for(auto& i:json["instances"].items()){
            const auto file=i.getStr("model");
            if(!models_.count(file)){
                if(!read)throw std::runtime_error("geometry scene: missing prepared source "+file);
                auto m=loadModel(file,read,&warnings_);
                if(!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: animated model '"+file+"' must use ordinary entities");
                for(auto& mesh:m->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph model must use ordinary entities");
                vertices+=m->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                if(m->vertices.empty()||m->indices.empty())throw std::runtime_error("geometry scene: source has no triangles: "+file);
                models_[file]=std::move(m);
            }
            Instance in;in.model=models_.at(file).get();in.policy=parseGeometryPolicy(i["geometry"]);
            auto rot=vector(i["rotation"],{});constexpr float deg=.0174532925199433f;
            in.world=mat4::translate(vector(i["position"],{}))*mat4::rotateY(rot.y*deg)*mat4::rotateX(rot.x*deg)*mat4::rotateZ(rot.z*deg)*mat4::scale(vector(i["scale"],{1,1,1}));
            if(i.has("proxy")){
                const auto proxyFile=i["proxy"].getStr("model");
                if(!models_.count(proxyFile)){
                    if(!read)throw std::runtime_error("geometry scene: missing prepared proxy "+proxyFile);
                    auto proxy=loadModel(proxyFile,read,&warnings_);
                    vertices+=proxy->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                    if(proxy->vertices.empty()||proxy->indices.empty()||!proxy->skins.empty()||!proxy->clips.empty())throw std::runtime_error("geometry scene: proxy must be a nonempty rigid model");
                    for(auto& mesh:proxy->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph proxy unsupported");
                    models_[proxyFile]=std::move(proxy);
                }
                in.surfaceProxy=models_.at(proxyFile).get();in.proxyPixels=float(i["proxy"]["maxPixels"].toDouble(96));
                ++stats_.proxies;
            }
            instances_.push_back(in);
        }
        for(auto& [file,m]:models_)if(hlod_||std::any_of(instances_.begin(),instances_.end(),[&](const Instance& i){return i.model==m.get()&&(i.policy.clusters||i.policy.shadowClusters||i.policy.occluder);}))prepareGeometry(*m);
        if(hlod_&&!instances_.empty()){std::vector<size_t> ids(instances_.size());std::iota(ids.begin(),ids.end(),0);buildGroup(dev,std::move(ids));}
        for(auto& [k,m]:models_)if(!uploadModel(dev,*m)||!m->vb.valid()||!m->ib.valid())throw std::runtime_error("geometry scene: source GPU upload failed");
        for(auto& in:instances_){
            in.cache.prepare(*in.model,in.world);
            if(in.surfaceProxy)in.proxyCache.prepare(*in.surfaceProxy,in.world);
            in.min={1e30f,1e30f,1e30f};in.max={-1e30f,-1e30f,-1e30f};
            for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});in.min=min3(in.min,p);in.max=max3(in.max,p);}
        }
        for(auto& g:groups_)if(g.proxy)g.cache.prepare(*g.proxy,mat4{});
        stats_.instances=uint32_t(instances_.size());stats_.groups=uint32_t(groups_.size());
    }catch(...){shutdown(dev);throw;}
}
std::unique_ptr<Model> GeometryScene::makeProxy(const std::vector<size_t>& ids,float& error){
    // Explicit baked surfaces own their switch threshold. Do not bypass it with a parent proxy.
    for(auto id:ids)if(instances_[id].surfaceProxy)return {};
    // Transparent/masked/graph materials retain their original independent draws and sorting.
    for(auto id:ids)for(auto& m:instances_[id].model->materials)if(m.alpha!=AlphaMode::Opaque||m.transparent||m.shaderGraph)return {};
    auto out=std::make_unique<Model>();out->path="geometry:hlod";
    out->meshes.emplace_back();Node node;node.mesh=0;out->nodes.push_back(node);
    std::map<std::pair<const Model*,int>,int> materials;
    std::map<const Model*,int> images;
    for(auto id:ids){const auto& in=instances_[id];const Model& m=*in.model;
        if(proxyVertices_+out->vertices.size()+m.vertices.size()>2000000)return {};
        if(!images.count(&m)){images[&m]=int(out->images.size());out->images.insert(out->images.end(),m.images.begin(),m.images.end());}
        for(int n:m.order){const auto& nd=m.nodes[size_t(n)];if(nd.mesh<0)continue;mat4 xf=in.world*m.bindWorld[size_t(n)],normal=xf.normalMatrix();
            size_t pi=0;for(int mi=0;mi<nd.mesh;++mi)pi+=m.meshes[size_t(mi)].primitives.size();
            for(const auto& p:m.meshes[size_t(nd.mesh)].primitives){
                const auto key=std::make_pair(&m,p.material);
                if(!materials.count(key)){auto mat=m.materials[size_t(p.material)];int index=-1;
                    for(auto field:{&mat.baseTex,&mat.mrTex,&mat.normalTex,&mat.occlusionTex,&mat.emissiveTex,&mat.teamTex})if(*field>=0)*field+=images[&m];
                    for(size_t k=0;k<out->materials.size();++k)if(sameBoundMaterial(mat,out->materials[k],out->images)){index=int(k);break;}
                    if(index<0){index=int(out->materials.size());out->materials.push_back(mat);}
                    materials[key]=index;}
                // Build temporary per-material index lists after all transforms below.
                const int material=materials[key];
                std::map<uint32_t,uint32_t> remap;
                auto emit=[&](uint32_t first,uint32_t count){for(uint32_t k=0;k<count;++k){uint32_t source=m.indices[first+k];
                    if(!remap.count(source)){auto v=m.vertices[source];v.pos=xf.transformPoint(v.pos);v.nrm=normalize(normal.transformDir(v.nrm));auto t=normalize(xf.transformDir({v.tangent.x,v.tangent.y,v.tangent.z}));v.tangent={t.x,t.y,t.z,v.tangent.w};remap[source]=uint32_t(out->vertices.size());out->vertices.push_back(v);}
                    out->indices.push_back(remap[source]);
                }};
                const auto first=uint32_t(out->indices.size());
                const auto& clusters=m.geometry->primitives[pi++];
                if(clusters.empty())emit(p.firstIndex,p.indexCount);
                else for(auto& c:clusters){emit(c.coarseFirst,c.coarseCount);error=std::max(error,c.error*scaleBound(xf));}
                // Each segment is tagged; regrouped by material at the end.
                Primitive segment;segment.material=material;segment.firstIndex=first;segment.indexCount=uint32_t(out->indices.size())-first;
                out->meshes[0].primitives.push_back(segment);
            }
        }
    }
    std::vector<uint32_t> indices;std::vector<Primitive> primitives;
    for(size_t material=0;material<out->materials.size();++material){Primitive p;p.material=int(material);p.firstIndex=uint32_t(indices.size());
        for(auto& segment:out->meshes[0].primitives)if(segment.material==int(material))indices.insert(indices.end(),out->indices.begin()+segment.firstIndex,out->indices.begin()+segment.firstIndex+segment.indexCount);
        p.indexCount=uint32_t(indices.size())-p.firstIndex;p.vertexCount=uint32_t(out->vertices.size());if(p.indexCount)primitives.push_back(p);}
    // Existing instancing already combines identical primitives in batches of 32.
    std::map<std::pair<const Model*,const Primitive*>,size_t> batches;size_t sourceIndices=0;
    for(auto id:ids){const auto* model=instances_[id].model;for(int n:model->order){const auto& node=model->nodes[size_t(n)];if(node.mesh<0)continue;
        for(const auto& p:model->meshes[size_t(node.mesh)].primitives){++batches[{model,&p}];sourceIndices+=p.indexCount;}}}
    size_t sourceDraws=0;for(auto [key,count]:batches)sourceDraws+=(count+31)/32;
    if(primitives.size()>=sourceDraws && indices.size()>=sourceIndices)return {};
    out->indices=std::move(indices);out->meshes[0].primitives=std::move(primitives);out->finalize();proxyVertices_+=out->vertices.size();return out;
}
int GeometryScene::buildGroup(gfx::Device& dev,std::vector<size_t> ids){
    const int index=int(groups_.size());groups_.emplace_back();
    float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(auto id:ids){const auto& in=instances_[id];for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});mn=min3(mn,p);mx=max3(mx,p);}}
    groups_[size_t(index)].min=mn;groups_[size_t(index)].max=mx;
    if(ids.size()>8){const auto e=mx-mn;const int axis=e.x>=e.z?0:2;std::stable_sort(ids.begin(),ids.end(),[&](size_t a,size_t b){auto x=instances_[a].world.transformPoint({}),y=instances_[b].world.transformPoint({});return axis==0?x.x<y.x:x.z<y.z;});
        auto middle=ids.begin()+ids.size()/2;const int l=buildGroup(dev,{ids.begin(),middle}),r=buildGroup(dev,{middle,ids.end()});groups_[size_t(index)].left=l;groups_[size_t(index)].right=r;
    }else groups_[size_t(index)].instances=ids;
    auto& group=groups_[size_t(index)];
    group.occlusion=std::all_of(ids.begin(),ids.end(),[&](size_t id){return instances_[id].policy.occlusion;});
    if(ids.size()>1){group.proxy=makeProxy(ids,group.error);if(group.proxy){if(!uploadModel(dev,*group.proxy)||!group.proxy->vb.valid()||!group.proxy->ib.valid())throw std::runtime_error("geometry scene: proxy GPU upload failed");++stats_.proxies;}}
    return index;
}
void GeometryScene::drawGroup(Renderer3D& r,int index){auto& g=groups_[size_t(index)];
    // Retain offscreen source draws so their shadows/reflections remain available.
    const float extent=length(g.max-g.min);
    const bool far=geometryErrorPixels(r.viewProj(),g.min,g.max,extent,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=maxPixels_;
    if(g.proxy&&far&&geometryErrorPixels(r.viewProj(),g.min,g.max,g.error,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=errorPixels_){ModelDraw d;d.model=g.proxy.get();d.staticCache=cacheEnabled_?&g.cache:nullptr;d.geometry.occlusion=geometryEnabled_&&g.occlusion;r.drawModel(d);++stats_.submitted;return;}
    if(g.left>=0){drawGroup(r,g.left);drawGroup(r,g.right);return;}
    for(auto id:g.instances)drawInstance(r,instances_[id]);
}
void GeometryScene::draw(Renderer3D& r){stats_.submitted=0;stats_.surfaceProxiesSubmitted=0;if(hlod_&&!groups_.empty())drawGroup(r,0);else for(auto& i:instances_)drawInstance(r,i);}
void GeometryScene::drawInstance(Renderer3D& r,const Instance& i){
    ModelDraw d;d.model=i.model;d.world=i.world;d.geometry=geometryEnabled_?i.policy:GeometryPolicy{};d.staticCache=cacheEnabled_?&i.cache:nullptr;
    if(i.surfaceProxy&&surfacesEnabled_){
        const auto mn=i.min,mx=i.max;
        if(geometryErrorPixels(r.viewProj(),mn,mx,length(mx-mn),r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=i.proxyPixels){
            ++stats_.surfaceProxiesSubmitted;d.model=i.surfaceProxy;d.staticCache=cacheEnabled_?&i.proxyCache:nullptr;d.geometry.clusters=false;d.geometry.shadowClusters=false;d.geometry.occluder=false;
        }
    }
    r.drawModel(d);++stats_.submitted;
}

}
