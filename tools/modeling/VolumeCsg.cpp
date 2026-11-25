#include "Modeling.h"
#include "engine/world/TerraWeaveCavernsMesh.h"
#include <aiverselib/algorithms/Noise.h>
#include <array>
#include <cmath>

namespace aiverse::modeling {
namespace {
enum class Kind { Sphere, Box, Cylinder, Density, Noise, Union, Subtract, Intersect };
struct Field {
    Kind kind{};
    float3 center{}, size{1,1,1};
    float radius=.5f, frequency=1, amplitude=1, threshold=.5f;
    int left=-1, right=-1, dimension=0, seed=0;
    std::vector<uint8_t> texels;
};

int integer(const Json& value, int fallback, int min, int max, const char* label) {
    if (value.isNull()) return fallback;
    const double d=value.toDouble(double(min)-1);
    require(std::isfinite(d) && std::floor(d)==d && d>=min && d<=max, label);
    return int(d);
}

std::vector<Field> compileFields(const Json& source) {
    const auto& nodes=source["fields"];
    require(nodes.isArray() && nodes.size()>0 && nodes.size()<=48,"volume needs 1..48 fields");
    std::map<std::string,int> ids;
    std::vector<Field> fields;
    const std::map<std::string,Kind> kinds={{"sphere",Kind::Sphere},{"box",Kind::Box},
        {"cylinder",Kind::Cylinder},{"densityTexture",Kind::Density},{"noise",Kind::Noise},
        {"union",Kind::Union},{"subtract",Kind::Subtract},{"intersect",Kind::Intersect}};
    size_t texelCount=0;
    for (const auto& row : nodes.items()) {
        const std::string id=row.getStr("id");
        require(!id.empty() && id.size()<=96 && !ids.count(id),"duplicate or empty volume field id");
        const auto kind=kinds.find(row.getStr("op"));
        require(kind!=kinds.end(),"unknown volume field operation");
        Field f; f.kind=kind->second;
        auto operand=[&](const char* name) {
            const auto it=ids.find(row.getStr(name));
            require(it!=ids.end(),id+": "+name+" must reference an earlier field");
            return it->second;
        };
        if (f.kind==Kind::Union || f.kind==Kind::Subtract || f.kind==Kind::Intersect) {
            f.left=operand("a"); f.right=operand("b");
        } else {
            f.center=vector3(row["center"],{},id);
            f.size=vector3(row["size"],{1,1,1},id);
            require(std::min({f.size.x,f.size.y,f.size.z})>0,id+": size must be positive");
            if (f.kind==Kind::Sphere || f.kind==Kind::Cylinder) {
                f.radius=number(row["radius"],.5f,id);
                require(f.radius>0,id+": radius must be positive");
            }
            if (f.kind==Kind::Noise) {
                if (row.has("input")) f.left=operand("input");
                f.frequency=number(row["frequency"],1,id);
                f.amplitude=number(row["amplitude"],.15f,id);
                f.seed=integer(row["seed"],0,0,1000000,"invalid volume noise seed");
                require(f.frequency>0 && f.frequency<=128,id+": frequency outside (0,128]");
            }
            if (f.kind==Kind::Density) {
                f.dimension=integer(row["dimension"],0,2,64,"invalid density texture dimension");
                const auto& data=row["texels"];
                const size_t total=size_t(f.dimension)*f.dimension*f.dimension;
                texelCount+=total;
                require(f.dimension>=2 && data.isArray() && data.size()==total && texelCount<=262144,
                        id+": density texture size or budget mismatch");
                f.threshold=number(row["threshold"],.5f,id);
                require(f.threshold>=0 && f.threshold<=1,id+": threshold outside [0,1]");
                f.texels.reserve(total);
                for (const auto& v:data.items()) {
                    require(v.isNumber(),"density texel must be an integer byte");
                    f.texels.push_back(uint8_t(integer(v,0,0,255,"invalid density texel")));
                }
            }
        }
        ids.emplace(id,int(fields.size()));
        fields.push_back(std::move(f));
    }
    require(nodes[nodes.size()-1].getStr("id")==source.getStr("root"),"volume root must be the final field");
    return fields;
}

template<class Samples>
float interpolate(const Samples& samples,int side,float x,float y,float z) {
    x=std::clamp(x,0.f,float(side-1)); y=std::clamp(y,0.f,float(side-1)); z=std::clamp(z,0.f,float(side-1));
    const int i=std::min(int(x),side-2),j=std::min(int(y),side-2),k=std::min(int(z),side-2);
    auto row=[&](int yy,int zz) {
        const size_t at=(size_t(zz)*side+yy)*side+i;
        return std::lerp(float(samples[at]),float(samples[at+1]),x-i);
    };
    return std::lerp(std::lerp(row(j,k),row(j+1,k),y-j),std::lerp(row(j,k+1),row(j+1,k+1),y-j),z-k);
}

float sample(const std::vector<Field>& fields,float3 p) {
    std::array<float,48> values{};
    for (size_t i=0;i<fields.size();++i) {
        const auto& f=fields[i];
        const float3 q=p-f.center;
        switch (f.kind) {
        case Kind::Sphere: values[i]=f.radius-length(q);break;
        case Kind::Box:
            values[i]=std::min({.5f*f.size.x-std::abs(q.x),.5f*f.size.y-std::abs(q.y),.5f*f.size.z-std::abs(q.z)});break;
        case Kind::Cylinder:
            values[i]=std::min(f.radius-std::hypot(q.x,q.z),.5f*f.size.y-std::abs(q.y));break;
        case Kind::Union: values[i]=std::max(values[f.left],values[f.right]);break;
        case Kind::Intersect: values[i]=std::min(values[f.left],values[f.right]);break;
        case Kind::Subtract: values[i]=std::min(values[f.left],-values[f.right]);break;
        case Kind::Density: {
            const float3 uv{q.x/f.size.x+.5f,q.y/f.size.y+.5f,q.z/f.size.z+.5f};
            const float boundary=std::min({uv.x,uv.y,uv.z,1-uv.x,1-uv.y,1-uv.z});
            const float scale=float(f.dimension-1);
            values[i]=std::min(boundary,interpolate(f.texels,f.dimension,uv.x*scale,uv.y*scale,uv.z*scale)/255.f-f.threshold);
            break;
        }
        case Kind::Noise: {
            using namespace lib::algorithms;
            auto coord=[&](float v){return std::llround(double(v)*f.frequency*noiseOne);};
            const auto v=periodicValueNoise<3>({coord(q.x),coord(q.y),coord(q.z)},uint64_t(f.seed));
            require(v.has_value(),"noise sample outside coordinate range");
            values[i]=(f.left>=0?values[f.left]:0.f)+f.amplitude*float(*v)/float(noiseOne);
            break;
        }
        }
    }
    return values[fields.size()-1];
}
}

Mesh volumeCsgGeometry(const Json& source,const std::atomic<bool>* cancel) {
    const auto fields=compileFields(source);
    const int n=integer(source["resolution"],16,4,24,"volume resolution must be an integer in [4,24]");
    const float3 lo=vector3(source["min"],{-1,-1,-1},"volume min");
    const float3 hi=vector3(source["max"],{1,1,1},"volume max");
    const float3 step=(hi-lo)*(1.f/n);
    require(std::min({step.x,step.y,step.z})>1e-5f,"volume extent is empty or too small");
    const int side=n+1;
    std::vector<float> grid(size_t(side)*side*side);
    auto checkCancel=[&] {if(cancel && cancel->load())throw std::runtime_error("build cancelled");};
    for(int z=0;z<=n;++z) {
        checkCancel();
        for(int y=0;y<=n;++y)for(int x=0;x<=n;++x) {
            const float v=sample(fields,{lo.x+x*step.x,lo.y+y*step.y,lo.z+z*step.z});
            // Close the extraction domain when the authored field extends beyond it.
            const bool boundary=!x||!y||!z||x==n||y==n||z==n;
            grid[(size_t(z)*side+y)*side+x]=boundary?std::min(0.f,v):v;
        }
    }
    namespace caverns=terraweave::caverns;
    const auto surface=caverns::generateCaveMeshSampled({}, {},uint32_t(n),100,[&](caverns::PointCm q) {
        checkCancel();
        return int64_t(std::llround(double(interpolate(grid,side,float(q.x)*.01f,float(q.y)*.01f,float(q.z)*.01f))*1000000.));
    });
    Mesh mesh;
    mesh.indices=surface.indices;
    mesh.vertices.reserve(surface.vertices.size());
    for(const auto& v:surface.vertices) {
        auto& out=mesh.vertices.emplace_back();
        out.pos={lo.x+v.x*step.x,lo.y+v.y*step.y,lo.z+v.z*step.z};
        out.nrm=normalize({v.nx/step.x,v.ny/step.y,v.nz/step.z});
        out.uv={v.x/n,v.z/n};
    }
    return mesh;
}
} // namespace aiverse::modeling
