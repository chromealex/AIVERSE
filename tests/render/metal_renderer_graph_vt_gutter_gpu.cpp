// Real production Renderer3D + compiled Shader Graph + nonuniform guttered VT Metal ICB.
// This is deliberately distinct from metal_icb_graph_gpu.mm's minimal MSL ABI fixture.
// The opt-in CTest must compile an authored JSON graph through render::graph::compile,
// render the same two material regions through the real Renderer3D direct and GPU-ICB
// paths, and prove both pixel parity and actual mixed-graph GPU ICB submissions.
#include "engine/render/Renderer3D.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/platform/Platform.h"
#include "engine/gfx/VirtualTextureFileStreamer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
constexpr int kWidth=192, kHeight=128;
bool failed=false, finished=false;
void check(bool condition,const char* what) {
    if (!condition) { failed=true; std::fprintf(stderr,"FAIL: %s\n",what); }
}
bool optIn(const char* name) {
    const char* value=std::getenv(name);
    return value && std::string(value)=="1";
}
render::graph::Program compileAuthoredGraph() {
    // Four real, connected sampleTexture nodes; tex16 is sampled, not merely
    // declared. This forces Renderer3D's production MSL injection code.
    const Json json=Json::parse(R"JSON({
      "version":1,"domain":"material","nodes":[
        {"id":"uv","op":"uv"},
        {"id":"t0","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/0.png"},
        {"id":"t1","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/1.png"},
        {"id":"t2","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/2.png"},
        {"id":"t3","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/3.png"},
        {"id":"a","op":"add","inputs":["t0","t1"]},
        {"id":"b","op":"add","inputs":["t2","t3"]},
        {"id":"sum","op":"add","inputs":["a","b"]},
        {"id":"quarter","op":"constant","value":0.015},
        {"id":"colour","op":"multiply","inputs":["sum","quarter"]},
        {"id":"vt","op":"sampleVirtualTexture","inputs":["uv"]},
        {"id":"vtWeight","op":"constant","value":0.85},
        {"id":"vtColour","op":"multiply","inputs":["vt","vtWeight"]},
        {"id":"final","op":"add","inputs":["colour","vtColour"]}
      ],"outputs":{"color":"final"}
    })JSON");
    return render::graph::compile(json,"material");
}
std::unique_ptr<render::Model> makeScene(const std::shared_ptr<const render::graph::Program>& graph) {
    auto m=std::make_unique<render::Model>();
    m->path="metal-renderer-graph-vt-gutter-fixture";
    m->batching.autoLod=false;
    // 8 separate source images: the same 4-sampler compiled graph uses
    // independent textures on each material, not shared static bindings.
    const std::array<std::array<uint8_t,4>,8> colors{{
        {{160,25,15,255}},{{120,35,35,255}},
        {{150,30,55,255}},{{170,20,35,255}},
        {{20,140,55,255}},{{25,160,60,255}},
        {{40,145,50,255}},{{35,155,70,255}}
    }};
    for (size_t i=0;i<colors.size();++i) {
        render::Image image;
        image.name="graph-source-"+std::to_string(i);
        image.width=image.height=2;
        image.repeat=true;
        image.nearest=false; // graph source slot16 requires repeat/linear
        for(int pixel=0;pixel<4;++pixel)
            image.rgba.insert(image.rgba.end(),colors[i].begin(),colors[i].end());
        m->images.push_back(std::move(image));
    }
    m->materials.resize(2);
    for(int side=0;side<2;++side) {
        auto& material=m->materials[size_t(side)];
        material.shaderGraph=graph;
        material.name=side?"vt-right":"vt-left";
        material.shading=render::ShadingModel::Unlit;
        material.doubleSided=true;
        material.alpha=render::AlphaMode::Opaque;
        material.castShadow=false;
        material.uvScale={0.0001f,0.0001f};
        material.uvOffset={0.4f,0.4f};
        material.baseColor=side?render::float4{.8f,1,1,1}:render::float4{1,1,1,1};
        for(int slot=0;slot<4;++slot)material.graphTextures[slot]=side*4+slot;
    }
    render::MeshDef mesh;
    mesh.name="split-material-quads";
    for(int side=0;side<2;++side) {
        const float x=side ? .8f : -.8f;
        const uint32_t firstVertex=uint32_t(m->vertices.size());
        for(const auto& local:std::array<render::float3,4>{{
                {-0.72f,-0.82f,0.f},{0.72f,-0.82f,0.f},
                {-0.72f,0.82f,0.f},{0.72f,0.82f,0.f}}}) {
            render::ModelVertex v;
            v.pos={x+local.x,local.y,0};
            v.nrm={0,0,1};
            v.uv={(local.x+0.72f)/1.44f,(local.y+0.82f)/1.64f};
            m->vertices.push_back(v);
        }
        const uint32_t firstIndex=uint32_t(m->indices.size());
        m->indices.insert(m->indices.end(),{
            firstVertex,firstVertex+1,firstVertex+2,
            firstVertex+1,firstVertex+3,firstVertex+2
        });
        render::Primitive primitive;
        primitive.material=side;
        primitive.firstIndex=firstIndex;
        primitive.indexCount=6;
        primitive.firstVertex=firstVertex;
        primitive.vertexCount=4;
        primitive.boundsMin={x-.72f,-.82f,0};
        primitive.boundsMax={x+.72f,.82f,0};
        mesh.primitives.push_back(primitive);
    }
    mesh.boundsMin={-1.52f,-.82f,0};
    mesh.boundsMax={1.52f,.82f,0};
    m->meshes.push_back(std::move(mesh));
    render::Node node;
    node.mesh=0;
    node.name="two-material-fixture";
    m->nodes.push_back(std::move(node));
    m->finalize();
    return m;
}
bool differentRegions(const std::vector<uint8_t>& rgba) {
    if (rgba.size()!=size_t(kWidth*kHeight*4)) return false;
    // The two interior regions must actually be rendered and must differ
    // chromatically. A blank frame or both draws inheriting one texture
    // cannot pass merely by being identical across modes.
    auto average=[&](int x0,int x1) {
        std::array<double,3> sum{};
        for(int y=kHeight/3;y<2*kHeight/3;++y)
            for(int x=x0;x<x1;++x)
                for(int channel=0;channel<3;++channel)
                    sum[size_t(channel)]+=rgba[(size_t(y*kWidth+x)*4)+size_t(channel)];
        const double count=double((2*kHeight/3-kHeight/3)*(x1-x0));
        for(auto& v:sum)v/=count;
        return sum;
    };
    const auto left=average(kWidth/5,2*kWidth/5);
    const auto right=average(3*kWidth/5,4*kWidth/5);
    const double delta=std::abs(left[0]-right[0])+
                       std::abs(left[1]-right[1])+
                       std::abs(left[2]-right[2]);
    std::printf("Renderer3D Shader Graph regions: left=(%.1f,%.1f,%.1f) "
                "right=(%.1f,%.1f,%.1f) delta=%.1f\n",
                left[0],left[1],left[2],right[0],right[1],right[2],delta);
    // This fixture uses nearby nonuniform RGB gradients, rather than the
    // saturated solid colors of the older scene test. Its independently
    // checked per-material source/ROI gradient gives stronger nonblank
    // evidence, while a genuine inter-material delta of 16..19 is expected.
    return delta>12.0; // still reject blank/identical material regions
}
void compareCaptured(const std::vector<uint8_t>& ordinary,
                     const std::vector<uint8_t>& icb,const char* phase) {
    check(ordinary.size()==size_t(kWidth*kHeight*4) &&
          ordinary.size()==icb.size(),"two valid full-screen capture buffers");
    if (ordinary.size()!=icb.size() || ordinary.empty()) return;
    check(differentRegions(ordinary),"compiled Shader Graph draws distinct regions");
    check(differentRegions(icb),"GPU ICB draws distinct graph-source regions");
    size_t bad=0;int maxDelta=0;
    // Allow only two 8-bit levels for shader/UNORM rounding.
    for(size_t i=0;i<ordinary.size();++i) {
        const int diff=std::abs(int(ordinary[i])-int(icb[i]));
        maxDelta=std::max(maxDelta,diff);
        if(diff>2)++bad;
    }
    std::printf("Renderer3D graph CPU/ICB pixels: bad_channels=%zu max_delta=%d\n",
                bad,maxDelta);
    check(bad==0,"full production graph Renderer3D CPU/ICB capture parity");
    if(!failed)std::printf("PASS: Shader Graph+VT %s Renderer3D CPU/ICB pixels\n",phase);
}

