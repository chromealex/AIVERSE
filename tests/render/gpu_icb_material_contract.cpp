// Portable C++20 contract checks for experimental Metal ICB material staging.
// Does not require a Metal device: validates the real RHI / renderer headers.
#include "engine/render/GpuMaterialResourceTable.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/render/ModelShaders.h"
#include <cstdio>
#include <memory>
#include <limits>

using namespace aiverse;
namespace {
int failures=0;
void check(bool ok,const char* what) {
    if (!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",what); }
}
render::GpuMaterialResourceBinding binding(unsigned root=10) {
    render::GpuMaterialResourceBinding b;
    b.pbrEnabled=true;
    for(unsigned i=0;i<6;++i) b.pbrTextures[i]={root+i};
    for(unsigned i=0;i<36;++i) b.pbrU2[i]=float(i)/36.0f;
    b.graphTextureCount=4;
    for(unsigned i=0;i<4;++i) b.graphTextures[i]={root+20+i};
    b.virtualTextureEnabled=true;
    b.virtualTexture.physicalAtlas={root+30};
    b.virtualTexture.pageTable={root+31};
    b.virtualTexture.layer=2;
    b.virtualTexture.gutterPixels=2.0f;
    b.virtualTexture.addressU=gfx::SamplerAddressMode::Repeat;
    b.virtualTexture.addressV=gfx::SamplerAddressMode::MirrorRepeat;
    return b;
}
} // namespace

int main() {
    render::GpuMaterialResourceTable table(2);
    auto a=binding();
    check(a.valid(),"bounded 4-source PBR+VT material valid");
    check(!table.intern(a).valid(),"unstarted table rejects");
    table.beginFrame(42);
    const auto ticket=table.intern(a);
    check(ticket.valid() && ticket.frameSerial==42,"staged generation ticket");
    check(ticket==table.intern(a) && table.size()==1,"exact deduplication");
    render::GpuMaterialResourceBinding snapshot;
    check(table.resolve(ticket,snapshot) && snapshot==a,"snapshot round trip");
    check(table.matches(ticket,a),"exact staged match");
    check(table.referencesTexture({10})&&table.referencesTexture({30})&&table.referencesTexture({40})&&table.referencesTexture({41}),"raw replay texture roots");
    check(!table.referencesTexture({})&&!table.referencesTexture({999}),"invalid or unrelated handle not a root");
    auto changed=a;
    changed.pbrU2[7]+=0.25f;
    check(!table.matches(ticket,changed),"changed U2 rejects old ticket");
    const auto t2=table.intern(changed);
    check(t2.valid() && t2.index!=ticket.index,"different U2 uses new entry");
    check(!table.intern(binding(100)).valid(),"table capacity fails closed");
    changed=a; changed.graphTextures[3]={99};
    check(!table.matches(ticket,changed),"graph tex16 change invalidates");
    changed=a; changed.virtualTexture.pageTable={98};
    check(!table.matches(ticket,changed),"VT table promotion invalidates");
    changed=a; changed.virtualTexture.addressV=gfx::SamplerAddressMode::ClampToEdge;
    check(!table.matches(ticket,changed),"V-address mode invalidates");
    changed=a; changed.pbrTextures[0]={98};
    check(!table.matches(ticket,changed),"PBR source swap invalidates");
    changed=a; changed.graphTextureCount=5;
    check(!changed.valid() && !table.intern(changed).valid(),"5 graph textures rejected");
    changed=a; changed.graphTextures[3]={};
    check(!changed.valid(),"missing graph source rejected");
    changed=a; changed.graphTextureCount=3;
    check(!changed.valid(),"nonzero unused graph source rejected");
    changed=a; changed.pbrEnabled=false;
    check(!changed.valid(),"graph sources without U2/PBR rejected");
    changed=a; changed.pbrU2[1]=std::numeric_limits<float>::quiet_NaN();
    check(!changed.valid(),"nonfinite U2 rejected");
    table.beginFrame(42);
    check(!table.matches(ticket,a),"generation rejects same-frame serial reuse");
    check(!table.resolve(ticket,snapshot),"stale snapshot fails resolve");
    const auto fresh=table.intern(a);
    check(fresh.valid() && fresh.generation!=ticket.generation,"new generation ticket");
    table.reset();
    check(!table.matches(fresh,a),"shutdown resets tickets");
    check(!table.referencesTexture({10}),"detached generation has no raw texture roots");

    gfx::GpuIcbVirtualMaterial material;
    material.physicalAtlas={1};material.pageTable={2};
    material.params[0]=0;material.params[1]=2;
    material.hasPbr=true;
    for(unsigned i=0;i<6;++i)material.pbrTextures[i]={10+i};
    material.graphTextureCount=4;
    for(unsigned i=0;i<4;++i)material.graphTextures[i]={20+i};
    check(material.valid(),"ICB VT graph descriptor valid");
    material.graphTextureCount=3;
    check(!material.valid(),"VT source tail cannot alias");
    material.graphTextureCount=4;
    material.params[2]=3;
    check(!material.valid(),"unsupported address mode rejected");
    gfx::GpuIcbPbrMaterial plain;
    for(unsigned i=0;i<6;++i)plain.textures[i]={10+i};
    plain.graphTextureCount=4;
    for(unsigned i=0;i<4;++i)plain.graphTextures[i]={20+i};
    check(plain.valid(),"ICB graph without VT valid");
    plain.graphTextureCount=5;
    check(!plain.valid(),"ICB graph budget enforced");
    plain.graphTextureCount=4;
    plain.graphTextures[3]={};
    check(!plain.valid(),"ICB missing source rejected");

    // Identical raw indices are not enough to bind a tracked graph image.
    render::Model graphModel;
    graphModel.images.resize(2);
    auto& image=graphModel.images[0];
    image.tex={101};image.virtualAlphaMask={102};image.proceduralGraphImage=true;
    image.proceduralGraphPin=std::make_shared<const render::ProceduralImagePin>();
    graphModel.images[1].tex={201};
    render::Material liveMaterial;liveMaterial.baseTex=0;
    liveMaterial.proceduralGraphPins=render::withGraphMaterialPin({},0,image.proceduralGraphPin);
    auto staleMaterial=liveMaterial;
    staleMaterial.proceduralGraphPins=render::withGraphMaterialPin({},0,std::make_shared<const render::ProceduralImagePin>());
    check(render::resolvedMaterialTexture(graphModel,liveMaterial,&render::Material::baseTex).id==101,"current pin resolves texture");
    check(render::resolvedMaterialTexture(graphModel,staleMaterial,&render::Material::baseTex,{99}).id==99,"wrong pin uses fallback");
    check(!render::resolvedMaterialAlphaCoverage(graphModel,staleMaterial).valid(),"wrong pin rejects alpha");
    check(!render::equivalentGpuMaterial(graphModel,liveMaterial,graphModel,staleMaterial),"batch equivalence checks generation");
    check(render::gpuMaterialStateHash(graphModel,liveMaterial)!=render::gpuMaterialStateHash(graphModel,staleMaterial),"batch hash uses resolved textures");
    liveMaterial.graphTextures[0]=0;
    check(!render::resolvedGraphSourceTexture(graphModel,liveMaterial,0).valid(),"untracked shader sampler cannot sample tracked image");
    liveMaterial.graphTextures[0]=1;
    check(render::resolvedGraphSourceTexture(graphModel,liveMaterial,0).id==201,"imported shader sampler unchanged");
    auto imported=staleMaterial;imported.baseTex=1;
    check(render::resolvedMaterialTexture(graphModel,imported,&render::Material::baseTex).id==201,"ordinary index overrides preserve compatibility");
    image.proceduralGraphRetired=true;
    check(!render::resolvedMaterialTexture(graphModel,liveMaterial,&render::Material::baseTex).valid(),"tombstone rejects old pin");
    image.proceduralGraphRetired=false;image.proceduralGraphPin=std::make_shared<const render::ProceduralImagePin>();
    check(!render::resolvedMaterialTexture(graphModel,liveMaterial,&render::Material::baseTex).valid(),"recycled same index and handle rejects old pin");

    render::Material m1,m2;
    check(render::equivalentGpuIcbPbrPipelineMaterial(m1,m2),
          "same opaque PBR pipeline");
    m2.normalTex=0;
    check(!render::equivalentGpuIcbPbrPipelineMaterial(m1,m2),
          "normalmap permutation differs");
    m2=m1;m2.alpha=render::AlphaMode::Mask;
    check(!render::equivalentGpuIcbPbrPipelineMaterial(m1,m2),
          "masked PBR not mixed");
    m2=m1;
    auto g1=std::make_shared<render::graph::Program>();
    g1->domain="material";g1->textures={"sample_a","sample_b"};
    g1->msl="float4 sg_n0=float4(1);";
    g1->glsl="vec4 sg_n0=vec4(1);";
    g1->outputs["color"]="sg_n0";
    m1.shaderGraph=g1;
    auto g2=std::make_shared<render::graph::Program>(*g1);
    g2->textures={"sample_c","sample_d"};
    m2.shaderGraph=g2;
    check(render::gpuIcbFragmentGraphEligible(m1),"fragment-only graph eligible");
    check(render::equivalentGpuIcbGraphPipelineMaterial(m1,m2),
          "different source paths with same code are compatible");
    check(render::gpuIcbGraphPipelineHash(m1)==
          render::gpuIcbGraphPipelineHash(m2),
          "equal pipelines must hash equal");
    g2->msl="different fragment shader";
    check(!render::equivalentGpuIcbGraphPipelineMaterial(m1,m2),
          "different fragment MSL code rejected");
    g2->msl=g1->msl;g2->vertexMsl="vertex deform";
    check(!render::gpuIcbFragmentGraphEligible(m2),
          "graph vertex modifications stay on CPU path");

    // Check the actual MSL fragment argument-buffer source, rather than
    // duplicating the GPU slot constants in a separate test-only table.
    const std::string shader=render::shaders::kModelMSL;
    check(shader.find("texture2d<float> tex16 [[id(32)]];") !=
          std::string::npos,"fourth Shader Graph source at texture id32");
    check(shader.find("sampler s16 [[id(33)]];") == std::string::npos,
          "source tex16 must use constexpr sampler, not sampler slot16");
    for (int n=0;n<=4;++n) {
        const std::string branch=(n==0 ? "#if " : "#elif ") +
            std::string("AIVERSE_SG_TEXTURE_COUNT == ") + std::to_string(n);
        const auto at=shader.find(branch);
        check(at!=std::string::npos,"all graph source count variants exist");
        if (at==std::string::npos) continue;
        const int atlas=13+n, tableSlot=14+n;
        const std::string atlasDecl="texture2d_array<float, access::sample> tex"+
            std::to_string(atlas)+" [[id("+std::to_string(2*atlas)+")]];";
        const std::string tableDecl="texture2d_array<float, access::read> tex"+
            std::to_string(tableSlot)+" [[id("+std::to_string(2*tableSlot)+")]];";
        const auto segment=shader.substr(at,240);
        check(segment.find(atlasDecl)!=std::string::npos,
              "graph atlas follows source texture slots");
        check(segment.find(tableDecl)!=std::string::npos,
              "graph page table follows atlas slot");
    }

    if(failures) std::fprintf(stderr,"%d GPU ICB material checks failed\n",failures);
    return failures?1:0;
}
