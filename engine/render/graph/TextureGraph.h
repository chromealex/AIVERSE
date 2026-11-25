// Bounded, CPU-only procedural texture DAG. No GPU or filesystem dependencies.
#pragma once
#include "engine/render/TextureGen.h"
#include "TextureGraphLiveness.h"
#include "TextureGraphCustomCompute.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>
#include <sstream>
#include <memory>

namespace aiverse::render::texture_graph {
enum class Kind { Scalar, Color, Normal, Packed, Radiance };
using Pixel=std::array<float,4>; // fourth lane is meaningful only for linear radiance
inline const char* kindName(Kind k){switch(k){case Kind::Scalar:return "scalar";case Kind::Color:return "color";case Kind::Normal:return "normal";case Kind::Radiance:return "radiance";default:return "packed";}}
struct Op {const char* id;Kind output;std::vector<Kind> inputs;};
inline const std::vector<Op>& catalog(){
    static const std::vector<Op> ops={
        {"image",Kind::Color,{}},{"imageAsset",Kind::Color,{}},{"imageAssetChannel",Kind::Scalar,{}},{"hdrAsset",Kind::Radiance,{}},{"hdrConstant",Kind::Radiance,{}},{"hdrFromColor",Kind::Radiance,{Kind::Color}},{"hdrAdd",Kind::Radiance,{Kind::Radiance,Kind::Radiance}},{"hdrMultiply",Kind::Radiance,{Kind::Radiance,Kind::Radiance}},{"hdrExposure",Kind::Radiance,{Kind::Radiance}},{"hdrBlend",Kind::Radiance,{Kind::Radiance,Kind::Radiance,Kind::Scalar}},{"hdrToneMap",Kind::Color,{Kind::Radiance}},{"hdrAlpha",Kind::Scalar,{Kind::Radiance}},{"hdrSetAlpha",Kind::Radiance,{Kind::Radiance,Kind::Scalar}},{"bufferScalar",Kind::Scalar,{}},{"bufferColor",Kind::Color,{}},{"formula",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"customScalar",Kind::Scalar,{Kind::Scalar,Kind::Scalar,Kind::Scalar}},{"customColor",Kind::Color,{Kind::Color,Kind::Color,Kind::Scalar}},{"constant",Kind::Scalar,{}},{"color",Kind::Color,{}},{"noise",Kind::Scalar,{}},{"fbm",Kind::Scalar,{}},{"voronoi",Kind::Scalar,{}},{"gradient",Kind::Scalar,{}},{"pattern",Kind::Color,{}},{"patternMask",Kind::Scalar,{}},
        {"add",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"multiply",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"subtract",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"min",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"max",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"invert",Kind::Scalar,{Kind::Scalar}},{"threshold",Kind::Scalar,{Kind::Scalar}},{"remap",Kind::Scalar,{Kind::Scalar}},{"blend",Kind::Scalar,{Kind::Scalar,Kind::Scalar,Kind::Scalar}},{"colorize",Kind::Color,{Kind::Scalar,Kind::Color,Kind::Color}},{"blendColor",Kind::Color,{Kind::Color,Kind::Color,Kind::Scalar}},{"blur",Kind::Scalar,{Kind::Scalar}},{"erosion",Kind::Scalar,{Kind::Scalar}},{"warp",Kind::Scalar,{Kind::Scalar,Kind::Scalar,Kind::Scalar}},{"distort",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"normal",Kind::Normal,{Kind::Scalar}},{"metallicRoughness",Kind::Packed,{Kind::Scalar,Kind::Scalar}}
    };return ops;
}
inline void require(bool condition,const char* error){if(!condition)throw std::invalid_argument(error);}
inline float scalar(const Json& j,float fallback,float lo=0,float hi=1){
    if(j.isNull())return fallback;
    require(j.isNumber()&&std::isfinite(j.toDouble())&&j.toDouble()>=lo&&j.toDouble()<=hi,"texture graph numeric parameter out of range");
    return float(j.toDouble());
}
inline int integer(const Json& j,int fallback,int lo,int hi){
    if(j.isNull())return fallback;
    require(j.isNumber()&&j.toDouble()==double(j.integer())&&j.integer()>=lo&&j.integer()<=hi,"texture graph integer parameter out of range");return int(j.integer());
}
inline uint32_t seed(const Json& j){if(j.isNull())return 1;require(j.isNumber()&&j.toDouble()==double(j.integer())&&j.integer()>=0&&j.integer()<=4294967295LL,"invalid texture seed");return uint32_t(j.integer());}
inline bool safeId(const std::string& id){return !id.empty()&&id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
inline const Op& op(const std::string& id){for(const auto& o:catalog())if(id==o.id)return o;throw std::invalid_argument("unknown texture graph operation: "+id);}
inline Kind outputKind(const std::string& name){
    if(name=="color"||name=="emissive")return Kind::Color;
    if(name=="radiance")return Kind::Radiance;
    if(name=="normal")return Kind::Normal;
    if(name=="metallicRoughness")return Kind::Packed;
    if(name=="height"||name=="roughness"||name=="metallic"||name=="ao"||name=="mask")return Kind::Scalar;
    throw std::invalid_argument("unknown texture graph output: "+name);
}
inline constexpr size_t maxImageSourceBytes=16u*1024u*1024u;
// Packed words are 0xAARRGGBB when hasAlpha is true; legacy RGB resolvers
// leave hasAlpha false and are treated as opaque. Storage remains 4 bytes/pixel.
struct ImageMip {int width=0,height=0;size_t offset=0;};
struct ImageSource {
    int width=0,height=0;std::vector<uint32_t> rgb;bool hasAlpha=false;
    // Alternative linear RGBA float snapshot (four floats per texel).
    // Exactly one representation may be populated; HDR alpha is linear.
    std::vector<float> rgba32f;
    // Derived levels are contiguous after level zero, offsets in texels.
    std::vector<ImageMip> mips;
    bool authoredMips=false; // Standard floor-halved container chain, not derived box levels.
    bool hdr() const{return !rgba32f.empty();}
    size_t bytes() const{return hdr()?rgba32f.size()*sizeof(float):rgb.size()*sizeof(uint32_t);}
    const void* data() const{return hdr()?static_cast<const void*>(rgba32f.data()):static_cast<const void*>(rgb.data());}
};
static_assert(sizeof(float)==4,"texture graph float buffer ABI requires 32-bit floats");
using RasterPixel=std::array<float,4>;
inline RasterPixel rasterTexel(const ImageSource& s,size_t i){
    if(s.hdr()){const auto* v=s.rgba32f.data()+i*4;return {v[0],v[1],v[2],v[3]};}
    const uint32_t p=s.rgb[i];
    return {float((p>>16)&255u)/255.f,float((p>>8)&255u)/255.f,float(p&255u)/255.f,s.hasAlpha?float(p>>24)/255.f:1.f};
}
inline RasterPixel rasterMix(const RasterPixel& a,const RasterPixel& b,float t){
    RasterPixel r{};for(size_t c=0;c<4;++c)r[c]=a[c]+(b[c]-a[c])*t;return r;
}
inline void buildImageMips(ImageSource& s,size_t budget){
    const size_t stride=s.hdr()?16u:4u;
    size_t texels=size_t(s.width)*s.height;
    int w=s.width,h=s.height;
    s.mips={{w,h,0}};
    while(w>1||h>1){
        w=(w+1)/2;h=(h+1)/2;
        const size_t count=size_t(w)*h;
        require(texels<=budget/stride&&count<=budget/stride-texels,"image mip chain exceeds source byte budget");
        s.mips.push_back({w,h,texels});texels+=count;
    }
    if(s.hdr())s.rgba32f.reserve(texels*4);else s.rgb.reserve(texels);
    for(size_t level=1;level<s.mips.size();++level){
        const auto a=s.mips[level-1],b=s.mips[level];
        for(int y=0;y<b.height;++y)for(int x=0;x<b.width;++x){
            RasterPixel sum{};unsigned count=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                const int xx=x*2+dx,yy=y*2+dy;if(xx>=a.width||yy>=a.height)continue;
                const auto v=rasterTexel(s,a.offset+size_t(yy)*a.width+xx);
                for(size_t c=0;c<4;++c)sum[c]+=v[c];
                ++count;
            }
            for(auto& v:sum)v/=float(count);
            if(s.hdr())s.rgba32f.insert(s.rgba32f.end(),sum.begin(),sum.end());
            else{
                auto byte=[](float v){return uint32_t(std::lround(std::clamp(v,0.f,1.f)*255.f));};
                s.rgb.push_back((byte(sum[3])<<24)|(byte(sum[0])<<16)|(byte(sum[1])<<8)|byte(sum[2]));
            }
        }
    }
}
struct ImageSampling {ImageMip a,b;float blend=0;bool linear=false;};
inline ImageSampling imageSampling(const ImageSource& s,const Json& p,int n){
    ImageSampling r;r.a=r.b={s.width,s.height,0};
    const auto filter=p.getStr("filter","nearest"),mode=p.getStr("mipMode","base");
    r.linear=filter!="nearest";
    if(mode=="base")return r;
    require(!s.mips.empty(),"missing image mip chain");
    const float maxLevel=float(s.mips.size()-1);
    const float lod=std::clamp(mode=="auto"?std::log2(float(std::max(s.width,s.height))/float(n))+scalar(p["mipBias"],0,-8,8):scalar(p["mipLevel"],0,0,12),0.f,maxLevel);
    const size_t a=filter=="trilinear"?size_t(std::floor(lod)):size_t(std::floor(lod+.5f));
    const size_t b=filter=="trilinear"?std::min(a+1,s.mips.size()-1):a;
    r.a=s.mips[a];r.b=s.mips[b];r.blend=b==a?0.f:lod-float(a);
    return r;
}
inline RasterPixel sampleRasterLevel(const ImageSource& s,const ImageMip& m,int x,int y,int n,bool linear){
    auto texel=[&](int xx,int yy){return rasterTexel(s,m.offset+size_t(std::clamp(yy,0,m.height-1))*m.width+std::clamp(xx,0,m.width-1));};
    if(!linear)return texel(x*m.width/n,y*m.height/n);
    const float fx=(float(x)+.5f)*float(m.width)/float(n)-.5f,fy=(float(y)+.5f)*float(m.height)/float(n)-.5f;
    const int ix=int(std::floor(fx)),iy=int(std::floor(fy));
    return rasterMix(rasterMix(texel(ix,iy),texel(ix+1,iy),fx-ix),rasterMix(texel(ix,iy+1),texel(ix+1,iy+1),fx-ix),fy-iy);
}
inline RasterPixel sampleRaster(const ImageSource& s,const ImageSampling& p,int x,int y,int n){
    const auto a=sampleRasterLevel(s,p.a,x,y,n,p.linear);
    return p.blend>0?rasterMix(a,sampleRasterLevel(s,p.b,x,y,n,p.linear),p.blend):a;
}
inline float mapRadiance(float v,float gain,bool reinhard){
    v=std::max(v*gain,0.f);
    return reinhard?v/(1.f+v):std::min(v,1.f);
}
// Trusted host must verify the exact pinned revision, return identical decoded
// pixels, alpha availability and extent within a project, and obey byte budget.
using ImageResolver=std::function<ImageSource(const std::string&,const std::string&,size_t)>;
struct Node {std::string id;const Op* info=nullptr;std::vector<size_t> inputs;Json parameters;std::shared_ptr<const ImageSource> image;std::array<custom_compute::Program,3> custom;};
inline bool isCustomCompute(std::string_view id){return id=="customScalar"||id=="customColor";}
inline bool readsCustomResources(const Node& n){
    return std::any_of(n.custom.begin(),n.custom.end(),[](const auto& program){return custom_compute::readsResources(program);});
}
inline bool isExternalImageSource(std::string_view id){return id=="imageAsset"||id=="imageAssetChannel"||id=="hdrAsset";}
inline bool isBufferSource(std::string_view id){return id=="bufferScalar"||id=="bufferColor";}
inline bool isImageSource(std::string_view id){return id=="image"||isExternalImageSource(id)||isBufferSource(id);}
inline ImageSource bufferSource(const std::string& id,const Json& q,size_t budget){
    const auto values=q.has("values")?q["values"]:Json::parse(id=="bufferScalar"?"[0,0.5,1]":"[[0,0,0],[0.5,0.5,0.5],[1,1,1]]");
    require(values.isArray()&&values.size()>=1&&values.size()<=4096&&values.size()<=budget/16,"invalid buffer extent or source budget");
    ImageSource s;s.width=int(values.size());s.height=1;s.hasAlpha=true;s.rgba32f.reserve(values.size()*4);
    auto channel=[](const Json& value){require(value.isNumber(),"buffer channel must be numeric");return scalar(value,0);};
    for(const auto& v:values.items()){
        if(id=="bufferScalar"){const float x=channel(v);s.rgba32f.insert(s.rgba32f.end(),{x,x,x,1.f});}
        else{
            require(v.isArray()&&v.size()==3,"bufferColor requires RGB triples");
            s.rgba32f.insert(s.rgba32f.end(),{channel(v[0]),channel(v[1]),channel(v[2]),1.f});
        }
    }
    return s;
}
inline custom_compute::Bindings customBindings(const Node& node){
    custom_compute::Bindings out;const auto& q=node.parameters;
    const auto& resources=q["resources"];
    require(resources.isNull()||(resources.isObject()&&resources.size()<=3),"custom resources require at most three named ports");
    std::array<bool,3> occupied{};
    for(const auto& [name,decl]:resources.members()){
        require(safeId(name)&&decl.isObject()&&decl.size()==2&&decl.has("slot")&&decl["slot"].isNumber()&&decl["kind"].isString(),"invalid custom resource declaration");
        const auto slot=unsigned(integer(decl["slot"],0,0,2));
        require(slot<node.info->inputs.size()&&!occupied[slot],"duplicate or invalid custom resource slot");
        occupied[slot]=true;
        const auto kind=node.info->inputs[slot];
        require(decl.getStr("kind")==kindName(kind),"custom resource type mismatch");
        const auto prefix="sample:"+name;
        if(kind==Kind::Scalar)out.samples.emplace(prefix,slot*3);
        else{
            require(kind==Kind::Color,"unsupported custom resource kind");
            for(unsigned i=0;i<3;++i)out.samples.emplace(prefix+"."+std::string(1,"rgb"[i]),slot*3+i);
        }
    }
    const auto& params=q["uniforms"];
    require(params.isNull()||(params.isObject()&&params.size()<=16),"custom uniforms require at most 16 declarations");
    for(const auto& [name,decl]:params.members()){
        require(safeId(name)&&decl.isObject()&&decl.size()==2&&decl["type"].isString()&&decl.has("value"),"invalid custom uniform declaration");
        const auto type=decl.getStr("type");const auto& value=decl["value"];float v=0;
        if(type=="float"){require(value.isNumber(),"float uniform must be numeric");v=scalar(value,0,-16,16);}
        else if(type=="int"){require(value.isNumber(),"int uniform must be numeric");v=float(integer(value,0,-16,16));}
        else if(type=="bool"){require(value.type()==Json::Type::Bool,"bool uniform must be boolean");v=value.boolean()?1.f:0.f;}
        else throw std::invalid_argument("unsupported custom uniform type");
        out.parameters.emplace("param:"+name,v);
    }
    return out;
}
inline unsigned imageChannelShift(const std::string& channel){
    if(channel=="red")return 16;
    if(channel=="green")return 8;
    if(channel=="blue")return 0;
    if(channel=="alpha")return 24;
    throw std::invalid_argument("invalid image asset channel");
}
struct Program {int size=256;std::vector<Node> nodes;std::vector<size_t> order;std::map<std::string,size_t> outputs;};
inline size_t imageSourceBytes(const Program& p){
    size_t bytes=0;for(const auto& n:p.nodes)if(n.image)bytes+=n.image->bytes();
    return bytes;
}
// Source identity for trusted pinned assets. Encoded SHA-256 is verified by
// host before compilation; dimensions and decoder ABI version guard layout.
inline std::string imageSourceIdentity(const Node& n){
    if(!isExternalImageSource(n.info->id))return {};
    require(n.image!=nullptr,"unresolved external image");
    return std::string(n.image->hdr()?"rgba32f-linear-nearest-v1:":n.image->hasAlpha?"rgba8-nearest-v2:":"rgb8-opaque-nearest-v2:")+std::to_string(n.image->width)+"x"+std::to_string(n.image->height)+(n.image->authoredMips?":authored-floor-v1:":":box-ceil-v1:")+std::to_string(n.image->mips.size());
}
inline std::vector<std::string> formulaTokens(const std::string& expression){
    require(!expression.empty()&&expression.size()<=256,"invalid formula size");
    std::istringstream stream(expression);std::vector<std::string> tokens;std::string token;int depth=0;
    while(stream>>token){
        require(tokens.size()<32,"formula token limit");
        if(token=="a"||token=="b"||token=="0"||token=="1"||token=="0.5")++depth;
        else if(token=="abs"||token=="invert"){require(depth>=1,"formula operand missing");}
        else if(token=="+"||token=="-"||token=="*"||token=="min"||token=="max"){require(depth>=2,"formula operand missing");--depth;}
        else throw std::invalid_argument("unsupported formula token");
        tokens.push_back(token);
    }
    require(depth==1,"formula must return one scalar");return tokens;
}
inline float evaluateFormula(const std::vector<std::string>& tokens,float a,float b){
    std::array<float,32> stack{};size_t count=0;
    for(const auto& t:tokens){
        if(t=="a"||t=="b"||t=="0"||t=="1"||t=="0.5")stack[count++]=t=="a"?a:t=="b"?b:t=="1"?1.f:t=="0.5"?.5f:0.f;
        else if(t=="abs")stack[count-1]=std::abs(stack[count-1]);
        else if(t=="invert")stack[count-1]=1.f-stack[count-1];
        else{const float y=stack[--count],x=stack[count-1];stack[count-1]=t=="+"?x+y:t=="-"?x-y:t=="*"?x*y:t=="min"?std::min(x,y):std::max(x,y);}
    }
    return stack[0];
}
inline constexpr int maxEmbeddedImageSide=1024;
inline constexpr size_t maxEmbeddedImagePixels=32768;
inline std::vector<uint32_t> imagePixels(const Json& p){
    const int w=integer(p["width"],1,1,maxEmbeddedImageSide),h=integer(p["height"],1,1,maxEmbeddedImageSide);
    require(size_t(w)*h<=maxEmbeddedImagePixels,"embedded image pixel budget exceeded");
    const auto hex=p.getStr("rgb","808080");
    require(hex.size()==size_t(w)*h*6,"image RGB payload extent mismatch");
    auto digit=[](char c)->uint32_t{if(c>='0'&&c<='9')return uint32_t(c-'0');if(c>='a'&&c<='f')return uint32_t(c-'a'+10);throw std::invalid_argument("invalid image hex digit");};
    std::vector<uint32_t> result;result.reserve(size_t(w)*h);
    for(size_t i=0;i<hex.size();i+=6){uint32_t v=0;for(size_t j=0;j<6;++j)v=(v<<4)|digit(hex[i+j]);result.push_back(v);}
    return result;
}
inline Json properties(const std::string& id){
    Json result=Json::object();
    auto field=[&](const char* key,const char* type,const Json& value,double lo=0,double hi=1){
        Json p=Json::object();p.set("type",Json::string(type));p.set("default",value);
        p.set("min",Json::number(lo));p.set("max",Json::number(hi));result.set(key,p);
    };
    auto options=[&](const char* key,const std::string& value,const std::vector<std::string>& values){
        Json p=Json::object(),list=Json::array();p.set("type",Json::string("enum"));p.set("default",Json::string(value));
        for(const auto& v:values)list.push(Json::string(v));p.set("options",list);result.set(key,p);
    };
    if(id=="image"){field("width","integer",Json::number(int64_t(1)),1,maxEmbeddedImageSide);field("height","integer",Json::number(int64_t(1)),1,maxEmbeddedImageSide);field("rgb","image",Json::string("808080"));}
    if(isExternalImageSource(id)){
        field("asset","text",Json::string(""));field("revision","text",Json::string(""));
        field("hdrExposure","integer",Json::number(int64_t(0)),-12,12);
        options("hdrMapping","reinhard",{"reinhard","clamp"});
        options("filter","nearest",{"nearest","bilinear","trilinear"});
        options("mipMode","base",{"base","auto","explicit"});
        field("mipLevel","number",Json::number(0.0),0,12);
        field("mipBias","number",Json::number(0.0),-8,8);
    }
    if(id=="imageAssetChannel")options("channel","alpha",{"red","green","blue","alpha"});
    if(id=="hdrConstant"){field("radiance","json",Json::parse("[1,1,1]"));field("alpha","number",Json::number(1.0));}
    if(id=="hdrExposure"||id=="hdrToneMap")field("ev","integer",Json::number(int64_t(0)),-12,12);
    if(id=="hdrToneMap")options("mapping","reinhard",{"reinhard","clamp"});
    if(isBufferSource(id))field("values","json",Json::parse(id=="bufferScalar"?"[0,0.5,1]":"[[0,0,0],[0.5,0.5,0.5],[1,1,1]]"));
    if(id=="formula")field("expression","text",Json::string("a b *"));
    if(isCustomCompute(id)){
        if(id=="customScalar")field("code","compute",Json::string("a b c mix"));
        else{
            field("red","compute",Json::string("ar br c mix"));
            field("green","compute",Json::string("ag bg c mix"));
            field("blue","compute",Json::string("ab bb c mix"));
        }
        for(const auto* key:{"p0","p1","p2","p3"})field(key,"number",Json::number(0.5),-16,16);
        options("resourceFilter","nearest",{"nearest","bilinear"});
        options("resourceAddress","clamp",{"clamp","repeat"});
        field("resources","json",Json::object());field("uniforms","json",Json::object());
    }
    if(id=="constant")field("value","number",Json::number(.5));
    if(id=="color")field("value","rgb",Json::parse("[0.5,0.5,0.5]"));
    if(id=="noise"||id=="fbm"||id=="voronoi"){
        field("period","integer",Json::number(int64_t(8)),1,256);
        field("seed","integer",Json::number(int64_t(1)),0,4294967295.0);
        if(id=="fbm")field("octaves","integer",Json::number(int64_t(4)),1,6);
    }
    if(id=="gradient")options("axis","y",{"x","y"});
    if(id=="pattern"||id=="patternMask"){
        options("pattern","brick",texturePatterns());field("scale","integer",Json::number(int64_t(8)),1,64);
        field("seed","integer",Json::number(int64_t(1)),0,4294967295.0);
    }
    if(id=="threshold")field("threshold","number",Json::number(.5));
    if(id=="remap"){field("low","number",Json::number(0.0));field("high","number",Json::number(1.0));}
    if(id=="blur"||id=="erosion")field("radius","integer",Json::number(int64_t(1)),1,4);
    if(id=="warp"||id=="distort")field("amount","integer",Json::number(int64_t(4)),0,16);
    if(id=="normal")field("strength","number",Json::number(2.0),0,16);
    return result;
}
inline void validateParameters(const Node& n){
    const std::string id=n.info->id;const auto& p=n.parameters;
    const auto fields=properties(id);
    for(const auto& [key,value]:p.members()){require(fields.has(key)&&!value.isNull(),"unknown or null texture node property");}
    if(isExternalImageSource(id)){
        const auto asset=p.getStr("asset"),revision=p.getStr("revision");
        require(!asset.empty()&&asset.size()<=512&&revision.size()==64&&revision.find_first_not_of("0123456789abcdef")==std::string::npos,"imageAsset requires a path and pinned SHA-256 revision");
        (void)integer(p["hdrExposure"],0,-12,12);
        require(!p.has("hdrMapping")||(p["hdrMapping"].isString()&&(p.getStr("hdrMapping")=="reinhard"||p.getStr("hdrMapping")=="clamp")),"invalid HDR mapping");
        const auto filter=p.getStr("filter","nearest"),mode=p.getStr("mipMode","base");
        require((!p.has("filter")||p["filter"].isString())&&(filter=="nearest"||filter=="bilinear"||filter=="trilinear"),"invalid image filter");
        require((!p.has("mipMode")||p["mipMode"].isString())&&(mode=="base"||mode=="auto"||mode=="explicit"),"invalid image mip mode");
        (void)scalar(p["mipLevel"],0,0,12);(void)scalar(p["mipBias"],0,-8,8);
    }
    if(id=="imageAssetChannel"){
        require(!p.has("channel")||p["channel"].isString(),"image asset channel must be text");
        (void)imageChannelShift(p.getStr("channel","alpha"));
    }
    if(id=="hdrConstant"&&p.has("radiance")){require(p["radiance"].isArray()&&p["radiance"].size()==3,"HDR constant requires RGB triple");for(const auto& v:p["radiance"].items()){require(v.isNumber(),"HDR channel must be numeric");(void)scalar(v,1,-65504,65504);}}
    if(id=="hdrConstant")(void)scalar(p["alpha"],1);
    if(id=="hdrExposure"||id=="hdrToneMap")(void)integer(p["ev"],0,-12,12);
    if(id=="hdrToneMap")require(!p.has("mapping")||(p["mapping"].isString()&&(p.getStr("mapping")=="reinhard"||p.getStr("mapping")=="clamp")),"invalid tone map");
    if(id=="image")(void)imagePixels(p);
    if(id=="formula")(void)formulaTokens(p.getStr("expression","a b *"));
    if(isCustomCompute(id)){
        for(const auto* key:{"p0","p1","p2","p3"})(void)scalar(p[key],.5f,-16,16);
        const auto filter=p.getStr("resourceFilter","nearest"),address=p.getStr("resourceAddress","clamp");
        require((!p.has("resourceFilter")||p["resourceFilter"].isString())&&(filter=="nearest"||filter=="bilinear"),"invalid custom resource filter");
        require((!p.has("resourceAddress")||p["resourceAddress"].isString())&&(address=="clamp"||address=="repeat"),"invalid custom resource address");
    }
    if(id=="constant")scalar(p["value"],.5f);
    if(id=="color"){require(p["value"].isArray()&&p["value"].size()==3,"color requires RGB triple");for(const auto& v:p["value"].items()){require(v.isNumber(),"RGB channel must be number");scalar(v,0);}}
    if(id=="noise"||id=="fbm"||id=="voronoi"){integer(p["period"],8,1,256);seed(p["seed"]);if(id=="fbm")integer(p["octaves"],4,1,6);}
    if(id=="gradient")require(!p.has("axis")||p.getStr("axis")=="x"||p.getStr("axis")=="y","gradient axis must be x or y");
    if(id=="pattern"||id=="patternMask"){auto patterns=texturePatterns();require(std::find(patterns.begin(),patterns.end(),p.getStr("pattern"))!=patterns.end(),"unknown native pattern");integer(p["scale"],8,1,64);seed(p["seed"]);}
    if(id=="threshold")scalar(p["threshold"],.5f);
    if(id=="remap"){scalar(p["low"],0);scalar(p["high"],1);}
    if(id=="blur"||id=="erosion")integer(p["radius"],1,1,4);
    if(id=="warp"||id=="distort")integer(p["amount"],4,0,16);
    if(id=="normal")scalar(p["strength"],2,0,16);
}
inline bool neighborhoodOp(std::string_view id){return id=="blur"||id=="erosion"||id=="warp"||id=="distort";}
inline bool requiresMaterialized(const Program& p){return std::any_of(p.nodes.begin(),p.nodes.end(),[](const Node& n){return neighborhoodOp(n.info->id)||isImageSource(n.info->id)||isCustomCompute(n.info->id)||n.info->output==Kind::Radiance||(std::string_view(n.info->id)=="hdrToneMap"||std::string_view(n.info->id)=="hdrAlpha");});}
inline Program compileResolved(const Json& g,const ImageResolver& resolver={}){
    require(g.isObject()&&g.getInt("version")==1&&g.dump(-1).size()<=256*1024,"texture graph version 1 / 256 KiB required");
    Program p;p.size=integer(g["size"],256,8,1024);
    const auto& nodes=g["nodes"];require(nodes.isArray()&&nodes.size()>0&&nodes.size()<=64,"texture graph requires 1..64 nodes");
    require(size_t(p.size)*p.size*nodes.size()<=4194304,"texture graph pixel-node budget exceeded (reduce resolution or nodes)");
    std::map<std::string,size_t> ids;
    size_t sourceBytes=0,customInstructions=0;
    for(const auto& j:nodes.items()){
        Node n;n.id=j.getStr("id");require(safeId(n.id)&&ids.emplace(n.id,p.nodes.size()).second,"invalid or duplicate texture node ID");
        n.info=&op(j.getStr("op"));n.parameters=j.has("properties")?j["properties"]:Json::object();require(n.parameters.isObject(),"properties must be object");validateParameters(n);
        require(j["inputs"].isArray()&&j["inputs"].size()==n.info->inputs.size(),"texture input arity mismatch");
        if(isCustomCompute(n.info->id)){
            const bool color=std::string_view(n.info->id)=="customColor";
            const auto bindings=customBindings(n);
            constexpr const char* keys[]={"red","green","blue"};
            constexpr const char* defaults[]={"ar br c mix","ag bg c mix","ab bb c mix"};
            for(size_t channel=0;channel<(color?3u:1u);++channel){
                const auto* key=color?keys[channel]:"code";
                require(!n.parameters.has(key)||n.parameters[key].isString(),"custom compute code must be text");
                n.custom[channel]=custom_compute::parse(n.parameters.getStr(key,color?defaults[channel]:"a b c mix"),color,bindings);
                customInstructions+=custom_compute::workUnits(n.custom[channel]);
            }
            require(customInstructions<=custom_compute::maxWork/(size_t(p.size)*p.size),"custom compute pixel-instruction budget exceeded");
        }
        if(isImageSource(n.info->id)){
            ImageSource data;
            if(std::string_view(n.info->id)=="image"){
                data.width=integer(n.parameters["width"],1,1,maxEmbeddedImageSide);
                data.height=integer(n.parameters["height"],1,1,maxEmbeddedImageSide);
                data.rgb=imagePixels(n.parameters);
            }else if(isBufferSource(n.info->id)){
                data=bufferSource(n.info->id,n.parameters,maxImageSourceBytes-sourceBytes);
            }else{
                require(bool(resolver),"external imageAsset requires an explicit host resolver");
                data=resolver(n.parameters.getStr("asset"),n.parameters.getStr("revision"),maxImageSourceBytes-sourceBytes);
            }
            require(data.width>0&&data.height>0&&data.width<=4096&&data.height<=4096,"image source extent exceeded");
            size_t texels=size_t(data.width)*data.height;
            const size_t stride=data.hdr()?16u:4u,available=(maxImageSourceBytes-sourceBytes)/stride;
            require(texels<=available,"image source byte budget exceeded");
            if(data.authoredMips){
                require(!data.mips.empty()&&data.mips.size()<=13,"invalid authored mip count");
                texels=0;int w=data.width,h=data.height;
                for(size_t i=0;i<data.mips.size();++i){
                    const auto& level=data.mips[i];
                    require(level.width==w&&level.height==h&&level.offset==texels,"invalid authored mip layout");
                    const size_t count=size_t(w)*h;
                    require(count<=available-texels,"authored mip chain exceeds source byte budget");texels+=count;
                    require(w>1||h>1||i+1==data.mips.size(),"authored mip chain exceeds extent");
                    w=std::max(1,w/2);h=std::max(1,h/2);
                }
            }else require(data.mips.empty(),"resolver must return base image or authored mip chain");
            if(std::string_view(n.info->id)=="hdrAsset")require(data.hdr(),"hdrAsset requires a linear HDR source");
            if(data.hdr()){
                require(data.rgb.empty()&&data.hasAlpha&&data.rgba32f.size()==texels*4,"invalid HDR snapshot layout");
                for(float v:data.rgba32f)require(std::isfinite(v)&&std::abs(v)<=65504.f,"invalid HDR snapshot value");
            }else require(data.rgb.size()==texels,"invalid RGB snapshot layout");
            if(n.parameters.getStr("mipMode","base")!="base"&&!data.authoredMips)buildImageMips(data,maxImageSourceBytes-sourceBytes);
            sourceBytes+=data.bytes();
            n.image=std::make_shared<const ImageSource>(std::move(data));
        }
        p.nodes.push_back(std::move(n));
    }
    for(size_t i=0;i<p.nodes.size();++i)for(size_t j=0;j<p.nodes[i].info->inputs.size();++j){
        auto it=ids.find(nodes[i]["inputs"][j].str());require(it!=ids.end(),"missing texture input");
        require(p.nodes[it->second].info->output==p.nodes[i].info->inputs[j],"texture port type mismatch");p.nodes[i].inputs.push_back(it->second);
    }
    std::vector<int> marks(p.nodes.size());std::function<void(size_t)> visit=[&](size_t i){
        require(marks[i]!=1,"texture graph cycle");if(marks[i]==2)return;marks[i]=1;
        for(auto dep:p.nodes[i].inputs)visit(dep);marks[i]=2;p.order.push_back(i);
    };for(size_t i=0;i<p.nodes.size();++i)visit(i);
    require(g["outputs"].isObject()&&g["outputs"].size()>0&&g["outputs"].size()<=9,"texture outputs required");
    for(const auto& [key,value]:g["outputs"].members()){
        auto it=ids.find(value.str());require(it!=ids.end(),"missing texture output node");
        require(outputKind(key)==p.nodes[it->second].info->output,"texture output type mismatch");p.outputs.emplace(key,it->second);
    }return p;
}
// A declaration binds one named value to one or more existing native properties.
// Source graph is never mutated. Validate both authored defaults and overrides.
inline Json resolveParameters(const Json& source,const Json& overrides=Json::object()){
    require(source.isObject()&&source.dump(-1).size()<=256*1024,"invalid texture source");
    require(overrides.isObject()&&overrides.size()<=32&&overrides.dump(-1).size()<=32768,"invalid texture overrides");
    const auto& defs=source["parameters"];
    require(defs.isNull()||(defs.isObject()&&defs.size()<=32),"invalid texture parameter declarations");
    for(const auto& [name,value]:overrides.members()){(void)value;require(defs.has(name),"unknown texture override");}
    Json resolved=source;resolved.erase("parameters");
    require(resolved["nodes"].isArray()&&resolved["nodes"].size()<=64,"invalid texture nodes");
    std::map<std::string,size_t> ids;for(size_t i=0;i<resolved["nodes"].size();++i)ids.emplace(resolved["nodes"][i].getStr("id"),i);
    std::map<std::pair<std::string,std::string>,bool> bound;
    for(const auto& [name,def]:defs.members()){
        require(safeId(name)&&def.isObject()&&def.has("default")&&!def["default"].isNull(),"invalid parameter declaration");
        const auto& bindings=def["bindings"];require(bindings.isArray()&&bindings.size()>0&&bindings.size()<=32,"invalid parameter bindings");
        const Json& value=overrides.has(name)?overrides[name]:def["default"];
        require(!value.isNull(),"null parameter override");
        std::string commonType;
        for(const auto& b:bindings.items()){
            const auto node=b.getStr("node"),key=b.getStr("property");auto it=ids.find(node);
            require(it!=ids.end()&&bound.emplace(std::make_pair(node,key),true).second,"unknown or duplicate parameter binding");
            auto& entry=resolved.member("nodes").at(it->second);
            if(!entry.has("properties"))entry.set("properties",Json::object());
            require(entry["properties"].isObject(),"invalid bound properties");
            if(entry.getStr("op")=="function"){
                const std::string prefix="arguments.";
                require(key.rfind(prefix,0)==0&&safeId(key.substr(prefix.size())),"function binding must target arguments.<name>");
                require(commonType.empty()||commonType=="functionArgument","parameter binding type mismatch");commonType="functionArgument";
                auto& props=entry.member("properties");
                if(!props.has("arguments"))props.set("arguments",Json::object());
                require(props["arguments"].isObject(),"invalid bound function arguments");
                props.member("arguments").set(key.substr(prefix.size()),value);
            }else{
                auto schema=properties(entry.getStr("op"));
                require(schema.has(key),"unknown bound node property");
                const auto type=schema[key].getStr("type");require(commonType.empty()||commonType==type,"parameter binding type mismatch");commonType=type;
                entry.member("properties").set(key,value);
            }
        }
    }
    return resolved;
}
// Expand embedded function calls recursively into the existing typed DAG.
inline constexpr size_t maxFunctionDepth=8;
inline Json expandFunctions(const Json& source){
    const auto& defs=source["functions"];
    if(defs.isNull())return source;
    require(defs.isObject()&&defs.size()<=16,"invalid texture function library");
    require(source["nodes"].isArray()&&source["nodes"].size()<=64,"invalid texture function graph");
    auto definition=[&](const Json& call)->const Json&{
        const auto& props=call["properties"];
        require(props.isObject()&&safeId(props.getStr("function"))&&safeId(props.getStr("output")),"invalid texture function call");
        for(const auto& [k,v]:props.members()){(void)v;require(k=="function"||k=="output"||k=="arguments","unknown texture function call property");}
        require(props["arguments"].isNull()||props["arguments"].isObject(),"invalid function arguments");
        const auto& def=defs[props.getStr("function")];
        require(def.isObject(),"unknown texture function definition");
        return def;
    };
    auto selected=[&](const Json& call,const Json& def){
        const auto& result=def["outputs"][call["properties"].getStr("output")];
        require(result.isString()&&safeId(result.str()),"unknown texture function output");return result.str();
    };
    // Validate the entire library, including currently unused definitions.
    std::map<std::string,int> state;
    std::map<std::string,std::map<std::string,Kind>> outputs;
    std::map<std::string,size_t> depths;
    std::function<void(const std::string&,size_t)> validate;
    validate=[&](const std::string& name,size_t depth){
        require(depth<=maxFunctionDepth,"texture function nesting exceeds depth limit");
        require(state[name]!=1,"recursive texture function call");
        if(state[name]==2){require(depth+depths.at(name)-1<=maxFunctionDepth,"texture function nesting exceeds depth limit");return;}
        state[name]=1;size_t height=1;
        const auto& def=defs[name];
        require(safeId(name)&&def.isObject()&&def["inputs"].isArray()&&def["inputs"].size()<=3&&def["nodes"].isArray()&&def["nodes"].size()>0&&def["nodes"].size()<=64&&def["outputs"].isObject()&&def["outputs"].size()>0&&def["outputs"].size()<=9,"invalid texture function definition");
        require(def["functions"].isNull(),"function definitions must use the shared library");
        std::map<std::string,Kind> local;
        std::map<std::string,bool> bodyIds;
        for(const auto& f:def["inputs"].items()){
            const auto id=f.getStr("id"),type=f.getStr("type");
            require(safeId(id)&&(type=="scalar"||type=="color"||type=="normal"||type=="packed"||type=="radiance"),"invalid texture formal input");
            const auto kind=type=="scalar"?Kind::Scalar:type=="color"?Kind::Color:type=="normal"?Kind::Normal:type=="radiance"?Kind::Radiance:Kind::Packed;
            require(local.emplace(id,kind).second,"duplicate texture formal input");
        }
        for(const auto& row:def["nodes"].items()){
            const auto id=row.getStr("id"),operation=row.getStr("op");
            require(safeId(id)&&bodyIds.emplace(id,true).second,"invalid or duplicate function body node ID");
            Kind kind;
            if(operation=="function"){
                const auto& child=definition(row);const auto fn=row["properties"].getStr("function");
                (void)selected(row,child);validate(fn,depth+1);
                height=std::max(height,1+depths.at(fn));
                kind=outputs.at(fn).at(row["properties"].getStr("output"));
            }else kind=op(operation).output;
            require(local.emplace(id,kind).second,"duplicate texture function local ID");
        }
        for(const auto& row:def["nodes"].items()){
            std::vector<Kind> expected;
            if(row.getStr("op")=="function"){
                const auto& child=definition(row);
                for(const auto& f:child["inputs"].items()){
                    const auto type=f.getStr("type");
                    expected.push_back(type=="scalar"?Kind::Scalar:type=="color"?Kind::Color:type=="normal"?Kind::Normal:type=="radiance"?Kind::Radiance:Kind::Packed);
                }
            }else expected=op(row.getStr("op")).inputs;
            require(row["inputs"].isArray()&&row["inputs"].size()==expected.size(),"texture function input arity mismatch");
            for(size_t i=0;i<expected.size();++i){
                auto arg=local.find(row["inputs"][i].str());
                require(arg!=local.end()&&arg->second==expected[i],"texture function local input type mismatch");
            }
        }
        // A local data-flow cycle is distinct from recursive calls.
        std::map<std::string,int> marks;
        std::map<std::string,const Json*> body;
        for(const auto& row:def["nodes"].items())body.emplace(row.getStr("id"),&row);
        std::function<void(const std::string&)> visit=[&](const std::string& id){
            if(!body.count(id))return;
            require(marks[id]!=1,"texture function local data-flow cycle");if(marks[id]==2)return;
            marks[id]=1;for(const auto& link:(*body.at(id))["inputs"].items())visit(link.str());marks[id]=2;
        };
        for(const auto& entry:body)visit(entry.first);
        for(const auto& [key,value]:def["outputs"].members()){
            require(safeId(key)&&value.isString()&&bodyIds.count(value.str()),"texture function output must reference a body node");
            outputs[name].emplace(key,local.at(value.str()));
        }
        depths[name]=height;state[name]=2;
    };
    for(const auto& [name,def]:defs.members()){(void)def;validate(name,1);}
    auto nodeKind=[&](const Json& row){
        if(row.getStr("op")!="function")return op(row.getStr("op")).output;
        const auto& def=definition(row);(void)selected(row,def);
        return outputs.at(row["properties"].getStr("function")).at(row["properties"].getStr("output"));
    };
    std::map<std::string,Kind> kinds;
    std::map<std::string,bool> used;
    for(const auto& row:source["nodes"].items()){
        const auto id=row.getStr("id");
        require(safeId(id)&&used.emplace(id,true).second,"invalid or duplicate texture node ID");
        kinds.emplace(id,nodeKind(row));
    }
    Json flat=Json::array();size_t serial=0,visits=0;
    std::function<void(const Json&,size_t)> expand;
    expand=[&](const Json& row,size_t depth){
        require(++visits<=64*(maxFunctionDepth+1),"texture function expansion work limit");
        if(row.getStr("op")!="function"){require(flat.size()<64,"expanded texture graph exceeds node limit");flat.push(row);return;}
        require(depth<=maxFunctionDepth,"texture function nesting exceeds depth limit");
        const auto& def=definition(row);const auto result=selected(row,def);
        const auto& actual=row["inputs"];const auto& formals=def["inputs"];
        require(actual.isArray()&&actual.size()==formals.size(),"texture function input arity mismatch");
        std::map<std::string,std::string> names;
        for(size_t i=0;i<formals.size();++i){
            const auto formal=formals[i].getStr("id"),type=formals[i].getStr("type"),arg=actual[i].str();
            const auto kind=kinds.find(arg);
            require(kind!=kinds.end()&&type==kindName(kind->second)&&names.emplace(formal,arg).second,"invalid texture function input or type mismatch");
        }
        const auto& args=row["properties"]["arguments"];
        const auto body=resolveParameters(def,args.isNull()?Json::object():args);
        for(const auto& n:body["nodes"].items()){
            const auto id=n.getStr("id");std::string mapped=row.getStr("id");
            if(id!=result){
                do{mapped="fn_internal_"+std::to_string(++serial);}while(used.count(mapped));
                used.emplace(mapped,true);kinds.emplace(mapped,nodeKind(n));
            }
            require(names.emplace(id,mapped).second,"duplicate texture function local ID");
        }
        for(const auto& n:body["nodes"].items()){
            Json expanded=n,inputs=Json::array();
            for(const auto& link:n["inputs"].items()){
                const auto it=names.find(link.str());require(it!=names.end(),"unknown texture function local input");
                inputs.push(Json::string(it->second));
            }
            expanded.set("id",Json::string(names.at(n.getStr("id"))));expanded.set("inputs",inputs);
            expand(expanded,depth+1);
        }
    };
    for(const auto& row:source["nodes"].items())expand(row,1);
    Json expanded=source;expanded.erase("functions");expanded.set("nodes",flat);
    return expanded;
}
inline Program compile(const Json& g,const Json& overrides=Json::object(),const ImageResolver& resolver={}){
    auto defaults=expandFunctions(resolveParameters(g));
    if(overrides.isObject()&&overrides.size()==0)return compileResolved(defaults,resolver);
    (void)compileResolved(defaults,resolver);
    return compileResolved(expandFunctions(resolveParameters(g,overrides)),resolver);
}
inline float clamp(float x){return std::clamp(x,0.f,1.f);}
inline Pixel gray(float x){return {x,x,x};}
inline Pixel mix(Pixel a,Pixel b,float t){return {a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t,a[3]+(b[3]-a[3])*t};}
inline std::vector<std::vector<Pixel>> evaluateFields(const Program& p,const std::vector<size_t>& targets,int resolution=0){
    const int n=resolution?resolution:p.size;require(!targets.empty()&&targets.size()<=9&&n>=8&&n<=p.size,"invalid texture evaluation target or resolution");
    for(auto target:targets)require(target<p.nodes.size(),"invalid target");
    std::vector<std::vector<Pixel>> images(p.nodes.size());
    std::vector<std::vector<size_t>> dependencies;dependencies.reserve(p.nodes.size());
    for(const auto& node:p.nodes)dependencies.push_back(node.inputs);
    const auto lifetime=planDagLifetime(dependencies,p.order,targets);
    const size_t count=size_t(n)*n;
    for(const auto& step:lifetime.steps){
        const auto i=step.node;
        const auto& node=p.nodes[i];const auto& q=node.parameters;const std::string id=node.info->id;
        auto& out=images[i];out.resize(count);
        if(id=="pattern"||id=="patternMask"){
            Json recipe=q;recipe.set("size",Json::number(int64_t(n)));recipe.set("output",Json::string(id=="pattern"?"color":"mask"));
            Image img;generateTexture(recipe,img);
            for(size_t k=0;k<count;++k)out[k]={img.rgba[k*4]/255.f,img.rgba[k*4+1]/255.f,img.rgba[k*4+2]/255.f};continue;
        }
        const auto formula=id=="formula"?formulaTokens(q.getStr("expression","a b *")):std::vector<std::string>{};
        const bool custom=isCustomCompute(id);
        const bool resourceLinear=custom&&q.getStr("resourceFilter","nearest")=="bilinear",resourceRepeat=custom&&q.getStr("resourceAddress","clamp")=="repeat";
        const std::array<float,4> params=custom?std::array<float,4>{scalar(q["p0"],.5f,-16,16),scalar(q["p1"],.5f,-16,16),scalar(q["p2"],.5f,-16,16),scalar(q["p3"],.5f,-16,16)}:std::array<float,4>{};
        const auto* imported=node.image.get();
        if(isImageSource(id))require(imported!=nullptr,"missing resolved image source");
        const unsigned imageShift=id=="imageAssetChannel"?imageChannelShift(q.getStr("channel","alpha")):0;
        const bool hdr=imported&&imported->hdr()&&!isBufferSource(id);
        const auto sampling=imported?imageSampling(*imported,q,n):ImageSampling{};
        const float hdrGain=hdr?std::ldexp(1.f,integer(q["hdrExposure"],0,-12,12)):1.f;
        const bool reinhard=q.getStr("hdrMapping","reinhard")=="reinhard";
        const float value=scalar(q["value"].isArray()?Json():q["value"],.5f);
        const int period=integer(q["period"],8,1,256),octaves=id=="fbm"?integer(q["octaves"],4,1,6):1;const auto randomSeed=seed(q["seed"]);
        const float strength=scalar(q["strength"],2,0,16),low=scalar(q["low"],0),high=scalar(q["high"],1),threshold=scalar(q["threshold"],.5f);
        const bool axisX=q.getStr("axis","y")=="x";
        const Pixel hdrColor=id=="hdrConstant"?Pixel{scalar(q["radiance"][0],1,-65504,65504),scalar(q["radiance"][1],1,-65504,65504),scalar(q["radiance"][2],1,-65504,65504),scalar(q["alpha"],1)}:Pixel{};
        const float evGain=(id=="hdrExposure"||id=="hdrToneMap")?std::ldexp(1.f,integer(q["ev"],0,-12,12)):1.f;
        const Pixel color=id=="color"?Pixel{scalar(q["value"][0],0),scalar(q["value"][1],0),scalar(q["value"][2],0)}:Pixel{};
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){
            const size_t k=size_t(y)*n+x;const float u=(x+.5f)/n,v=(y+.5f)/n;
            auto input=[&](size_t slot)->const Pixel&{return images[node.inputs[slot]][k];};
            auto a=[&](){return input(0)[0];};auto b=[&](){return input(1)[0];};
            Pixel r{};
            if(id=="constant")r=gray(value);else if(id=="color")r=color;
            else if(id=="hdrConstant")r=hdrColor;
            else if(id=="hdrFromColor"){r=input(0);r[3]=1.f;}
            else if(id=="hdrExposure"){r=input(0);for(size_t c=0;c<3;++c)r[c]*=evGain;}
            else if(id=="hdrAdd"||id=="hdrMultiply"){for(size_t c=0;c<3;++c)r[c]=id=="hdrAdd"?input(0)[c]+input(1)[c]:input(0)[c]*input(1)[c];r[3]=id=="hdrAdd"?std::max(input(0)[3],input(1)[3]):input(0)[3]*input(1)[3];}
            else if(id=="hdrAlpha")r=gray(input(0)[3]);
            else if(id=="hdrSetAlpha"){r=input(0);r[3]=input(1)[0];}
            else if(id=="hdrBlend")r=mix(input(0),input(1),input(2)[0]);
            else if(id=="hdrToneMap")for(size_t c=0;c<3;++c)r[c]=mapRadiance(input(0)[c],evGain,q.getStr("mapping","reinhard")=="reinhard");
            else if(id=="noise"||id=="fbm")r=gray(textureNoise(u,v,period,octaves,randomSeed));
            else if(id=="voronoi")r=gray(textureVoronoi(u,v,period,randomSeed));
            else if(id=="gradient")r=gray(axisX?u:v);
            else if(id=="formula")r=gray(evaluateFormula(formula,a(),b()));
            else if(custom){
                const auto& aa=input(0);const auto& bb=input(1);
                const custom_compute::Values args={aa[0],aa[1],aa[2],bb[0],bb[1],bb[2],input(2)[0],u,v,params[0],params[1],params[2],params[3]};
                auto sample=[&](unsigned index,float su,float sv){
                    const auto slot=index/3,channel=index%3;
                    auto address=[&](float v){return resourceRepeat?v-std::floor(v):std::clamp(v,0.f,1.f);};
                    auto addressIndex=[&](int v){return resourceRepeat?((v%n)+n)%n:std::clamp(v,0,n-1);};
                    auto texel=[&](int xx,int yy){return images[node.inputs[slot]][size_t(addressIndex(yy))*n+addressIndex(xx)][channel];};
                    const float fx=address(su)*n-(resourceLinear?.5f:0.f),fy=address(sv)*n-(resourceLinear?.5f:0.f);
                    const int ix=int(std::floor(fx)),iy=int(std::floor(fy));
                    if(!resourceLinear)return texel(ix,iy);
                    const float tx=fx-ix,ty=fy-iy;
                    const float a=texel(ix,iy),b=texel(ix+1,iy),c=texel(ix,iy+1),d=texel(ix+1,iy+1);
                    const float lo=a+(b-a)*tx,hi=c+(d-c)*tx;
                    return lo+(hi-lo)*ty;
                };
                if(id=="customScalar")r=gray(custom_compute::evaluate(node.custom[0],args,sample));
                else for(size_t channel=0;channel<3;++channel)r[channel]=custom_compute::evaluate(node.custom[channel],args,sample);
            }
            else if(isImageSource(id)){
                const auto v=sampleRaster(*imported,sampling,x,y,n);
                auto mapped=[&](unsigned c){return c==3?clamp(v[3]):hdr?mapRadiance(v[c],hdrGain,reinhard):v[c];};
                if(id=="hdrAsset")r={v[0],v[1],v[2],clamp(v[3])};
                else if(id=="imageAssetChannel")r=gray(mapped(imageShift==24?3:2-imageShift/8));
                else r={mapped(0),mapped(1),mapped(2)};
            }
            else if(id=="add")r=gray(a()+b());else if(id=="subtract")r=gray(a()-b());else if(id=="multiply")r=gray(a()*b());
            else if(id=="min")r=gray(std::min(a(),b()));else if(id=="max")r=gray(std::max(a(),b()));else if(id=="invert")r=gray(1-a());
            else if(id=="threshold")r=gray(a()>=threshold?1.f:0.f);else if(id=="remap")r=gray(low+(high-low)*a());
            else if(id=="blend")r=mix(input(0),input(1),input(2)[0]);else if(id=="colorize")r=mix(input(1),input(2),a());
            else if(id=="blendColor")r=mix(input(0),input(1),input(2)[0]);
            else if(id=="metallicRoughness")r={1.f,a(),b()}; // G roughness, B metallic
            else if(neighborhoodOp(id)){
                auto sample=[&](int xx,int yy){return images[node.inputs[0]][size_t((yy%n+n)%n)*n+(xx%n+n)%n][0];};
                if(id=="blur"||id=="erosion"){
                    const int radius=integer(q["radius"],1,1,4);float v=id=="erosion"?1.f:0.f;
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto t=sample(x+dx*radius,y+dy*radius);v=id=="erosion"?std::min(v,t):v+t;}
                    r=gray(id=="blur"?v/9.f:v);
                }else{
                    const float amount=float(integer(q["amount"],4,0,16));
                    const int dx=int(std::floor((input(1)[0]-.5f)*2.f*amount+.5f));
                    const int dy=id=="warp"?int(std::floor((input(2)[0]-.5f)*2.f*amount+.5f)):-dx;
                    r=gray(sample(x+dx,y+dy));
                }
            }
            else if(id=="normal"){
                auto h=[&](int xx,int yy){return images[node.inputs[0]][size_t((yy+n)%n)*n+(xx+n)%n][0];};
                const float dx=(h(x+1,y)-h(x-1,y))*strength*n/256.f,dy=(h(x,y+1)-h(x,y-1))*strength*n/256.f;
                const float len=std::sqrt(dx*dx+dy*dy+1);r={.5f-.5f*dx/len,.5f+.5f*dy/len,.5f+.5f/len};
            }
            for(size_t c=0;c<3;++c)r[c]=node.info->output==Kind::Radiance?std::clamp(r[c],-65504.f,65504.f):clamp(r[c]);
            r[3]=node.info->output==Kind::Radiance?clamp(r[3]):1.f;out[k]=r;
        }
        // All pixels (including neighborhood samples) have been consumed.
        for(auto dep:step.releaseAfter)std::vector<Pixel>{}.swap(images[dep]);
    }
    std::vector<std::vector<Pixel>> result;result.reserve(targets.size());
    for(auto target:targets)result.push_back(images[target]);
    return result;
}
// Display preview only: HDR targets use a fixed Reinhard display mapping.
// Raw export and native HDR upload use evaluateRadiance instead.
inline std::vector<Image> evaluateMany(const Program& p,const std::vector<size_t>& targets,int resolution=0){
    const int n=resolution?resolution:p.size;const size_t count=size_t(n)*n;
    auto fields=evaluateFields(p,targets,resolution);
    std::vector<Image> result;result.reserve(targets.size());
    for(size_t t=0;t<targets.size();++t){
        Image img;img.width=img.height=n;img.name="generated:texture-graph";img.rgba.resize(count*4,255);
        const bool hdr=p.nodes[targets[t]].info->output==Kind::Radiance;
        for(size_t i=0;i<count;++i)for(size_t c=0;c<3;++c){
            const float x=fields[t][i][c];
            img.rgba[i*4+c]=uint8_t(std::lround((hdr?mapRadiance(x,1.f,true):clamp(x))*255.f));
        }
        result.push_back(std::move(img));
    }
    return result;
}
inline std::vector<float> evaluateRadiance(const Program& p,size_t target,int resolution=0){
    require(target<p.nodes.size()&&p.nodes[target].info->output==Kind::Radiance,"raw export requires radiance target");
    const auto field=evaluateFields(p,{target},resolution);
    std::vector<float> rgba;rgba.reserve(field.front().size()*4);
    for(const auto& rgb:field.front())rgba.insert(rgba.end(),rgb.begin(),rgb.end());
    return rgba;
}
inline Image evaluate(const Program& p,size_t target,int resolution=0){return std::move(evaluateMany(p,{target},resolution).front());
}
inline Json describeCatalog(){
    Json r=Json::object(),nodes=Json::array();
    for(const auto& o:catalog()){Json n=Json::object(),inputs=Json::array();n.set("id",Json::string(o.id));n.set("output",Json::string(kindName(o.output)));for(auto k:o.inputs)inputs.push(Json::string(kindName(k)));n.set("inputs",inputs);n.set("properties",properties(o.id));nodes.push(n);}
    r.set("ok",Json::fromBool(true));r.set("nodes",nodes);r.set("outputs",Json::parse(R"(["color","normal","height","roughness","metallic","metallicRoughness","ao","emissive","mask","radiance"])"));Json outputTypes=Json::object();for(const auto& v:r["outputs"].items())outputTypes.set(v.str(),Json::string(kindName(outputKind(v.str()))));r.set("outputTypes",outputTypes);return r;
}
} // namespace aiverse::render::texture_graph
