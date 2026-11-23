// AIVERSE SpectraFX: bounded, allocation-free per-particle expression VM.
// A program is compiled once per emitter from declarative JSON. It executes
// only built-in math on visual presentation data, never gameplay ECS state.
// No strings, JSON, heap allocation, function pointers or IO on particle steps.
#pragma once

#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::spectra::program {

enum class Stage : uint8_t { Spawn, Update, Death, Collision, Trigger, Count };
enum class Action : uint8_t { Set, Add, AddRate, Multiply };
enum class Op : uint8_t {
    Constant, Read,
    Add, Subtract, Multiply, Divide, Min, Max, Pow,
    Sin, Cos, Abs, Negate, Sqrt, Floor, Ceil, Saturate,
    Clamp, Lerp, Select,
    Greater, Less, Equal
};
enum class Field : uint8_t {
    Age, AgeNormalized, Dt, Uid,
    PositionX, PositionY, PositionZ,
    VelocityX, VelocityY, VelocityZ,
    Size, Rotation,
    TintR, TintG, TintB, TintA,
    Attribute0, Attribute1, Attribute2, Attribute3,
    Attribute4, Attribute5, Attribute6, Attribute7,
    Param0, Param1, Param2, Param3, Param4, Param5, Param6, Param7,
    BirthRandom, Invalid
};
struct Instruction {
    Op op=Op::Constant;
    Field field=Field::Invalid;
    float constant=0;
};
struct Statement {
    Action action=Action::Set;
    Field field=Field::Invalid;
    std::vector<Instruction> code;
};
struct Program {
    std::array<std::vector<Statement>,size_t(Stage::Count)> stages;
    bool enabled(Stage stage) const {return !stages[size_t(stage)].empty();}
};
struct Context {
    render::float3& position;
    render::float3& velocity;
    render::float4& tint;
    float& size;
    float& rotation;
    std::array<float,8>& attributes;
    float age=0, ageNormalized=0, dt=0, uid=0;
    // Optional per-instance uniforms, snapshotted onto each particle at
    // spawn. Null means all zeros (older runtime/context clients are safe).
    const std::array<float,8>* instanceParams=nullptr;
};

