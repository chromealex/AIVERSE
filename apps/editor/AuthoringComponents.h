// Opt-in authored entity presentation components. Document JSON is canonical.
// Legacy MapDoc pos owns X/Z; Transform owns Y/yaw/uniform scale. No mutable mirror.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/core/FixedMath.h"
#include "engine/render/SceneHierarchyRender.h"
#include <cmath>
#include <array>
namespace aiverse::editor {
inline bool authoredEntity(const DocEntity& e){return e.extra["components"]["Transform"].isObject();}
inline std::string authoredModelAsset(const DocEntity& e){return e.extra["components"]["StaticModel"].getStr("asset");}
// Transform JSON: {"y":n, "rotation":[x,y,z] degrees (Euler, applied Z then X then Y,
// right-handed), "scale": n | [x,y,z]}. Legacy {"yaw":deg} is the Y rotation;
// a uniform numeric "scale" applies to all axes.
inline std::array<double,3> authoredRotationDegrees(const DocEntity& e){
    const auto& t=e.extra["components"]["Transform"];
    if(t["rotation"].isArray()&&t["rotation"].size()==3)
        return {t["rotation"][size_t(0)].toDouble(),t["rotation"][size_t(1)].toDouble(),t["rotation"][size_t(2)].toDouble()};
    return {0.0,t["yaw"].toDouble(0.0),0.0};
}
inline std::array<double,3> authoredScale3(const DocEntity& e){
    const auto& s=e.extra["components"]["Transform"]["scale"];
    if(s.isArray()&&s.size()==3)return {s[size_t(0)].toDouble(1),s[size_t(1)].toDouble(1),s[size_t(2)].toDouble(1)};
    const double u=s.isNumber()?s.toDouble():1.0;return {u,u,u};
}
namespace authoring_detail {
inline Rotation axisQuat(int axis,Fixed degrees){
    const Fixed half=degrees/720_fx;Rotation q;q.w=fxCosTurns(half);
    (axis==0?q.x:axis==1?q.y:q.z)=fxSinTurns(half);return q;
}
inline Rotation mul(const Rotation& a,const Rotation& b){
    Rotation r;
    r.w=a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z;
    r.x=a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y;
    r.y=a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x;
    r.z=a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w;
    return r;
}
}
inline LocalTransform authoredLocalTransform(const DocEntity& e){
    const auto& t=e.extra["components"]["Transform"];
    if(!std::isfinite(e.x)||!std::isfinite(e.z)||std::abs(e.x)>1000000||std::abs(e.z)>1000000)throw JsonError("entity position extent");
    LocalTransform out;out.position={Fixed::fromRaw(int64_t(std::llround(double(e.x)*Fixed::kOne))),{},Fixed::fromRaw(int64_t(std::llround(double(e.z)*Fixed::kOne)))};
    for(auto key:{"y","yaw"})if(t.has(key)&&!t[key].isNumber())throw JsonError("entity Transform requires numeric fields");
    out.position.y=t.getFx("y");
    if(fxAbs(out.position.y)>1000000_fx)throw JsonError("entity Transform out of range");
    Fixed rot[3]{};
    if(t.has("rotation")){
        const auto& r=t["rotation"];
        if(!r.isArray()||r.size()!=3)throw JsonError("entity Transform rotation must be [x,y,z] degrees");
        for(size_t i=0;i<3;++i){if(!r[i].isNumber())throw JsonError("entity Transform rotation must be numeric");rot[i]=r[i].fx();}
    } else rot[1]=t.getFx("yaw");
    for(const auto& value:rot)if(fxAbs(value)>360000_fx)throw JsonError("entity Transform out of range");
    using namespace authoring_detail;
    // Unity-style order: Z, then X, then Y.
    out.rotation=mul(axisQuat(1,rot[1]),mul(axisQuat(0,rot[0]),axisQuat(2,rot[2])));
    Fixed scale[3]{1_fx,1_fx,1_fx};
    if(t.has("scale")){
        const auto& s=t["scale"];
        if(s.isNumber())scale[0]=scale[1]=scale[2]=s.fx();
        else if(s.isArray()&&s.size()==3){for(size_t i=0;i<3;++i){if(!s[i].isNumber())throw JsonError("entity Transform scale must be numeric");scale[i]=s[i].fx();}}
        else throw JsonError("entity Transform scale must be a number or [x,y,z]");
    }
    for(const auto& value:scale)if(value<Fixed::fromRatio(1,1000)||value>10000_fx)throw JsonError("entity Transform out of range");
    out.scale={scale[0],scale[1],scale[2]};
    return out;
}
inline render::mat4 authoredModelMatrix(const DocEntity& e){return render::hierarchyMatrix(hierarchy_detail::localAffine(authoredLocalTransform(e)));}
// Editable keys: y, yaw (legacy), scale (uniform), rotationX/Y/Z, scaleX/Y/Z.
inline bool authoredTransformKey(const std::string& key){
    return key=="y"||key=="yaw"||key=="scale"||key=="rotationX"||key=="rotationY"||key=="rotationZ"||
           key=="scaleX"||key=="scaleY"||key=="scaleZ";
}
inline DocEntity editAuthoredPlacement(const DocEntity& source,const std::string& key,const Json& value){
    if(!authoredEntity(source)||!value.isNumber()||!authoredTransformKey(key))throw JsonError("invalid authored Transform property");
    auto result=source;auto components=result.extra["components"];auto transform=components["Transform"];
    if(key=="y")transform.set("y",value);
    else if(key=="yaw"||key=="rotationX"||key=="rotationY"||key=="rotationZ"){
        auto rot=authoredRotationDegrees(source);
        const int axis=key=="rotationX"?0:key=="rotationZ"?2:1;rot[size_t(axis)]=value.toDouble();
        if(rot[0]==0&&rot[2]==0){transform.erase("rotation");transform.set("yaw",Json::number(rot[1],3));}
        else{transform.erase("yaw");Json r=Json::array();for(double v:rot)r.push(Json::number(v,3));transform.set("rotation",r);}
    } else {
        auto sc=authoredScale3(source);
        if(key=="scale")sc={value.toDouble(),value.toDouble(),value.toDouble()};
        else sc[size_t(key=="scaleX"?0:key=="scaleY"?1:2)]=value.toDouble();
        if(sc[0]==sc[1]&&sc[1]==sc[2])transform.set("scale",Json::number(sc[0],4));
        else{Json s=Json::array();for(double v:sc)s.push(Json::number(v,4));transform.set("scale",s);}
    }
    components.set("Transform",transform);result.extra.set("components",components);
    if(result.extra["components"].has("NereidWater")){
        const auto r=authoredRotationDegrees(result),s=authoredScale3(result);
        if(r[0]!=0||r[1]!=0||r[2]!=0||s[0]!=1||s[1]!=1||s[2]!=1)throw JsonError("NEREID entity supports translation only");
    }
    (void)authoredLocalTransform(result);return result;
}
} // namespace aiverse::editor