// Only synthetic, trusted 4x4 raw RGBA8 pages live in this test's own
// temporary directory. No assetpack path and no production game files.

constexpr int kPage=8,kGutter=1,kInterior=6,kWidthLogical=12,kHeightLogical=6;
constexpr std::array<float,5> kSampleU{{0.40f,0.48f,0.50f,0.52f,0.60f}};
using Pixel=std::array<uint8_t,4>;

// This is an immutable full-resolution logical source generator, not an
// implementation of the shader's page lookup or physical inset formula.
Pixel sourceTexel(int x,int y,int side) {
    x=std::clamp(x,0,kWidthLogical-1);
    y=std::clamp(y,0,kHeightLogical-1);
    return {
        uint8_t(22+12*x+2*y+side*18),
        uint8_t(224-11*x-4*y-side*8),
        uint8_t(38+8*x+3*y+side*12),
        255
    };
}
float logicalBilinear(int channel,int side,float u,float v) {
    const float fx=std::clamp(u,0.f,1.f)*kWidthLogical-.5f;
    const float fy=std::clamp(v,0.f,1.f)*kHeightLogical-.5f;
    const int x=int(std::floor(fx)),y=int(std::floor(fy));
    const float a=fx-float(x),b=fy-float(y);
    const auto a00=sourceTexel(x,y,side)[size_t(channel)];
    const auto a10=sourceTexel(x+1,y,side)[size_t(channel)];
    const auto a01=sourceTexel(x,y+1,side)[size_t(channel)];
    const auto a11=sourceTexel(x+1,y+1,side)[size_t(channel)];
    const float low=float(a00)*(1-a)+float(a10)*a;
    const float high=float(a01)*(1-a)+float(a11)*a;
    return low*(1-b)+high*b;
}
struct Fixture {
    std::filesystem::path root;
    bool ready=false;
    Fixture() {
        std::error_code error;
        const auto dir=std::filesystem::temp_directory_path(error);
        if(error)return;
        root=dir/("aiverse-graph-vt-gutter-"+
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ready=std::filesystem::create_directory(root,error)&&!error;
    }
    ~Fixture() {
        if(ready) {
            std::error_code error;
            std::filesystem::remove_all(root,error);
        }
    }
    bool write(int side,int tile) const {
        if(!ready||side<0||side>1||tile<0||tile>1)return false;
        const gfx::VirtualPageKey key{0,tile,0,side};
        const auto path=root/(side==0?"left":"right")/
                        gfx::VirtualTextureDiskQueue::relativePath(key);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(),error);
        if(error)return false;
        std::array<uint8_t,kPage*kPage*4> bytes{};
        for(int y=0;y<kPage;++y)
            for(int x=0;x<kPage;++x) {
                // The one-pixel gutters come from the adjacent original
                // logical texels, NOT from repeated edge texels.
                const auto pixel=sourceTexel(tile*kInterior+x-kGutter,
                                             y-kGutter,side);
                const auto at=bytes.begin()+std::ptrdiff_t((y*kPage+x)*4);
                std::copy(pixel.begin(),pixel.end(),at);
            }
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  std::streamsize(bytes.size()));
        out.close();
        return bool(out);
    }
};
struct Capture {
    std::vector<uint8_t> pixels;
    bool requested=false,ready=false;
};
std::array<double,3> meanRegion(const std::vector<uint8_t>& rgba,int side) {
    std::array<double,3> sums{};
    if(rgba.size()!=size_t(kWidth*kHeight*4)) {
        check(false,"full RGBA8 image needed for original-source CPU oracle");
        return sums;
    }
    const int begin=side==0?kWidth/5:3*kWidth/5;
    const int end=side==0?2*kWidth/5:4*kWidth/5;
    const int minY=kHeight/3,maxY=2*kHeight/3;
    for(int y=minY;y<maxY;++y)
        for(int x=begin;x<end;++x)
            for(int c=0;c<3;++c)
                sums[size_t(c)]+=rgba[(size_t(y*kWidth+x)*4)+size_t(c)];
    const double count=double((end-begin)*(maxY-minY));
    for(double& v:sums)v/=count;
    return sums;
}
// Use sourceTexel's *full logical-resolution* bilinear values to locate
// the required left/right scene ROIs at each U and to interpolate
// expected values between two observed endpoints. Endpoint calibration
// eliminates the scene lighting/gamma transfer function; neither the
// GPU physical page table nor shader tile inset is part of this oracle.
// Pixel tolerances account for small nonlinear gamma changes.
void independentSceneOracle(const std::array<Capture,10>& captures) {
    for(int side=0;side<2;++side)
        for(int mode=0;mode<2;++mode) {
            const auto get=[&](int sample) {
                return meanRegion(captures[size_t(sample*2+mode)].pixels,side);
            };
            const auto first=get(0),last=get(4);
            const auto intermediateA=get(1),midpoint=get(2),intermediateB=get(3);
            const std::array<std::array<double,3>,3> observed{
                intermediateA,midpoint,intermediateB
            };
            for(int c=0;c<3;++c) {
                const double originalLow=logicalBilinear(c,side,kSampleU[0],.4f);
                const double originalHigh=logicalBilinear(c,side,kSampleU[4],.4f);
                const double originalRange=originalHigh-originalLow;
                const double sceneRange=last[size_t(c)]-first[size_t(c)];
                // Strong, nonuniform source pattern and correctly colored
                // quads are mandatory; a constant frame must not pass.
                check(std::abs(originalRange)>12,
                      "independent original logical texels have a seam gradient");
                check(sceneRange*originalRange>0 && std::abs(sceneRange)>4.0,
                      "Renderer3D scene ROI changes in original-source channel direction");
                if(std::abs(originalRange)<1e-4)continue;
                for(int j=0;j<3;++j) {
                    const int sample=j+1;
                    const double originalValue=logicalBilinear(
                        c,side,kSampleU[size_t(sample)],.4f);
                    const double fraction=(originalValue-originalLow)/originalRange;
                    const double expected=first[size_t(c)]+sceneRange*fraction;
                    const double actual=observed[size_t(j)][size_t(c)];
                    // The graph's nonlinear output transfer means exact
                    // 8-bit absolute equality is not expected. This bound
                    // is tighter than old center-inset's wrong seam value.
                    const double error=std::abs(actual-expected);
                    std::printf("GUTTER ORACLE mode=%s side=%d u=%.3f channel=%d "
                                "expected=%.2f actual=%.2f delta=%.2f\n",
                                mode?"icb":"direct",side,double(kSampleU[size_t(sample)]),
                                c,expected,actual,error);
                    check(error<=3.0,
                          "independent logical-texel bilinear scene seam oracle");
                }
            }
        }
    if(!failed)std::puts(
        "PASS: production Renderer3D graph VT nonuniform gutters independent CPU seam oracle");
}
} // namespace

