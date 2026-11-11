// Host-only verified reconstruction of an untextured repeated glTF mesh.
// Requires one flattened mesh, equally sized index groups and a node group describing instances.
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
using namespace rts;
using namespace rts::render;
namespace fs=std::filesystem;
namespace {
struct Vertex { float3 p,n; float2 uv; };
struct Part { std::vector<Vertex> vertices; std::vector<uint32_t> indices; int material=0; float error=0; };
Json number(int64_t n){return Json::number(n);}
Json vector(float3 v){Json a=Json::array();for(float f:{v.x,v.y,v.z})a.push(Json::number(double(f),9));return a;}
std::vector<uint8_t> read(const fs::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("cannot read "+p.string());auto n=f.tellg();if(n<0)throw std::runtime_error("invalid file size");std::vector<uint8_t> b(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));if(!f)throw std::runtime_error("truncated file");return b;}
float3 v3(const Json& j,float3 def={}){return j.size()==3?float3{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())}:def;}
mat4 local(const Json& n){
    if(n["matrix"].size()==16){mat4 m;for(int i=0;i<16;++i)m.m[i]=float(n["matrix"][size_t(i)].toDouble());return m;}
    quat q;if(n["rotation"].size()==4)q=qnormalize({float(n["rotation"][0].toDouble()),float(n["rotation"][1].toDouble()),float(n["rotation"][2].toDouble()),float(n["rotation"][3].toDouble())});
    return mat4::trs(v3(n["translation"]),q,v3(n["scale"],{1,1,1}));
}
struct Access {
    const std::vector<uint8_t>* bytes=nullptr;size_t start=0,stride=0,count=0;int component=0,comps=0;
    const uint8_t* ptr(size_t i)const{if(i>=count)throw std::runtime_error("accessor index out of range");return bytes->data()+start+i*stride;}
    float f(size_t i,int c)const{if(component!=5126||c>=comps)throw std::runtime_error("expected float attribute");float v;std::memcpy(&v,ptr(i)+size_t(c)*4,4);return v;}
    uint32_t index(size_t i)const{uint32_t n=0;if(component==5125)std::memcpy(&n,ptr(i),4);else if(component==5123){uint16_t s;std::memcpy(&s,ptr(i),2);n=s;}else throw std::runtime_error("expected uint indices");return n;}
    float3 xyz(size_t i)const{return {f(i,0),f(i,1),f(i,2)};}
};
Access access(const Json& doc,const std::vector<uint8_t>& bytes,int id){
    if(id<0||id>=int(doc["accessors"].size()))throw std::runtime_error("missing accessor");
    const auto& a=doc["accessors"][size_t(id)];if(a.has("sparse"))throw std::runtime_error("sparse input is unsupported");
    const auto& v=doc["bufferViews"][size_t(a.getInt("bufferView",-1))];
    if(v.getInt("buffer")!=0)throw std::runtime_error("expected buffer 0");
    Access r;r.bytes=&bytes;r.component=int(a.getInt("componentType"));auto type=a.getStr("type");r.comps=type=="VEC3"?3:type=="VEC2"?2:type=="SCALAR"?1:0;
    if(!r.comps)throw std::runtime_error("unsupported accessor shape");
    size_t size=size_t(r.comps)*(r.component==5123?2:4);r.count=size_t(a.getInt("count"));r.stride=size_t(v.getInt("byteStride",int64_t(size)));r.start=size_t(v.getInt("byteOffset"))+size_t(a.getInt("byteOffset"));
    if(!r.count||r.stride<size||r.start>bytes.size()||(r.count-1)>(bytes.size()-r.start)/r.stride||r.start+(r.count-1)*r.stride+size>bytes.size())throw std::runtime_error("invalid accessor range");
    return r;
}
float difference(float3 a,float3 b){return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)});}
struct Output {
    Json doc=Json::object(),views=Json::array(),accessors=Json::array();std::vector<uint8_t> bytes;
    int append(const void* data,size_t size,size_t count,int component,const char* type){
        while(bytes.size()%4)bytes.push_back(0);Json view=Json::object();view.set("buffer",number(0));view.set("byteOffset",number(int64_t(bytes.size())));view.set("byteLength",number(int64_t(size)));int vi=int(views.size());views.push(view);
        auto* b=static_cast<const uint8_t*>(data);bytes.insert(bytes.end(),b,b+size);Json a=Json::object();a.set("bufferView",number(vi));a.set("componentType",number(component));a.set("count",number(int64_t(count)));a.set("type",Json::string(type));int ai=int(accessors.size());accessors.push(a);return ai;
    }
    Json primitive(const Part& p){
        std::vector<float> pos,nrm,uv;float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
        for(auto& v:p.vertices){pos.insert(pos.end(),{v.p.x,v.p.y,v.p.z});nrm.insert(nrm.end(),{v.n.x,v.n.y,v.n.z});uv.insert(uv.end(),{v.uv.x,v.uv.y});mn=min3(mn,v.p);mx=max3(mx,v.p);}
        Json at=Json::object();int a=append(pos.data(),pos.size()*4,p.vertices.size(),5126,"VEC3");accessors.at(size_t(a)).set("min",vector(mn));accessors.at(size_t(a)).set("max",vector(mx));at.set("POSITION",number(a));
        at.set("NORMAL",number(append(nrm.data(),nrm.size()*4,p.vertices.size(),5126,"VEC3")));at.set("TEXCOORD_0",number(append(uv.data(),uv.size()*4,p.vertices.size(),5126,"VEC2")));
        Json pr=Json::object();pr.set("attributes",at);pr.set("indices",number(append(p.indices.data(),p.indices.size()*4,p.indices.size(),5125,"SCALAR")));pr.set("material",number(p.material));return pr;
    }
};
}
int main(int argc,char** argv){
    try{
        static_assert(std::endian::native==std::endian::little);
        if(argc!=4)throw std::runtime_error("usage: aiverse_reinstance input.gltf output.gltf instance-root-name");
        fs::path input=argv[1],output=argv[2];Json doc=Json::parseFile(input.string());
        if(doc["meshes"].size()!=1||doc["buffers"].size()!=1||doc["images"].size()!=0||doc["skins"].size()!=0||doc["animations"].size()!=0)throw std::runtime_error("requires one untextured static mesh and one external buffer");
        std::string uri=doc["buffers"][0].getStr("uri");if(uri.empty()||uri.find(':')!=std::string::npos)throw std::runtime_error("requires an external buffer");auto bytes=read(input.parent_path()/uri);
        const auto& nodes=doc["nodes"];std::vector<int> parents(nodes.size(),-1);int root=-1,meshNode=-1;
        for(size_t i=0;i<nodes.size();++i){if(nodes[i].getStr("name")==argv[3])root=int(i);if(nodes[i].getInt("mesh",-1)==0){if(meshNode>=0)throw std::runtime_error("multiple flattened mesh nodes");meshNode=int(i);}for(const auto& child:nodes[i]["children"].items()){int c=int(child.integer());if(c<0||c>=int(nodes.size())||parents[size_t(c)]!=-1)throw std::runtime_error("invalid node parent");parents[size_t(c)]=int(i);}}
        if(root<0||meshNode<0)throw std::runtime_error("instance root or mesh missing");
        std::vector<mat4> worlds(nodes.size());std::vector<uint8_t> state(nodes.size());
        auto resolve=[&](auto&& self,int n)->void{if(state[size_t(n)]==2)return;if(state[size_t(n)]==1)throw std::runtime_error("node cycle");state[size_t(n)]=1;if(parents[size_t(n)]>=0)self(self,parents[size_t(n)]);worlds[size_t(n)]=parents[size_t(n)]>=0?worlds[size_t(parents[size_t(n)])]*local(nodes[size_t(n)]):local(nodes[size_t(n)]);state[size_t(n)]=2;};
        for(size_t i=0;i<nodes.size();++i)resolve(resolve,int(i));
        std::vector<int> instances;for(const auto& n:nodes[size_t(root)]["children"].items())instances.push_back(int(n.integer()));if(instances.size()<2)throw std::runtime_error("need at least two instances");
        const mat4 meshWorld=worlds[size_t(meshNode)],canonical=worlds[size_t(instances[0])].inverse()*meshWorld;
        const mat4 canonicalNormal=canonical.normalMatrix(),meshNormal=meshWorld.normalMatrix();
        std::vector<Part> prototypes;float maxPositionError=0,maxNormalError=0,maxUvError=0;
        for(const auto& pr:doc["meshes"][0]["primitives"].items()){
            if(pr.has("targets")||pr.has("extensions")||pr["attributes"].size()!=3)throw std::runtime_error("unsupported primitive attributes or deformation");
            if(pr.getInt("mode",4)!=4)throw std::runtime_error("requires triangle primitives");
            Access ix=access(doc,bytes,int(pr.getInt("indices"))),pos=access(doc,bytes,int(pr["attributes"].getInt("POSITION"))),normal=access(doc,bytes,int(pr["attributes"].getInt("NORMAL"))),uv=access(doc,bytes,int(pr["attributes"].getInt("TEXCOORD_0")));
            if(ix.count%instances.size())throw std::runtime_error("unequal instance index groups");size_t count=ix.count/instances.size();if(count%3)throw std::runtime_error("invalid triangle groups");
            Part p;p.material=int(pr.getInt("material"));std::map<uint32_t,uint32_t> unique;
            for(size_t k=0;k<count;++k){uint32_t id=ix.index(k);auto [it,added]=unique.emplace(id,uint32_t(p.vertices.size()));if(added)p.vertices.push_back({canonical.transformPoint(pos.xyz(id)),normalize(canonicalNormal.transformDir(normal.xyz(id))),{uv.f(id,0),uv.f(id,1)}});p.indices.push_back(it->second);}
            std::vector<float3> expectedPosition(p.vertices.size()),expectedNormal(p.vertices.size());
            // Exporters can reorder flattened groups independently from the instance node list.
            std::map<std::array<int,3>,std::vector<size_t>> locations;
            auto key=[](float3 v){return std::array<int,3>{int(std::floor(v.x*1000)),int(std::floor(v.y*1000)),int(std::floor(v.z*1000))};};
            for(size_t i=0;i<instances.size();++i)locations[key(worlds[size_t(instances[i])].transformPoint(p.vertices[p.indices[0]].p))].push_back(i);
            std::set<size_t> matched;
            for(size_t i=0;i<instances.size();++i){
                float3 actualFirst=meshWorld.transformPoint(pos.xyz(ix.index(i*count)));auto k=key(actualFirst);size_t found=instances.size();
                for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){auto it=locations.find({k[0]+x,k[1]+y,k[2]+z});if(it==locations.end())continue;
                    for(size_t candidate:it->second)if(!matched.count(candidate)&&difference(actualFirst,worlds[size_t(instances[candidate])].transformPoint(p.vertices[p.indices[0]].p))<=0.0001f){if(found!=instances.size())throw std::runtime_error("ambiguous instance location");found=candidate;}}
                if(found==instances.size())throw std::runtime_error("no instance matches flattened group "+std::to_string(i));matched.insert(found);
                const auto& world=worlds[size_t(instances[found])];const mat4 norm=world.normalMatrix();
                for(size_t v=0;v<p.vertices.size();++v){expectedPosition[v]=world.transformPoint(p.vertices[v].p);expectedNormal[v]=normalize(norm.transformDir(p.vertices[v].n));}
                for(size_t k=0;k<count;++k){uint32_t actual=ix.index(i*count+k),v=p.indices[k];float ep=difference(meshWorld.transformPoint(pos.xyz(actual)),expectedPosition[v]);float en=difference(normalize(meshNormal.transformDir(normal.xyz(actual))),expectedNormal[v]);float eu=std::max(std::abs(uv.f(actual,0)-p.vertices[v].uv.x),std::abs(uv.f(actual,1)-p.vertices[v].uv.y));maxPositionError=std::max(maxPositionError,ep);maxNormalError=std::max(maxNormalError,en);maxUvError=std::max(maxUvError,eu);
                    if(ep>0.0001f||en>0.002f||eu>0.0001f)throw std::runtime_error("instance "+std::to_string(i)+" differs from prototype: position="+std::to_string(ep)+" normal="+std::to_string(en)+" UV="+std::to_string(eu));}
            }
            std::cout<<"Verified material "<<p.material<<": "<<p.indices.size()/3<<" triangles x "<<instances.size()<<std::endl;prototypes.push_back(std::move(p));
        }
        Output out;out.doc=doc;Json meshes=Json::array(),audit=Json::object(),mesh=Json::object(),primitives=Json::array();uint64_t triangles=0;
        for(const auto& p:prototypes){triangles+=p.indices.size()/3;primitives.push(out.primitive(p));}
        mesh.set("name",Json::string("shared prototype"));mesh.set("primitives",primitives);meshes.push(mesh);out.doc.set("meshes",meshes);
        for(int n:instances)out.doc.member("nodes").at(size_t(n)).set("mesh",number(0));out.doc.member("nodes").at(size_t(meshNode)).erase("mesh");
        out.doc.set("accessors",out.accessors);out.doc.set("bufferViews",out.views);Json buffers=Json::array(),buffer=Json::object();auto bin=output;bin.replace_extension(".bin");buffer.set("uri",Json::string(bin.filename().string()));buffer.set("byteLength",number(int64_t(out.bytes.size())));buffers.push(buffer);out.doc.set("buffers",buffers);
        audit.set("instances",number(int64_t(instances.size())));audit.set("maxPositionError",Json::number(double(maxPositionError),9));audit.set("maxNormalError",Json::number(double(maxNormalError),9));audit.set("maxUvError",Json::number(double(maxUvError),9));audit.set("prototypeTriangles",number(int64_t(triangles)));
        fs::create_directories(output.parent_path());std::ofstream bf(bin,std::ios::binary);bf.write(reinterpret_cast<const char*>(out.bytes.data()),std::streamsize(out.bytes.size()));if(!bf)throw std::runtime_error("cannot write buffer");
        std::ofstream jf(output);jf<<out.doc.dump()<<"\n";if(!jf)throw std::runtime_error("cannot write glTF");auto report=output;report.replace_extension("audit.json");std::ofstream af(report);af<<audit.dump()<<"\n";std::cout<<audit.dump()<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
