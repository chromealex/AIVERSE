// Restricted, deterministic pointwise expression language. No raw source,
// arbitrary memory access, loops, recursion, resource bindings or host I/O.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace aiverse::render::texture_graph::custom_compute {
inline constexpr size_t maxInstructions=48,maxCodeBytes=1024,maxWork=64u*1024u*1024u;
enum class Code:uint8_t {Literal,Variable,Add,Sub,Mul,Div,Min,Max,Abs,Neg,Invert,Sat,Sqrt,Fract,Sin,Cos,Step,Mix};
struct Instruction {Code code;float value=0;unsigned index=0;};
using Program=std::vector<Instruction>;
using Values=std::array<float,13>;
inline constexpr std::array<std::string_view,13> variableNames={"ar","ag","ab","br","bg","bb","c","u","v","p0","p1","p2","p3"};
inline bool numeric(std::string_view token,float& result){
    if(token.empty()||token.size()>16)return false;
    size_t i=0;bool negative=false,digit=false,dot=false;double value=0,scale=1;
    if(token[i]=='-'||token[i]=='+'){negative=token[i]=='-';++i;}
    for(;i<token.size();++i){
        const char c=token[i];if(c=='.'&&!dot){dot=true;continue;}
        if(c<'0'||c>'9')return false;
        digit=true;
        if(dot){scale*=.1;value+=(c-'0')*scale;}else value=value*10+(c-'0');
        if(value>16)return false;
    }
    if(!digit)return false;
    result=float(negative?-value:value);return true;
}
inline Program parse(const std::string& source,bool color){
    if(source.empty()||source.size()>maxCodeBytes)throw std::invalid_argument("custom compute code length");
    std::istringstream stream(source);std::string token;Program out;int depth=0;
    struct Op{std::string_view name;Code code;int arity;};
    constexpr Op ops[]={{"+",Code::Add,2},{"-",Code::Sub,2},{"*",Code::Mul,2},{"/",Code::Div,2},{"min",Code::Min,2},{"max",Code::Max,2},{"abs",Code::Abs,1},{"neg",Code::Neg,1},{"invert",Code::Invert,1},{"sat",Code::Sat,1},{"sqrt",Code::Sqrt,1},{"fract",Code::Fract,1},{"sin",Code::Sin,1},{"cos",Code::Cos,1},{"step",Code::Step,2},{"mix",Code::Mix,3}};
    while(stream>>token){
        if(out.size()>=maxInstructions)throw std::invalid_argument("custom compute instruction limit");
        if(token=="a"||token=="b"){
            if(color)throw std::invalid_argument("RGB compute requires explicit ar/ag/ab or br/bg/bb channel");
            out.push_back({Code::Variable,0,token=="a"?0u:3u});++depth;continue;
        }
        auto variable=std::find(variableNames.begin(),variableNames.end(),token);
        if(variable!=variableNames.end()){
            const auto index=unsigned(variable-variableNames.begin());
            if(!color&&index<6)throw std::invalid_argument("scalar compute uses a and b, not RGB channels");
            out.push_back({Code::Variable,0,index});++depth;continue;
        }
        float value=0;
        if(numeric(token,value)){out.push_back({Code::Literal,value});++depth;continue;}
        const auto it=std::find_if(std::begin(ops),std::end(ops),[&](const Op& o){return o.name==token;});
        if(it==std::end(ops))throw std::invalid_argument("unknown custom compute instruction");
        if(depth<it->arity)throw std::invalid_argument("custom compute stack underflow");
        depth+=1-it->arity;out.push_back({it->code});
    }
    if(depth!=1)throw std::invalid_argument("custom compute must return exactly one scalar");
    return out;
}
inline float limit(float x){return std::clamp(x,-16.f,16.f);}
inline float evaluate(const Program& program,const Values& values){
    std::array<float,maxInstructions> stack{};size_t count=0;
    for(const auto& op:program){
        if(op.code==Code::Literal){stack[count++]=op.value;continue;}
        if(op.code==Code::Variable){stack[count++]=values[op.index];continue;}
        if(op.code==Code::Mix){
            const float t=stack[--count],b=stack[--count],a=stack[count-1];
            stack[count-1]=limit(a+(b-a)*t);continue;
        }
        if(op.code==Code::Add||op.code==Code::Sub||op.code==Code::Mul||op.code==Code::Div||op.code==Code::Min||op.code==Code::Max||op.code==Code::Step){
            const float b=stack[--count],a=stack[count-1];float x=0;
            switch(op.code){
                case Code::Add:x=a+b;break;case Code::Sub:x=a-b;break;case Code::Mul:x=a*b;break;
                case Code::Div:x=a/(b<0.f?-std::max(-b,.001f):std::max(b,.001f));break;
                case Code::Min:x=std::min(a,b);break;case Code::Max:x=std::max(a,b);break;
                case Code::Step:x=b<a?0.f:1.f;break;default:break;
            }
            stack[count-1]=limit(x);continue;
        }
        auto& a=stack[count-1];float x=0;
        switch(op.code){
            case Code::Abs:x=std::abs(a);break;case Code::Neg:x=-a;break;case Code::Invert:x=1.f-a;break;
            case Code::Sat:x=std::clamp(a,0.f,1.f);break;case Code::Sqrt:x=std::sqrt(std::max(0.f,a));break;
            case Code::Fract:x=a-std::floor(a);break;case Code::Sin:x=std::sin(a);break;case Code::Cos:x=std::cos(a);break;default:break;
        }
        a=limit(x);
    }
    return stack[0];
}
// All emitted syntax belongs to this compiler. The user token text is never
// copied into a shader. Helpers evaluate each operand once.
inline const char* shaderHelpers=R"(
float tgCustomDiv(float a,float b){return a/(b<0.0f?-max(-b,0.001f):max(b,0.001f));}
float tgCustomMix(float a,float b,float t){return a+(b-a)*t;}
float tgCustomSqrt(float a){return sqrt(max(a,0.0f));}
)";
template<class Variable,class Literal>
inline std::string emit(const Program& program,Variable variable,Literal literal){
    std::vector<std::string> stack;stack.reserve(maxInstructions);
    auto pop=[&](){auto s=std::move(stack.back());stack.pop_back();return s;};
    for(const auto& op:program){
        if(op.code==Code::Literal){stack.push_back("("+literal(op.value)+")");continue;}
        if(op.code==Code::Variable){stack.push_back(variable(op.index));continue;}
        std::string expr;
        if(op.code==Code::Mix){auto t=pop(),b=pop(),a=pop();expr="tgCustomMix("+a+","+b+","+t+")";}
        else if(op.code==Code::Add||op.code==Code::Sub||op.code==Code::Mul||op.code==Code::Div||op.code==Code::Min||op.code==Code::Max||op.code==Code::Step){
            auto b=pop(),a=pop();
            switch(op.code){
                case Code::Add:expr="("+a+"+"+b+")";break;case Code::Sub:expr="("+a+"-"+b+")";break;
                case Code::Mul:expr="("+a+"*"+b+")";break;case Code::Div:expr="tgCustomDiv("+a+","+b+")";break;
                case Code::Min:expr="min("+a+","+b+")";break;case Code::Max:expr="max("+a+","+b+")";break;
                case Code::Step:expr="step("+a+","+b+")";break;default:break;
            }
        }else{
            auto a=pop();
            switch(op.code){
                case Code::Abs:expr="abs("+a+")";break;case Code::Neg:expr="(-"+a+")";break;
                case Code::Invert:expr="(1.0f-"+a+")";break;case Code::Sat:expr="clamp("+a+",0.0f,1.0f)";break;
                case Code::Sqrt:expr="tgCustomSqrt("+a+")";break;case Code::Fract:expr="fract("+a+")";break;
                case Code::Sin:expr="sin("+a+")";break;case Code::Cos:expr="cos("+a+")";break;default:break;
            }
        }
        stack.push_back("clamp("+expr+",-16.0f,16.0f)");
    }
    return stack.back();
}
} // namespace aiverse::render::texture_graph::custom_compute