int aiverseMain(int,char**) {
    for(const char* flag:{"AIVERSE_EXPERIMENTAL_METAL_ICB",
                          "AIVERSE_GPU_ENCODED_ICB",
                          "AIVERSE_GPU_AUTHORED_DRAWS",
                          "AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT",
                          "AIVERSE_EXPERIMENTAL_ICB_MIXED_VT",
                          "AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH"}) {
        if(!optIn(flag)) {
            std::fprintf(stderr,"FAIL: experimental GPU ICB opt-in missing: %s\n",flag);
            return 2;
        }
    }
    std::shared_ptr<const render::graph::Program> graph;
    std::unique_ptr<render::Model> model;
    try {
        graph=std::make_shared<render::graph::Program>(compileAuthoredGraph());
        check(graph->textures.size()==4 && graph->usesVirtualTexture &&
              graph->vertexMsl.empty() && graph->vertexGlsl.empty(),
              "compiled actual fragment-only graph with 4 textures and VT");
        if(failed)return 1;
        model=makeScene(graph);
    }catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: production authored graph compile: %s\n",
                     error.what());
        return 2;
    }
    const bool indexed=optIn("AIVERSE_TEST_INDEXED_VT");
    Fixture sourceFiles;
    check(sourceFiles.ready,"guttered page archive directory created");
    for(int side=0;side<2;++side)
        for(int tile=0;tile<2;++tile)
            check(sourceFiles.write(side,tile),
                  "nonuniform 8x8 guttered physical page authored from original texels");
    if(failed)return 1;

    render::Renderer3D renderer;
    gfx::Device* device=nullptr;
    bool rendererReady=false,modelReady=false,prepared=false;
    std::array<std::unique_ptr<gfx::VirtualTextureFileStreamer>,2> streaming;
    std::array<Capture,10> captures; // 5 UV samples × {ordinary, GPU ICB}
    std::array<bool,5> actualIcb{};
    size_t frame=0,steady=0;
    int step=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(50);
    platform::AppConfig cfg;
    cfg.title="AIVERSE compiled Shader Graph nonuniform gutter VT GPU ICB";
    cfg.width=kWidth;cfg.height=kHeight;cfg.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& dev) {
        device=&dev;
        check(dev.info().backend==gfx::Backend::Metal &&
              dev.supportsGpuEncodedIcb(),
              "real Metal GPU encoded indirect commands supported");
        if(failed)return;
        rendererReady=renderer.init(dev);
        check(rendererReady,"production Renderer3D available");
        if(!rendererReady)return;
        renderer.setShadowQuality(render::ShadowQuality::Off);
        renderer.setMaterialQuality(render::MaterialQuality::High);
        renderer.setModelInstancingEnabled(true);
        renderer.setModelMultiDrawEnabled(true);
        renderer.setProfiling(true);
        renderer.lighting.tonemap=false;
        renderer.lighting.fogDensity=0;
        modelReady=render::uploadModel(dev,*model);
        check(modelReady,"rigid multi-material graph geometry uploaded");
        if(!modelReady)return;
        for(const auto& image:model->images)
            check(image.tex.valid(),"four Shader Graph source textures per material present");
        for(int side=0;side<2;++side) {
            gfx::TextureStreamConfig limits;
            limits.gpuBudgetBytes=8192;limits.stagingBudgetBytes=1024;
            limits.uploadBytesPerFrame=512;limits.uploadsPerFrame=2;
            streaming[size_t(side)]=
                std::make_unique<gfx::VirtualTextureFileStreamer>(dev,limits);
            gfx::VirtualTextureStreamConfig atlas;
            atlas.tilesX=2;atlas.tilesY=1;atlas.mipLevels=indexed?2:1;
            atlas.indexedMipTable=indexed;
            atlas.virtualLayers=2;atlas.physicalPages=2;
            atlas.physicalPageSize=kPage;atlas.physicalFormat=gfx::PixelFormat::RGBA8;
            atlas.pageTableBytesPerFlush=64;
            gfx::VirtualDiskPageConfig disk;
            disk.assetRoot=sourceFiles.root/(side==0?"left":"right");
            disk.tilesX=2;disk.tilesY=1;disk.mipLevels=indexed?2:1;
            disk.virtualLayers=2;disk.pageSize=kPage;
            disk.maxOutstanding=8;disk.maxReadyBytes=2048;
            const bool ok=streaming[size_t(side)]->initialize(atlas,disk,2);
            check(ok,"real separate disk/gpu VT domain initialized");
            if(!ok) {
                std::fprintf(stderr,"FAIL: VT initialization: %s\n",
                             streaming[size_t(side)]->lastError().c_str());
                return;
            }
        }
        check(streaming[0]->physicalAtlas()!=streaming[1]->physicalAtlas() &&
              (indexed ? streaming[0]->indexedMipPageTableTexture()!=streaming[1]->indexedMipPageTableTexture() :
               streaming[0]->pageTableTexture()!=streaming[1]->pageTableTexture()),
              "real physical GPU atlas and page table identities differ");
        renderer.setModelVirtualTextureResolver(
            [&](const render::Model&,const render::Material& mat,
                render::ModelVirtualTextureBinding& out) {
                const int side=mat.name=="vt-left"?0:
                    mat.name=="vt-right"?1:-1;
                if(side<0||!streaming[size_t(side)]||
                   !streaming[size_t(side)]->ready())return false;
                out.physicalAtlas=streaming[size_t(side)]->physicalAtlas();
                out.pageTable=indexed?streaming[size_t(side)]->indexedMipPageTableTexture():
                    streaming[size_t(side)]->pageTableTexture();
                out.indexedMipTable=indexed;
                out.virtualMipLevels=indexed?2:1;
                out.virtualWidthPixels=12;out.virtualHeightPixels=6;
                out.virtualAnisotropyTaps=indexed?8:1;
                out.layer=side;
                out.gutterPixels=float(kGutter);
                out.addressU=gfx::SamplerAddressMode::ClampToEdge;
                out.addressV=gfx::SamplerAddressMode::ClampToEdge;
                return out.valid();
            });
        for(int side=0;side<2;++side)
            for(int tile=0;tile<2;++tile) {
                const gfx::VirtualPageKey key{0,tile,0,side};
                const auto ticket=streaming[size_t(side)]->requestPage(key,true);
                check(ticket.valid(),"immutable nonuniform guttered tile queued");
            }
    };
    callbacks.frame=[&](gfx::Device& dev,double) {
        if(failed || finished || frame>=280 ||
           std::chrono::steady_clock::now()>=deadline) {
            if(!failed&&!finished)check(false,"full Renderer3D VT gutter ICB deadline");
            platform::requestQuit();return;
        }
        ++frame;
        for(int side=0;side<2;++side) {
            if(!streaming[size_t(side)]) {
                check(false,"native VT domain initialized before render");
                break;
            }
            auto& stream=*streaming[size_t(side)];
            stream.pump(2,[&,side](gfx::VirtualDiskPageTicket,
                                    gfx::VirtualPageKey key,bool ok,
                                    const std::string& error) {
                if(!ok) {
                    failed=true;
                    std::fprintf(stderr,"FAIL: source page disk-to-GPU "
                        "side=%d x=%d layer=%d: %s\n",
                        side,key.x,key.layer,error.c_str());
                }
            });
            check(stream.ready(),"native page-streamer atlas healthy");
        }
        if(failed){platform::requestQuit();return;}
        if(!prepared) {
            bool all=true;
            for(int side=0;side<2;++side)
                for(int tile=0;tile<2;++tile) {
                    const gfx::VirtualPageKey key{0,tile,0,side};
                    const auto& streamer=*streaming[size_t(side)];
                    all&=streamer.exactResidentPage(key);
                    const auto table=indexed?
                        streamer.atlasStreamer().encodedPageTableForMip(tile,0,side,0):
                        streamer.encodedPageTable(tile,0,side);
                    if(all)all&=(table==std::array<uint8_t,4>{
                        uint8_t(tile),0,0,255});
                }
            if(all) {
                prepared=true;
                std::puts("PASS: disk-backed nonuniform VT physical pages 0/1 published");
            }
        }
        const int sample=step/2;
        const bool gpu=(step%2)==1;
        renderer.setGpuAuthoredDrawEnabled(gpu);
        const float u=kSampleU[size_t(std::min(sample,4))];
        for(auto& mat:model->materials) {
            mat.uvOffset={u,0.4f};
            mat.uvScale={0.0001f,0.0001f};
        }
        render::RTSCamera cam;
        cam.target={0,0,0};cam.distance=4.0f;
        cam.pitch=0.10f;cam.yaw=0.0f;
        renderer.beginScene(cam,kWidth,kHeight);
        render::ModelDraw draw;
        draw.model=model.get();draw.flags=render::kDrawStatic;
        renderer.drawModel(draw);
        gfx::PassDesc pass;
        renderer.render(pass);
        if(!renderer.modelError().empty()) {
            std::fprintf(stderr,"FAIL: Shader Graph native pipeline: %s\n",
                         renderer.modelError().c_str());
            failed=true;
        }
        if(prepared && gpu) {
            const auto stats=renderer.gpuModelDrawStats();
            if(indexed && stats.eligible==0 && stats.shaderGraphIcbSubmissions==0 &&
               stats.perCommandVtExecutions==0 && stats.gpuEncodedCommands==0 &&
               stats.gpuIcbExecuteFailed==0)actualIcb[size_t(sample)]=true;
            if(!indexed && stats.shaderGraphIcbSubmissions>=1 &&
               stats.mixedGraphSourceSubmissions>=1 &&
               stats.perCommandVtExecutions>=1 &&
               stats.mixedVtExecutions>=1 &&
               stats.gpuEncodedCommands>=2 &&
               stats.gpuIcbRejected==0 && stats.gpuIcbExecuteFailed==0)
                actualIcb[size_t(sample)]=true;
            if(steady==2)
                std::printf("GUTTER GPU u=%.3f graph=%u mixedGraph=%u "
                            "vt=%u mixedVt=%u commands=%u rejected=%u errors=%u\n",
                            double(u),stats.shaderGraphIcbSubmissions,
                            stats.mixedGraphSourceSubmissions,
                            stats.perCommandVtExecutions,stats.mixedVtExecutions,
                            stats.gpuEncodedCommands,stats.gpuIcbRejected,
                            stats.gpuIcbExecuteFailed);
        }
        if(prepared && ++steady>=3 && step<10) {
            auto& capture=captures[size_t(step)];
            if(!capture.requested) {
                capture.requested=true;
                dev.captureNextFrame([&,step](int width,int height,std::vector<uint8_t> data) {
                    check(width==kWidth&&height==kHeight,
                          "native 192x128 screenshot extent");
                    captures[size_t(step)].pixels=std::move(data);
                    captures[size_t(step)].ready=true;
                });
            }
        }
        dev.endPass();
        if(prepared && step<10 && captures[size_t(step)].ready) {
            ++step;
            steady=0;
        }
        if(step==10) {
            for(int i=0;i<5;++i) {
                check(actualIcb[size_t(i)],
                      indexed?"indexed VT retained safe direct rendering under ICB opt-in":
                      "actual mixed graph and VT GPU indirect draw submitted");
                compareCaptured(captures[size_t(i*2)].pixels,
                                captures[size_t(i*2+1)].pixels,
                                "nonuniform-gutter");
            }
            if(!failed)independentSceneOracle(captures);
            if(!failed)std::puts(indexed?
                "PASS: production Renderer3D indexed VT nonuniform direct and ICB fallback parity":
                "PASS: production Renderer3D Shader Graph nonuniform guttered disk VT mixed Metal GPU ICB seam parity");
            finished=true;
            platform::requestQuit();
        }
    };
    callbacks.shutdown=[&] {
        if(device) {
            renderer.clearModelVirtualTextureResolver();
            if(rendererReady)renderer.shutdown();
            if(modelReady)render::releaseModel(*device,*model);
            for(auto& stream:streaming)
                if(stream)stream->shutdown();
        }
        // macOS NSApplication::terminate may exit the process before
        // platform::run returns and aiverseMain can return a failure code.
        // An observed FAIL must ALWAYS make CTest fail on the real Mac.
        if (!finished) check(false, "production gutter ICB fixture did not finish");
        if (failed) {
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(1);
        }
    };
    platform::run(cfg,callbacks);
    check(finished,"complete mixed Renderer3D VT guttered image-oracle cycle");
    return failed?1:0;
}
