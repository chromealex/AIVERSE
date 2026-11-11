#include "World.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace aiverse::physics {
namespace {
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr const char* kArchitecture="arm64";
#elif defined(__x86_64__) || defined(_M_X64)
constexpr const char* kArchitecture="x64";
#elif defined(__EMSCRIPTEN__)
constexpr const char* kArchitecture="wasm";
#elif defined(__arm__) || defined(_M_ARM)
constexpr const char* kArchitecture="arm";
#else
constexpr const char* kArchitecture="other";
#endif
constexpr const char* kRevision="box3d:5d6364d7313e185e85e74674649f8e6c2ce50939:float:v1";
void check(bool ok,const std::string& msg){if(!ok)throw JsonError("physics: "+msg);}
float number(const Json& j,const char* key,float fallback,float lo,float hi){
    if(!j.has(key))return fallback;
    check(j[key].isNumber(),std::string(key)+" must be numeric");
    auto v=j[key].fx().toDouble();check(std::isfinite(v)&&v>=lo&&v<=hi,std::string(key)+" out of range");return float(v);
}
b3Vec3 vec(const Json& j,b3Vec3 fallback,float limit=10000){
    if(j.isNull())return fallback;
    check(j.isArray()&&j.size()==3,"vector requires three numbers");b3Vec3 v;
    float* out[]={&v.x,&v.y,&v.z};
    for(int i=0;i<3;++i){check(j[i].isNumber(),"vector must be numeric");auto x=j[i].fx().toDouble();check(std::isfinite(x)&&std::abs(x)<=limit,"vector out of range");*out[i]=float(x);}return v;
}
bool finite(b3Vec3 v,float limit){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<=limit&&std::abs(v.y)<=limit&&std::abs(v.z)<=limit;}
std::string abi(){return std::string(kRevision)+":"+kArchitecture+":"+AIVERSE_PHYSICS_ABI+":"+std::to_string(std::endian::native==std::endian::little)+":"+std::to_string(sizeof(void*))+":"+std::to_string(sizeof(b3BodyDef))+":"+std::to_string(sizeof(b3WorldDef));}
}
World::World(const Json& scene,int workers):scene_(scene),workers_(workers){
    check(workers>=1&&workers<=8,"workers must be 1..8");
#ifdef __EMSCRIPTEN__
    check(workers==1,"web requires one worker");
#endif
    check(scene.isObject(),"scene must be an object");
    auto rate=number(scene,"tickRate",60,10,240), steps=number(scene,"substeps",4,1,16);
    check(rate==std::floor(rate)&&steps==std::floor(steps),"tickRate and substeps must be integers");
    tickRate_=int(rate);substeps_=int(steps);
    auto def=b3DefaultWorldDef();def.workerCount=uint32_t(workers);def.gravity=vec(scene["gravity"],{0,-9.81f,0},1000);
    world_=b3CreateWorld(&def);
    try{
        const auto& bodies=scene["bodies"];check(bodies.isArray()&&bodies.size()<=4096,"bodies must be an array of at most 4096 objects");std::set<std::string> ids;
        for(size_t i=0;i<bodies.size();++i){const auto& j=bodies[i];Object o;o.id=j.getStr("id");o.shape=j.getStr("shape","box");
            check(!o.id.empty()&&ids.insert(o.id).second,"body ids must be nonempty and unique");
            auto mode=j.getStr("motion","dynamic");check(mode=="static"||mode=="dynamic"||mode=="kinematic","unknown motion on "+o.id);
            auto bd=b3DefaultBodyDef();bd.type=mode=="static"?b3_staticBody:mode=="dynamic"?b3_dynamicBody:b3_kinematicBody;o.dynamic=bd.type==b3_dynamicBody;
            bd.position=vec(j["position"],{0,0,0});bd.linearVelocity=vec(j["velocity"],{0,0,0},500);
            bd.angularVelocity=vec(j["angularVelocity"],{0,0,0},100);bd.linearDamping=number(j,"linearDamping",0.05f,0,100);bd.angularDamping=number(j,"angularDamping",0.05f,0,100);
            bd.enableSleep=j.getBool("sleep",true);bd.isBullet=j.getBool("bullet",false);
            auto rot=vec(j["axis"],{0,1,0},1);auto angle=number(j,"angle",0,-100,100);float len=std::sqrt(rot.x*rot.x+rot.y*rot.y+rot.z*rot.z);check(len>0.001f,"rotation axis is zero");
            bd.rotation=b3MakeQuatFromAxisAngle({rot.x/len,rot.y/len,rot.z/len},angle);
            o.color=vec(j["color"],{0.45f,0.6f,0.8f},1);
            check(o.color.x>=0&&o.color.y>=0&&o.color.z>=0,"color must be in 0..1");
            o.body=b3CreateBody(world_,&bd);check(b3Body_IsValid(o.body),"body creation failed");
            auto sd=b3DefaultShapeDef();sd.density=number(j,"density",1000,0.01f,100000);sd.baseMaterial.friction=number(j,"friction",0.5f,0,2);sd.baseMaterial.restitution=number(j,"restitution",0,0,1);
            sd.isSensor=j.getBool("sensor",false);sd.enableSensorEvents=true;sd.enableContactEvents=true;
            b3ShapeId shape{};
            if(o.shape=="box") {o.size=vec(j["size"],{1,1,1},1000);check(o.size.x>=0.02f&&o.size.y>=0.02f&&o.size.z>=0.02f,"box size must be positive");auto hull=b3MakeBoxHull(o.size.x/2,o.size.y/2,o.size.z/2);shape=b3CreateHullShape(o.body,&sd,&hull.base);}
            else if(o.shape=="sphere"){float r=number(j,"radius",0.5f,0.01f,100);o.size={r,0,0};b3Sphere sphere{{0,0,0},r};shape=b3CreateSphereShape(o.body,&sd,&sphere);}
            else if(o.shape=="capsule"){float r=number(j,"radius",0.4f,0.01f,100),h=number(j,"halfHeight",0.6f,0.01f,100);o.size={r,h,0};b3Capsule c{{0,-h,0},{0,h,0},r};shape=b3CreateCapsuleShape(o.body,&sd,&c);}
            else check(false,"unsupported JSON shape "+o.shape);
            check(b3Shape_IsValid(shape),"shape creation failed");objects_.push_back(o);
        }
        const auto& joints=scene["joints"];check(joints.isNull()||(joints.isArray()&&joints.size()<=4096),"invalid joints array");
        for(size_t i=0;i<joints.size();++i){auto& j=joints[i];auto* a=find(j.getStr("a"));auto* b=find(j.getStr("b"));check(a&&b&&a!=b,"invalid joint bodies");
            auto kind=j.getStr("kind","distance");auto aa=vec(j["anchorA"],{0,0,0}),ab=vec(j["anchorB"],{0,0,0});
            if(kind=="distance"){auto d=b3DefaultDistanceJointDef();d.base.bodyIdA=a->body;d.base.bodyIdB=b->body;d.base.localFrameA.p=aa;d.base.localFrameB.p=ab;d.length=number(j,"length",1,0.01f,1000);b3CreateDistanceJoint(world_,&d);}
            else if(kind=="hinge"){auto d=b3DefaultRevoluteJointDef();d.base.bodyIdA=a->body;d.base.bodyIdB=b->body;d.base.localFrameA.p=aa;d.base.localFrameB.p=ab;b3CreateRevoluteJoint(world_,&d);}
            else check(false,"unsupported joint "+kind);
        }
    }catch(...){release();throw;}
}
World::~World(){release();}
void World::release(){if(restored_){b3DestroyPlayer(restored_);restored_=nullptr;}else if(b3World_IsValid(world_))b3DestroyWorld(world_);world_={};}
void World::step(){b3World_Step(world_,1.0f/float(tickRate_),substeps_);++tick_;}
const Object* World::find(const std::string& id)const{for(const auto& o:objects_)if(o.id==id)return &o;return nullptr;}
bool World::impulse(const std::string& id,b3Vec3 impulse){auto* o=find(id);if(!o||!o->dynamic||!finite(impulse,1000000))return false;b3Body_ApplyLinearImpulseToCenter(o->body,impulse,true);return true;}
uint64_t World::trajectoryHash()const{Hasher h;h.u64(tick_);auto f=[&](float x){h.u32(std::bit_cast<uint32_t>(x));};for(auto& o:objects_){h.str(o.id);auto p=b3Body_GetPosition(o.body);auto q=b3Body_GetRotation(o.body);auto v=b3Body_GetLinearVelocity(o.body);auto w=b3Body_GetAngularVelocity(o.body);f(p.x);f(p.y);f(p.z);f(q.v.x);f(q.v.y);f(q.v.z);f(q.s);f(v.x);f(v.y);f(v.z);f(w.x);f(w.y);f(w.z);h.u32(b3Body_IsAwake(o.body));}return h.value();}
std::string World::save()const{
    auto* rec=b3CreateRecording(0);b3World_StartRecording(world_,rec);b3World_StopRecording(world_);
    std::string data(reinterpret_cast<const char*>(b3Recording_GetData(rec)),size_t(b3Recording_GetSize(rec)));b3DestroyRecording(rec);
    std::string bytes;Archive a(&bytes);uint32_t magic=0x50425641,version=1;auto tag=abi(),definition=scene_.dump(-1);auto tick=tick_;Hasher h;h.str(tag);h.str(definition);h.u64(tick);h.str(data);auto hash=h.value();a(magic,version,tag,definition,tick,data,hash);return bytes;
}
bool World::restore(const std::string& bytes,std::string* error){
    auto fail=[&](const char* why){if(error)*error=why;return false;};
    if(bytes.size()>64*1024*1024)return fail("physics snapshot too large");
    Archive a(bytes);uint32_t magic=0,version=0;std::string tag,definition,data;uint64_t tick=0,hash=0;a(magic,version,tag,definition,tick,data,hash);
    if(!a.ok()||a.remaining()||magic!=0x50425641||version!=1||tag!=abi()||definition!=scene_.dump(-1))return fail("incompatible physics snapshot");
    Hasher h;h.str(tag);h.str(definition);h.u64(tick);h.str(data);if(hash!=h.value()||data.empty())return fail("corrupt physics snapshot");
    auto* player=b3CreatePlayer(data.data(),int(data.size()),workers_);if(!player)return fail("Box3D rejected physics snapshot");
    if(b3RecPlayer_GetBodyCount(player)!=int(objects_.size())){b3DestroyPlayer(player);return fail("physics body manifest changed");}
    std::vector<b3BodyId> ids;for(size_t i=0;i<objects_.size();++i){auto id=b3RecPlayer_GetBodyId(player,int(i));if(!b3Body_IsValid(id)){b3DestroyPlayer(player);return fail("invalid restored body");}ids.push_back(id);}
    release();restored_=player;world_=b3RecPlayer_GetWorldId(player);tick_=tick;
    for(size_t i=0;i<objects_.size();++i)objects_[i].body=ids[i];
    if(error)error->clear();
    return true;
}
}
