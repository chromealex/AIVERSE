// Bounded portable HDR lat-long convolution. Renderer owns no asset I/O.
// Bands 0..3 are GGX importance-sampled specular radiance at roughness
// 0, 1/3, 2/3, 1; band 4 is cosine-weighted irradiance / pi.
// Split-sum approximation assumes N=V=R during prefiltering.
#pragma once
#include "HdrTexture.h"
#include <stdexcept>
#include <span>
namespace aiverse::render::environment_ibl {
inline constexpr int width=128,height=64,bandWidth=width+2,bandHeight=height+2,bands=5;
inline constexpr float pi=3.14159265358979323846f;
struct Vec {float x=0,y=0,z=0;};
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator*(Vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec unit(Vec a){const float q=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);return q>0?a*(1.f/q):Vec{0,1,0};}
inline float halfValue(uint16_t h){
    const unsigned e=(h>>10)&31u,m=h&1023u;
    if((h&0x8000u)||e==31)return 0;
    return e?std::ldexp(1.f+float(m)/1024.f,int(e)-15):std::ldexp(float(m),-24);
}
inline Vec direction(float u,float v){
    const float phi=(u-.5f)*2*pi,theta=v*pi,s=std::sin(theta);
    return {std::cos(phi)*s,std::cos(theta),std::sin(phi)*s};
}
inline Vec sample(const RadianceImage& src,Vec d){
    const float u=std::atan2(d.z,d.x)/(2*pi)+.5f,v=std::acos(std::clamp(d.y,-1.f,1.f))/pi;
    const float fx=u*src.width-.5f,fy=v*src.height-.5f;
    const int x=int(std::floor(fx)),y=int(std::floor(fy));
    auto texel=[&](int xx,int yy){
        xx=(xx%src.width+src.width)%src.width;yy=std::clamp(yy,0,src.height-1);
        const auto i=(size_t(yy)*src.width+xx)*4;
        return Vec{halfValue(src.rgba16f[i]),halfValue(src.rgba16f[i+1]),halfValue(src.rgba16f[i+2])};
    };
    const float a=fx-x,b=fy-y;
    return (texel(x,y)*(1-a)+texel(x+1,y)*a)*(1-b)+(texel(x,y+1)*(1-a)+texel(x+1,y+1)*a)*b;
}
// Deterministic Hammersley sequence (base-2 radical inverse).
inline float radicalInverse(uint32_t bits){
    bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    return float(bits)*2.3283064365386963e-10f;
}
struct WeightedDirection {Vec direction;float weight=0;};
inline WeightedDirection ggxDirection(float roughness,float u,float v){
    // GGX NDF importance sample H, alpha=perceptual roughness squared.
    const float alpha=roughness*roughness,a2=alpha*alpha;
    const float phi=2.f*pi*v;
    const float cosTheta=std::sqrt((1.f-u)/(1.f+(a2-1.f)*u));
    const float sinTheta=std::sqrt(std::max(0.f,1.f-cosTheta*cosTheta));
    // Reflect -V about H, with V=N=(0,0,1).
    const Vec l={2.f*cosTheta*sinTheta*std::cos(phi),2.f*cosTheta*sinTheta*std::sin(phi),2.f*cosTheta*cosTheta-1.f};
    return {l,std::max(0.f,l.z)};
}
inline WeightedDirection diffuseDirection(float u,float v){
    const float phi=2.f*pi*v,s=std::sqrt(u);
    return {{s*std::cos(phi),s*std::sin(phi),std::sqrt(1.f-u)},1.f};
}
inline constexpr int specularSamples=256,diffuseSamples=128;
// Independent split-sum BRDF integral. The LUT is stored below the five
// radiance bands, with a one-texel clamp gutter, to avoid another sampler.
inline constexpr int brdfSize=128,brdfSamples=512;
inline constexpr int radianceHeight=bandHeight*bands,brdfHeight=brdfSize+2,combinedHeight=radianceHeight+brdfHeight;
static_assert(brdfSize+2==bandWidth);
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float smithG1(float nd,float a2){
    if(nd<=0.f)return 0.f;
    return 2.f*nd/(nd+std::sqrt(a2+(1.f-a2)*nd*nd));
}
inline float fresnel5(float x){const float x2=x*x;return x2*x2*x;}
inline std::vector<Vec> brdfHalfVectors(float roughness){
    const float a=roughness*roughness,a2=a*a;
    std::vector<Vec> samples;samples.reserve(brdfSamples);
    for(int i=0;i<brdfSamples;++i){
        const float u=(i+.5f)/float(brdfSamples),phi=2.f*pi*radicalInverse(uint32_t(i));
        const float z=std::sqrt((1.f-u)/(1.f+(a2-1.f)*u)),s=std::sqrt(std::max(0.f,1.f-z*z));
        samples.push_back({s*std::cos(phi),s*std::sin(phi),z});
    }
    return samples;
}
inline std::array<float,2> integrateBrdf(float ndv,float roughness,const std::vector<Vec>& halfVectors){
    ndv=std::clamp(ndv,0.0001f,1.f);roughness=std::clamp(roughness,0.f,1.f);
    if(roughness==0.f){const float f=fresnel5(1.f-ndv);return {1.f-f,f};}
    const float a=roughness*roughness,a2=a*a;
    const Vec v={std::sqrt(std::max(0.f,1.f-ndv*ndv)),0.f,ndv};
    const float gv=smithG1(ndv,a2);double scale=0,bias=0;
    for(const auto& h:halfVectors){
        const float vdh=dot(v,h);
        if(vdh<=0.f||h.z<=0.f)continue;
        const float ndl=2.f*vdh*h.z-ndv;
        if(ndl<=0.f)continue;
        const float visibility=gv*smithG1(ndl,a2)*vdh/(h.z*ndv);
        const float fc=fresnel5(1.f-std::clamp(vdh,0.f,1.f));
        scale+=(1.f-fc)*visibility;bias+=fc*visibility;
    }
    // Divide by all samples, including rejected below-horizon directions.
    const double count=double(halfVectors.size());
    if(count==0)return {0.f,0.f};
    return {float(std::clamp(scale/count,0.0,1.0)),float(std::clamp(bias/count,0.0,1.0))};
}
inline std::array<float,2> integrateBrdf(float ndv,float roughness){
    roughness=std::clamp(roughness,0.f,1.f);
    return integrateBrdf(ndv,roughness,brdfHalfVectors(roughness));
}
// Cached CPU pixels are independent of project/environment and immutable.
inline const std::vector<uint16_t>& brdfLut(){
    static const std::vector<uint16_t> lut=[]{
        std::vector<uint16_t> pixels(size_t(bandWidth)*brdfHeight*4u);
        for(int y=0;y<brdfSize;++y){
            const float rough=(y+.5f)/brdfSize;
            const auto vectors=brdfHalfVectors(rough);
            for(int x=0;x<brdfSize;++x){
                const auto ab=integrateBrdf((x+.5f)/brdfSize,rough,vectors);
                const size_t i=(size_t(y+1)*bandWidth+x+1)*4;
                pixels[i]=hdr_detail::toHalf(ab[0]);pixels[i+1]=hdr_detail::toHalf(ab[1]);
                pixels[i+2]=0;pixels[i+3]=0x3c00u;
            }
        }
        auto copy=[&](int dx,int dy,int sx,int sy){
            const size_t d=(size_t(dy)*bandWidth+dx)*4,s=(size_t(sy)*bandWidth+sx)*4;
            for(size_t c=0;c<4;++c)pixels[d+c]=pixels[s+c];
        };
        for(int y=1;y<=brdfSize;++y){copy(0,y,1,y);copy(brdfSize+1,y,brdfSize,y);}
        for(int x=0;x<bandWidth;++x){copy(x,0,x,1);copy(x,brdfSize+1,x,brdfSize);}
        return pixels;
    }();
    return lut;
}
// Full-sphere UV domain; box-filter V into a 2:1 lat-long image.
// Lighting uses nonnegative radiance and ignores material alpha.
inline RadianceImage fromGraphLatLong(int side,std::span<const float> rgba){
    if(side<8||side>1024||side%2||rgba.size()!=size_t(side)*side*4u)
        throw std::invalid_argument("Invalid square HDR graph environment");
    RadianceImage result;result.width=side;result.height=side/2;
    result.rgba16f.resize(size_t(result.width)*result.height*4u);
    for(int y=0;y<result.height;++y)for(int x=0;x<side;++x){
        const auto a=(size_t(2*y)*side+x)*4u,b=a+size_t(side)*4u,o=(size_t(y)*side+x)*4u;
        for(size_t c=0;c<3;++c){
            const float v=rgba[a+c],w=rgba[b+c];
            if(!std::isfinite(v)||!std::isfinite(w)||std::abs(v)>65504.f||std::abs(w)>65504.f)
                throw std::invalid_argument("Invalid HDR environment radiance");
            result.rgba16f[o+c]=hdr_detail::toHalf((std::max(0.f,v)+std::max(0.f,w))*.5f);
        }
        result.rgba16f[o+3]=0x3c00u;
    }
    return result;
}
inline RadianceImage convolve(const RadianceImage& source){
    if(source.width<2||source.height<1||source.width!=2*source.height||
       source.width>8192||source.rgba16f.size()!=size_t(source.width)*source.height*4)
        throw std::invalid_argument("HDR environment must be a bounded 2:1 lat-long image");
    RadianceImage result;result.width=bandWidth;result.height=bandHeight*bands;
    result.rgba16f.resize(size_t(result.width)*result.height*4);
    // Precompute tangent-space samples once per band; no per-texel trig
    // for the sampling distribution. Normalize NdotL-weighted radiance.
    std::array<std::vector<WeightedDirection>,bands> kernels;
    for(int band=1;band<bands;++band){
        const int count=band==4?diffuseSamples:specularSamples;
        const float rough=float(band)/3.f;
        auto& kernel=kernels[size_t(band)];kernel.reserve(size_t(count));
        float weight=0.f;
        for(int i=0;i<count;++i){
            const float u=(i+.5f)/float(count),v=radicalInverse(uint32_t(i));
            auto q=band==4?diffuseDirection(u,v):ggxDirection(rough,u,v);
            if(q.weight>0.f){weight+=q.weight;kernel.push_back(q);}
        }
        for(auto& q:kernel)q.weight/=weight;
    }
    for(int band=0;band<bands;++band)for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const Vec n=direction((x+.5f)/width,(y+.5f)/height);
        Vec sum{};
        if(band==0)sum=sample(source,n);
        else{
            const Vec t=unit(cross(std::abs(n.y)<.99f?Vec{0,1,0}:Vec{1,0,0},n)),b=cross(n,t);
            for(const auto& q:kernels[size_t(band)]){
                const auto l=q.direction;
                sum=sum+sample(source,unit(t*l.x+b*l.y+n*l.z))*q.weight;
            }
        }
        const size_t i=(size_t(band*bandHeight+y+1)*bandWidth+x+1)*4;
        result.rgba16f[i]=hdr_detail::toHalf(sum.x);result.rgba16f[i+1]=hdr_detail::toHalf(sum.y);result.rgba16f[i+2]=hdr_detail::toHalf(sum.z);result.rgba16f[i+3]=0x3c00u;
    }
    // Duplicate wrap columns and clamp pole rows to isolate each filtered band.
    auto copy=[&](int dx,int dy,int sx,int sy){
        const size_t d=(size_t(dy)*bandWidth+dx)*4,s=(size_t(sy)*bandWidth+sx)*4;
        for(int k=0;k<4;++k)result.rgba16f[d+k]=result.rgba16f[s+k];
    };
    for(int band=0;band<bands;++band){
        const int off=band*bandHeight;
        for(int y=1;y<=height;++y){copy(0,off+y,width,off+y);copy(width+1,off+y,1,off+y);}
        for(int x=0;x<bandWidth;++x){copy(x,off,x,off+1);copy(x,off+height+1,x,off+height);}
    }
    return result;
}
// Keep legacy convolve() contract intact for native consumers. Studio opts
// into the extended atlas; older borrowed atlases retain analytic BRDF.
inline RadianceImage convolveWithBrdf(const RadianceImage& source){
    auto atlas=convolve(source);
    const auto& lut=brdfLut();
    atlas.rgba16f.insert(atlas.rgba16f.end(),lut.begin(),lut.end());
    atlas.height=combinedHeight;return atlas;
}
} // namespace aiverse::render::environment_ibl