inline bool number(float x) {return std::isfinite(x) && std::abs(x)<=1.0e7f;}
inline Field fieldOf(std::string_view name) {
    struct Entry {const char* name;Field field;};
    static constexpr Entry entries[]{
        {"age",Field::Age},{"ageNormalized",Field::AgeNormalized},
        {"dt",Field::Dt},{"uid",Field::Uid},{"birthRandom",Field::BirthRandom},
        {"position.x",Field::PositionX},{"position.y",Field::PositionY},{"position.z",Field::PositionZ},
        {"velocity.x",Field::VelocityX},{"velocity.y",Field::VelocityY},{"velocity.z",Field::VelocityZ},
        {"size",Field::Size},{"rotation",Field::Rotation},
        {"tint.r",Field::TintR},{"tint.g",Field::TintG},
        {"tint.b",Field::TintB},{"tint.a",Field::TintA},
        {"attribute0",Field::Attribute0},{"attribute1",Field::Attribute1},
        {"attribute2",Field::Attribute2},{"attribute3",Field::Attribute3},
        {"attribute4",Field::Attribute4},{"attribute5",Field::Attribute5},
        {"attribute6",Field::Attribute6},{"attribute7",Field::Attribute7},
        {"param0",Field::Param0},{"param1",Field::Param1},
        {"param2",Field::Param2},{"param3",Field::Param3},
        {"param4",Field::Param4},{"param5",Field::Param5},
        {"param6",Field::Param6},{"param7",Field::Param7}
    };
    for(const auto& e:entries)if(name==e.name)return e.field;
    return Field::Invalid;
}
inline bool writable(Field f) {
    return f>=Field::PositionX && f<=Field::Attribute7; // params read-only
}
inline float read(Field f,const Context& c) {
    switch(f) {
    case Field::Age:return c.age;
    case Field::AgeNormalized:return c.ageNormalized;
    case Field::Dt:return c.dt;
    case Field::Uid:return c.uid;
    case Field::BirthRandom:{
        const double uid=std::isfinite(c.uid)?std::clamp(double(c.uid),0.0,4294967295.0):0.0;
        uint32_t x=uint32_t(uid)^0x85ebca6bu;
        x^=x>>16u;x*=0x7feb352du;x^=x>>15u;x*=0x846ca68bu;x^=x>>16u;
        return float(x>>8u)*(1.f/16777216.f);
    }
    case Field::PositionX:return c.position.x;
    case Field::PositionY:return c.position.y;
    case Field::PositionZ:return c.position.z;
    case Field::VelocityX:return c.velocity.x;
    case Field::VelocityY:return c.velocity.y;
    case Field::VelocityZ:return c.velocity.z;
    case Field::Size:return c.size;
    case Field::Rotation:return c.rotation;
    case Field::TintR:return c.tint.x;
    case Field::TintG:return c.tint.y;
    case Field::TintB:return c.tint.z;
    case Field::TintA:return c.tint.w;
    default:
        if(f>=Field::Attribute0 && f<=Field::Attribute7)
            return c.attributes[size_t(f)-size_t(Field::Attribute0)];
        if(f>=Field::Param0 && f<=Field::Param7 && c.instanceParams)
            return (*c.instanceParams)[size_t(f)-size_t(Field::Param0)];
        return 0;
    }
}
inline void write(Field f,Context& c,float v) {
    if(!number(v)||!writable(f))return;
    switch(f) {
    case Field::PositionX:c.position.x=v;break;
    case Field::PositionY:c.position.y=v;break;
    case Field::PositionZ:c.position.z=v;break;
    case Field::VelocityX:c.velocity.x=v;break;
    case Field::VelocityY:c.velocity.y=v;break;
    case Field::VelocityZ:c.velocity.z=v;break;
    case Field::Size:c.size=std::max(0.f,v);break;
    case Field::Rotation:c.rotation=v;break;
    case Field::TintR:c.tint.x=std::clamp(v,0.f,1.f);break;
    case Field::TintG:c.tint.y=std::clamp(v,0.f,1.f);break;
    case Field::TintB:c.tint.z=std::clamp(v,0.f,1.f);break;
    case Field::TintA:c.tint.w=std::clamp(v,0.f,1.f);break;
    default:
        if(f>=Field::Attribute0 && f<=Field::Attribute7)
            c.attributes[size_t(f)-size_t(Field::Attribute0)]=v;
        break;
    }
}
inline bool fail(std::vector<std::string>& errors,const std::string& what) {
    errors.push_back("particleProgram: "+what);
    return false;
}
inline bool compileExpression(const Json& node,std::vector<Instruction>& code,
                              std::vector<std::string>& errors,unsigned depth=0) {
    if(depth>=16 || code.size()>=128)
        return fail(errors,"expression budget exceeded (depth 16, 128 instructions)");
    if(node.isNumber()) {
        const float value=float(node.toDouble());
        if(!number(value))return fail(errors,"nonfinite/out-of-range scalar");
        code.push_back({Op::Constant,Field::Invalid,value});
        return true;
    }
    if(!node.isObject())return fail(errors,"expression must be number or typed operation object");
    if(node.has("var")) {
        if(node.size()!=1 || !node["var"].isString())
            return fail(errors,"var must be one field-name string");
        const Field f=fieldOf(node["var"].str());
        if(f==Field::Invalid)return fail(errors,"unknown field '"+node["var"].str()+"'");
        code.push_back({Op::Read,f,0});
        return true;
    }
    const std::string name=node.getStr("op");
    const bool unary=name=="sin"||name=="cos"||name=="abs"||
        name=="negate"||name=="sqrt"||name=="floor"||
        name=="ceil"||name=="saturate";
    const bool ternary=name=="clamp"||name=="lerp"||name=="select";
    const bool binary=name=="add"||name=="subtract"||name=="multiply"||
        name=="divide"||name=="min"||name=="max"||name=="pow"||
        name=="greater"||name=="less"||name=="equal";
    if(!unary && !ternary && !binary)
        return fail(errors,"unknown expression op '"+name+"'");
    if(!node.has("a") || (!unary&&!node.has("b")) ||
       (ternary&&!node.has("t")))
        return fail(errors,"expression op '"+name+"' missing inputs a/b/t");
    for(const auto& [key,unused]:node.members()) {
        (void)unused;
        if(key!="op"&&key!="a"&&key!="b"&&key!="t")
            return fail(errors,"unexpected expression key '"+key+"'");
    }
    if(!compileExpression(node["a"],code,errors,depth+1))return false;
    if(!unary && !compileExpression(node["b"],code,errors,depth+1))return false;
    if(ternary && !compileExpression(node["t"],code,errors,depth+1))return false;
    Op op=Op::Constant;
    if(name=="add")op=Op::Add;
    else if(name=="subtract")op=Op::Subtract;
    else if(name=="multiply")op=Op::Multiply;
    else if(name=="divide")op=Op::Divide;
    else if(name=="min")op=Op::Min;
    else if(name=="max")op=Op::Max;
    else if(name=="pow")op=Op::Pow;
    else if(name=="sin")op=Op::Sin;
    else if(name=="cos")op=Op::Cos;
    else if(name=="abs")op=Op::Abs;
    else if(name=="negate")op=Op::Negate;
    else if(name=="sqrt")op=Op::Sqrt;
    else if(name=="floor")op=Op::Floor;
    else if(name=="ceil")op=Op::Ceil;
    else if(name=="saturate")op=Op::Saturate;
    else if(name=="clamp")op=Op::Clamp;
    else if(name=="lerp")op=Op::Lerp;
    else if(name=="select")op=Op::Select;
    else if(name=="greater")op=Op::Greater;
    else if(name=="less")op=Op::Less;
    else if(name=="equal")op=Op::Equal;
    if(code.size()>=128)return fail(errors,"expression instruction limit 128 exceeded");
    code.push_back({op,Field::Invalid,0});
    return true;
}
inline bool compile(const Json& json,Program& out,std::vector<std::string>& errors) {
    out={};
    const size_t before=errors.size();
    if(!json.isObject()||json.getInt("version")!=1)
        return fail(errors,"expected {version:1,spawn:[...],update:[...]}");
    for(const auto& [key,stage]:json.members()) {
        if(key=="version")continue;
        size_t stageIdx=size_t(Stage::Count);
        if(key=="spawn")stageIdx=size_t(Stage::Spawn);
        else if(key=="update")stageIdx=size_t(Stage::Update);
        else if(key=="death")stageIdx=size_t(Stage::Death);
        else if(key=="collision")stageIdx=size_t(Stage::Collision);
        else if(key=="trigger")stageIdx=size_t(Stage::Trigger);
        if(stageIdx==size_t(Stage::Count))return fail(errors,"unknown stage '"+key+"'");
        if(!stage.isArray()||stage.size()>32)
            return fail(errors,key+" needs 0..32 statements");
        size_t instructionCount=0;
        for(const Json& statement:stage.items()) {
            if(!statement.isObject() || !statement["target"].isString() ||
               !statement.has("value"))
                return fail(errors,key+" statement requires target and value");
            const Field field=fieldOf(statement.getStr("target"));
            if(!writable(field))
                return fail(errors,"cannot write "+statement.getStr("target"));
            Action action=Action::Set;
            const std::string act=statement.getStr("op","set");
            if(act=="add")action=Action::Add;
            else if(act=="addRate")action=Action::AddRate;
            else if(act=="multiply")action=Action::Multiply;
            else if(act!="set")return fail(errors,"unknown statement op "+act);
            for(const auto& [k,unused]:statement.members()) {
                (void)unused;
                if(k!="target"&&k!="value"&&k!="op")
                    return fail(errors,"unknown statement key '"+k+"'");
            }
            Statement compiled;
            compiled.field=field;
            compiled.action=action;
            if(!compileExpression(statement["value"],compiled.code,errors))return false;
            instructionCount+=compiled.code.size();
            if(instructionCount>256)
                return fail(errors,"stage instruction budget 256 exceeded");
            out.stages[stageIdx].push_back(std::move(compiled));
        }
    }
    return errors.size()==before;
}
inline bool eval(const Statement& statement,const Context& context,float& result) {
    std::array<float,128> stack{};
    size_t used=0;
    for(const auto& ins:statement.code) {
        if(ins.op==Op::Constant||ins.op==Op::Read) {
            if(used>=stack.size())return false;
            stack[used++]=ins.op==Op::Constant?ins.constant:read(ins.field,context);
            continue;
        }
        const bool unary=ins.op==Op::Sin||ins.op==Op::Cos||ins.op==Op::Abs||
             ins.op==Op::Negate||ins.op==Op::Sqrt||ins.op==Op::Floor||
             ins.op==Op::Ceil||ins.op==Op::Saturate;
        const bool ternary=ins.op==Op::Clamp||ins.op==Op::Lerp||ins.op==Op::Select;
        const size_t needed=unary?1:ternary?3:2;
        if(used<needed)return false;
        float t=0,b=0,a=0;
        if(ternary)t=stack[--used];
        if(!unary)b=stack[--used];
        a=stack[--used];
        float value=0;
        switch(ins.op) {
        case Op::Add:value=a+b;break;
        case Op::Subtract:value=a-b;break;
        case Op::Multiply:value=a*b;break;
        case Op::Divide:
            if(std::abs(b)<1.0e-10f)return false;
            value=a/b;break;
        case Op::Min:value=std::min(a,b);break;
        case Op::Max:value=std::max(a,b);break;
        case Op::Pow:value=std::pow(a,b);break;
        case Op::Sin:value=std::sin(a);break;
        case Op::Cos:value=std::cos(a);break;
        case Op::Abs:value=std::abs(a);break;
        case Op::Negate:value=-a;break;
        case Op::Sqrt:
            if(a<0)return false;
            value=std::sqrt(a);break;
        case Op::Floor:value=std::floor(a);break;
        case Op::Ceil:value=std::ceil(a);break;
        case Op::Saturate:value=std::clamp(a,0.f,1.f);break;
        case Op::Clamp:
            if(b>t)return false;
            value=std::clamp(a,b,t);break;
        case Op::Lerp:value=a+(b-a)*std::clamp(t,0.f,1.f);break;
        case Op::Select:value=a!=0?b:t;break;
        case Op::Greater:value=a>b?1.f:0.f;break;
        case Op::Less:value=a<b?1.f:0.f;break;
        case Op::Equal:value=a==b?1.f:0.f;break;
        default:return false;
        }
        if(!number(value))return false;
        stack[used++]=value;
    }
    if(used!=1||!number(stack[0]))return false;
    result=stack[0];
    return true;
}
inline bool execute(const Program& program,Stage stage,Context& context) {
    bool allOk=true;
    for(const auto& statement:program.stages[size_t(stage)]) {
        float value=0;
        if(!eval(statement,context,value)) {allOk=false;continue;}
        const float before=read(statement.field,context);
        float next=value;
        switch(statement.action) {
        case Action::Set:break;
        case Action::Add:next=before+value;break;
        case Action::AddRate:next=before+value*context.dt;break;
        case Action::Multiply:next=before*value;break;
        }
        if(!number(next)) {allOk=false;continue;}
        write(statement.field,context,next);
    }
    return allOk;
}

} // namespace aiverse::spectra::program
