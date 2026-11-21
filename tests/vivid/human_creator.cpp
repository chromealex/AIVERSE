#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/ViewportSculptPick.h"
#include "engine/character/vivid/DeformedSculptPick.h"
#include "engine/character/vivid/DeformedSculptWorkspace.h"
#include "engine/character/vivid/SculptGpuPickContract.h"
#include "engine/character/vivid/SculptGpuPickShaders.h"
#include "engine/character/vivid/SculptGpuSceneOccluders.h"
#include "engine/character/vivid/SculptGpuAlphaMask.h"
#include "engine/character/vivid/SculptGpuPickEligibility.h"
#include "engine/character/vivid/SculptGpuRendererView.h"
#include "engine/character/vivid/SculptGpuSubmittedView.h"
#include "engine/character/vivid/SculptGpuRendererMaskSources.h"
#include "engine/character/vivid/SculptGpuColorPixelOracle.h"
#include "engine/character/vivid/SculptGpuVertexSource.h"
#include "engine/render/ModelShaders.h"
#include <string_view>
#include <limits>
#include "engine/character/vivid/MorphLodTransfer.h"
#include "engine/character/vivid/MorphLodSurface.h"
#include "engine/character/vivid/MorphLodMapping.h"
#include "engine/character/vivid/MorphLodGpuAuthoring.h"
#include "engine/character/vivid/HumanImport.h"
#include <filesystem>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse::vivid;
namespace {
void req(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void test() {
    HumanCreatorSession session;
    req(session.dna().id=="vivid_default" && !session.dirty(),
        "creator default failed");
    const auto start=session.revision();
    req(session.edit([](HumanDNA& d){d.skin.wetness=0.7f;}),
        "valid DNA edit rejected");
    req(session.revision()==start+1 && session.dirty() &&
        session.dna().skin.wetness==0.7f,"DNA transaction not recorded");
    std::string error;
    req(!session.edit([](HumanDNA& d){d.groom.guideCount=999999;},&error) &&
        !error.empty() && session.dna().groom.guideCount==512,
        "invalid creator edit was not rolled back");
    req(session.undo() && session.dna().skin.wetness==0 &&
        session.canRedo(),"DNA undo failed");
    req(session.redo() && session.dna().skin.wetness==0.7f,
        "DNA redo failed");
    req(session.preset("stylized") &&
        session.dna().anatomy.eyeScale>1.1f,"creator preset not applied");
    req(session.undo() && session.dna().skin.wetness==0.7f,
        "preset undo failed");
    const auto file=(std::filesystem::temp_directory_path()/
        "aiverse-vivid-human-creator-test.json").string();
    req(session.saveFile(file,&error) && !session.dirty(),
        "creator save failed");
    HumanCreatorSession second;
    req(second.loadFile(file,&error) && !second.dirty() &&
        second.dna().toJson()==session.dna().toJson(),
        "creator JSON file load mismatch");
    req(!second.loadJson(aiverse::Json::parse(R"({"version":100})"),&error) &&
        second.dna().toJson()==session.dna().toJson(),
        "creator invalid DNA replaced last-good profile");
    {
        MorphTargetInput shape;
        shape.name="mouthSmileLeft";
        MorphDelta initial;
        initial.vertex=1;
        initial.position={0.1f,0,0};
        shape.deltas.push_back(initial);
        const auto csr=SparseMorphLibrary::compile(4,{shape});
        auto sculpt=MorphAuthoring::fromCompiled(csr);
        req(sculpt.targets()==1 && sculpt.vertices()==4,
            "sculpt target import failed");
        req(sculpt.setPosition(0,1,{0.25f,0.1f,0}) && sculpt.canUndo(),
            "sparse morph edit failed");
        req(sculpt.undo() && sculpt.redo(),
            "sparse morph undo/redo failed");
        const auto exported=sculpt.toJson(4);
        const auto compiled=parseHumanMorphs(exported,4);
        req(compiled.targets()==1 && compiled.deltas()==1,
            "sparse morph JSON roundtrip lost shape");
        std::size_t targetId=0;
        req(compiled.findTarget("mouthSmileLeft",targetId) && targetId==0,
            "sparse sculpt export lost target name");
        req(sculpt.addTarget("browUp") && sculpt.targets()==2,
            "named corrective creation failed");
        req(!sculpt.addTarget("browUp"),
            "duplicate corrective name accepted");
        req(sculpt.setPosition(1,2,{0,0.2f,0}) &&
            sculpt.removeTarget(0) && sculpt.targets()==1,
            "corrective deletion or remapping failed");
        req(sculpt.undo() && sculpt.targets()==2 &&
            sculpt.redo() && sculpt.targets()==1,
            "corrective target deletion undo/redo failed");
        const std::array<MorphVec3,4> positions{{{0,0,0},{0.1f,0,0},
                                                  {0.3f,0,0},{2,0,0}}};
        req(sculpt.brush(0,positions,0,0.35f,{0.05f,0,0}) &&
            sculpt.canUndo(),
            "falloff morph brush rejected");
        const auto brushed=sculpt.compile();
        req(brushed.deltas()>=2,"brush did not affect adjacent source vertices");
        req(!sculpt.brush(0,positions,0,-1.0f,{0.1f,0,0}) &&
            !sculpt.brush(0,positions,0,0.5f,{50.0f,0,0}),
            "invalid brush parameters accepted");
        req(sculpt.undo() && sculpt.redo(),
            "brush stroke history failed");
        req(!sculpt.setPosition(0,4,{1,0,0}) &&
            !sculpt.setPosition(0,1,{100,0,0}),
            "out-of-range sculpt accepted");
        bool rejected=false;
        try { (void)sculpt.toJson(1); }
        catch(const std::invalid_argument&) { rejected=true; }
        req(rejected,"sculpt exported generated vertex outside original topology");
    }
    {
        // Actual triangle-ray hit must choose the nearest source vertex,
        // never a projected point hidden behind unrelated model surfaces.
        aiverse::render::Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.indices={0,1,2};
        aiverse::render::MeshDef mesh;
        aiverse::render::Primitive triangle;
        triangle.firstIndex=0;
        triangle.indexCount=3;
        mesh.primitives.push_back(triangle);
        model.meshes.push_back(std::move(mesh));
        aiverse::render::Node node;
        node.mesh=0;
        model.nodes.push_back(node);
        const std::array<aiverse::render::mat4,1> pose{};
        const auto picked=pickSculptVertex(model,0,pose,
            {0.05f,0.05f,2.0f},{0,0,-1},3);
        req(picked.hit&&picked.vertex==0,
            "viewport sculpt did not pick expected nearest triangle vertex");
        req(!pickSculptVertex(model,0,pose,
            {2,2,2},{0,0,-1},3).hit,
            "viewport sculpt accepted ray missing the triangle");
    }
    // 7AN: GPU colour-ID/depth callback contract. These tests require no
    // graphics backend; real ID pass/rasterized visibility is a separate gate.
    {
        std::uint32_t ordinal=123u;
        const auto first=packSculptTriangleId(0u);
        req(first==std::array<std::uint8_t,4>{{1,0,0,0}} &&
            unpackSculptTriangleId(first,ordinal) && ordinal==0u,
            "GPU triangle ID packing broke first ordinal");
        const auto largest=packSculptTriangleId(999999u);
        req(unpackSculptTriangleId(largest,ordinal) && ordinal==999999u,
            "GPU triangle ID packing broke upper bounded ordinal");
        ordinal=456u;
        req(!unpackSculptTriangleId({0,0,0,0},ordinal) && ordinal==456u &&
            !unpackSculptTriangleId({255,255,255,255},ordinal) &&
            !unpackSculptTriangleId(packSculptTriangleId(1000000u),ordinal),
            "GPU triangle ID clear/overflow must fail without writing ordinal");
        SculptGpuPickProtocol protocol;
        const auto older=protocol.begin(7u,20u,0u,800u,600u,200u,300u);
        const auto newer=protocol.begin(7u,21u,0u,800u,600u,200u,300u);
        req(older.valid() && newer.valid() &&
            older.generation!=newer.generation &&
            protocol.pending() && protocol.pendingIs(newer) &&
            !protocol.pendingIs(older),
            "GPU pick protocol did not replace older click");
        const SculptGpuPickPixel pixel{packSculptTriangleId(0u),0.45f,true};
        req(!protocol.consume(older,pixel,7u,22u,800u,600u,ordinal) &&
            protocol.pending(),
            "stale GPU pixel callback stole a newer sculpt selection");
        req(!protocol.consume(newer,pixel,8u,22u,800u,600u,ordinal) &&
            !protocol.consume(newer,pixel,7u,20u,800u,600u,ordinal) &&
            !protocol.consume(newer,pixel,7u,22u,801u,600u,ordinal) &&
            protocol.pending(),
            "GPU pick accepted wrong actor revision, frame or viewport size");
        SculptGpuPickPixel badDepth=pixel;
        badDepth.depth01=std::numeric_limits<float>::quiet_NaN();
        req(!protocol.consume(newer,badDepth,7u,22u,800u,600u,ordinal) &&
            protocol.pending(),
            "invalid GPU depth was incorrectly accepted");
        req(protocol.consume(newer,pixel,7u,22u,800u,600u,ordinal) &&
            ordinal==0u && !protocol.pending() &&
            !protocol.pendingIs(newer),
            "valid GPU ID/depth callback was not resolved");
        const auto obsolete=protocol.begin(7u,23u,0u,800u,600u,200u,300u);
        protocol.cancel(); // caller has published a different character
        req(!protocol.consume(obsolete,pixel,7u,25u,800u,600u,ordinal) &&
            !protocol.pending(),
            "old GPU pixel was accepted after character publication");
        req(!protocol.begin(7u,26u,0u,800u,600u,801u,300u).valid() &&
            !protocol.pending(),
            "off-viewport GPU picking request should be rejected");
    }
    {
        // GPU supplies one mesh-local triangle ID. CPU resolves only that
        // triangle, with morphed+skinned positions and a seam-clone source map.
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(4);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.vertices[3]=model.vertices[0];
        model.indices={3,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=3;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        const std::array<mat4,1> pose{{mat4::translate({0,0,2})}};
        MorphTargetInput morph;
        morph.name="lift";
        MorphDelta delta;
        delta.vertex=3;delta.position={0.4f,0,0};
        morph.deltas={delta};
        const auto library=SparseMorphLibrary::compile(4,{morph});
        std::array<float,1> weights{{1.f}};
        std::vector<float3> morphed;
        req(prepareSculptMorphPositions(model,library,weights,morphed),
            "GPU pick CPU corroboration morph pose unavailable");
        SkinnedWeightSurface surface;
        req(prepareSkinnedWeightSurface(model,0,pose,surface,morphed),
            "GPU pick CPU corroboration world pose unavailable");
        const std::array<std::uint32_t,4> map{{0,1,2,0}};
        const auto picked=resolveSculptGpuTriangle(
            model,surface,0u,{0.42f,0.05f,4.f},{0,0,-1},3,map);
        req(picked.hit && picked.vertex==0u &&
            std::fabs(picked.worldPosition.x-0.4f)<1.e-5f &&
            std::fabs(picked.worldPosition.z-2.f)<1.e-5f,
            "GPU triangle ID did not resolve to seam-clone author vertex");
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.f}
        }};
        const auto binding=FaceShapeBinding::compile(library,routes);
        FaceFrame face;
        face.values[std::size_t(Channel::JawOpen)]=1.f;
        DeformedSculptWorkspace workspace;
        const auto cpuPick=workspace.pick(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,map,42u,7u);
        const auto gpuResolved=workspace.resolveGpuTriangle(
            model,0,0u,{0.42f,0.05f,4.f},{0,0,-1},3,map,42u,7u);
        req(cpuPick.hit && gpuResolved.hit &&
            gpuResolved.vertex==cpuPick.vertex &&
            std::fabs(gpuResolved.rayDistance-cpuPick.rayDistance)<1.e-5f,
            "workspace GPU ID resolution diverges from CPU BVH pick");
        req(!workspace.resolveGpuTriangle(
            model,0,0u,{0.42f,0.05f,4.f},{0,0,-1},3,map,43u,7u).hit &&
            !workspace.resolveGpuTriangle(
            model,0,0u,{0.42f,0.05f,4.f},{0,0,-1},3,map,42u,8u).hit,
            "GPU ID resolution accepted a different frame or model publication");
        workspace.reset();
        req(!workspace.resolveGpuTriangle(
            model,0,0u,{0.42f,0.05f,4.f},{0,0,-1},3,map,42u,7u).hit,
            "GPU ID resolver accepted a stale CPU surface after reset");

        req(!resolveSculptGpuTriangle(
            model,surface,2u,{0.42f,0.05f,4.f},{0,0,-1},3,map).hit &&
            !resolveSculptGpuTriangle(
                model,surface,0u,{2,2,4},{0,0,-1},3,map).hit,
            "GPU triangle ID accepted nonexistent or ray-incompatible hit");
        auto uneditable=map;
        uneditable[3]=std::numeric_limits<std::uint32_t>::max();
        req(!resolveSculptGpuTriangle(
            model,surface,0u,{0.42f,0.05f,4.f},{0,0,-1},3,uneditable).hit,
            "GPU ID lookup returned a generated LOD vertex as authorable");
    }
    // 7AN raster preparation: each triangle receives one flat GPU ID,
    // while an uneditable LOD triangle still writes depth with ZERO ID.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(5);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.vertices[3]=model.vertices[0]; // seam clone of original 0
        model.vertices[4]=model.vertices[0]; // generated LOD, no author ID
        model.indices={3,1,2,4,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=6;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        const std::array<mat4,1> nodeWorld{{mat4::translate({0,0,2})}};
        SkinnedWeightSurface surface;
        req(prepareSkinnedWeightSurface(model,0,nodeWorld,surface),
            "GPU ID fixture failed to prepare visible mesh surface");
        const std::array<std::uint32_t,5> authorMap{{
            0u,1u,2u,0u,std::numeric_limits<std::uint32_t>::max()
        }};
        std::vector<SculptGpuIdVertex> expanded;
        req(buildSculptGpuIdVertices(
                model,surface,3u,authorMap,expanded) &&
            expanded.size()==6u &&
            std::fabs(expanded[0].world.z-2.f)<1.e-5f,
            "GPU ID triangle stream did not preserve deformed world positions");
        for(int i=0;i<3;++i) {
            std::array<std::uint8_t,4> id{};
            for(int k=0;k<4;++k)
                id[std::size_t(k)]=expanded[std::size_t(i)].rgba[k];
            std::uint32_t ordinal=999u;
            req(unpackSculptTriangleId(id,ordinal) && ordinal==0u,
                "first GPU triangle must have one constant flat ID");
        }
        for(int i=3;i<6;++i)
            req(expanded[std::size_t(i)].rgba[0]==0 &&
                expanded[std::size_t(i)].rgba[1]==0 &&
                expanded[std::size_t(i)].rgba[2]==0 &&
                expanded[std::size_t(i)].rgba[3]==0,
                "generated LOD triangle must occlude without selectable ID");
        const auto original=expanded;
        auto bad=model;
        bad.indices[0]=999u;
        req(!buildSculptGpuIdVertices(
                bad,surface,3u,authorMap,expanded) &&
            expanded.size()==original.size() &&
            expanded[0].rgba[0]==original[0].rgba[0],
            "invalid GPU ID triangle source replaced last-good stream");
        const auto has=[](const char* src,std::string_view term) {
            return std::string_view(src).find(term)!=std::string_view::npos;
        };
        req(sizeof(SculptGpuIdVertex)==16u &&
            has(kSculptGpuIdVS,"flat out vec4 v_id") &&
            has(kSculptGpuIdFS,"outColor=v_id") &&
            has(kSculptGpuIdMSL,"identifier [[flat]]") &&
            has(kSculptGpuIdMSL,"return input.identifier"),
            "GPU ID GLSL/MSL shader ABI/source parity contract missing");
        const auto shader=sculptGpuIdShaderDesc();
        const auto pipe=sculptGpuIdPipelineDesc(
            aiverse::gfx::ShaderHandle{7u});
        req(shader.glslVertex==kSculptGpuIdVS &&
            shader.glslFragment==kSculptGpuIdFS &&
            shader.msl==kSculptGpuIdMSL &&
            shader.uniformSlots==1 && shader.textureSlots==0 &&
            pipe.attrCount==2 && pipe.stride==16u &&
            pipe.attrs[0].location==0 &&
            pipe.attrs[0].format==aiverse::gfx::VertexFormat::Float3 &&
            pipe.attrs[1].location==1 &&
            pipe.attrs[1].format==aiverse::gfx::VertexFormat::UByte4N &&
            pipe.attrs[1].offset==12u &&
            pipe.depthTest && pipe.depthWrite &&
            pipe.blend==aiverse::gfx::BlendMode::Opaque &&
            pipe.colorFormat==aiverse::gfx::PixelFormat::RGBA8,
            "GPU ID pass must retain linear RGBA8 and hardware depth writes");

    }
    // 7AP: scene depth occluders have ZERO selectable ID. A face lying
    // behind an opaque body mesh, hair card or floor cannot be confirmed
    // as visible by the selected-mesh GPU pick pass.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(6);
        for(int i=0;i<6;++i)
            model.vertices[std::size_t(i)].pos={
                float(i%3==1),float(i%3==2),i<3?0.f:0.6f};
        model.indices={0,1,2,3,4,5};
        MeshDef face,body;
        Primitive prim;
        prim.firstIndex=0;prim.indexCount=3;prim.material=0;
        face.primitives.push_back(prim);
        prim.firstIndex=3;prim.indexCount=3;
        body.primitives.push_back(prim);
        model.meshes.push_back(face);
        model.meshes.push_back(body);
        model.materials.emplace_back();
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[1].mesh=1;
        const std::array<mat4,2> pose{{mat4{},mat4{}}};
        SkinnedWeightSurface selected,other;
        req(prepareSkinnedWeightSurface(model,0,pose,selected) &&
            prepareSkinnedWeightSurface(model,1,pose,other),
            "GPU scene occluder fixture skin surfaces unavailable");
        std::vector<SculptGpuIdVertex> gpu;
        req(buildSculptGpuIdVertices(model,selected,6,{},gpu) &&
            gpu.size()==3 && gpu[0].rgba[0]==1,
            "opaque selected face must carry one selectable triangle ID");
        req(appendSculptGpuMeshOccluder(model,other,gpu) &&
            gpu.size()==6 && gpu[3].rgba[0]==0 &&
            gpu[3].rgba[3]==0 &&
            std::fabs(gpu[3].world.z-0.6f)<1.e-5f,
            "other-model mesh must append zero-ID depth geometry");

        model.materials[0].alpha=AlphaMode::Mask;
        std::vector<SculptGpuIdVertex> masked;
        req(buildSculptGpuIdVertices(model,selected,6,{},masked) &&
            masked.size()==3 && masked[0].rgba[0]==0,
            "alpha-masked face must not return a selectable GPU ID");
        model.materials[0].alpha=AlphaMode::Blend;
        req(buildSculptGpuIdVertices(model,selected,6,{},masked) &&
            masked[0].rgba[0]==0,
            "transparent cornea must not return a selectable GPU ID");
        model.materials[0].alpha=AlphaMode::Opaque;
        model.materials[0].transparent=true;
        req(buildSculptGpuIdVertices(model,selected,6,{},masked) &&
            masked[0].rgba[0]==0,
            "transparent depth-after material must not return a selectable GPU ID");

        const std::array<float3,3> floor{{
            {-1.f,-1.f,1.f},{1.f,-1.f,1.f},{0,1.f,1.f}
        }};
        const std::array<std::uint32_t,3> faceIndices{{0,1,2}};
        std::vector<SculptGpuIdVertex> floorData;
        req(appendSculptGpuWorldOccluder(
                floor,faceIndices,mat4::identity(),floorData) &&
            floorData.size()==3 && floorData[0].rgba[0]==0,
            "floor occluder geometry was not represented in the ID pass");
        std::array<ModelVertex,3> hair{};
        for(int i=0;i<3;++i)hair[std::size_t(i)].pos=floor[i];
        std::vector<SculptGpuIdVertex> hairData;
        req(appendSculptGpuWorldOccluder(
                hair,faceIndices,mat4::translate({0,0,-1}),hairData) &&
            hairData.size()==3 && hairData[0].rgba[0]==0 &&
            std::fabs(hairData[0].world.z)<1.e-5f,
            "animated groom must produce world-space zero-ID occluders");
        const auto before=hairData.size();
        const std::array<std::uint32_t,3> invalidIndices{{0,1,99}};
        req(!appendSculptGpuWorldOccluder(
                hair,invalidIndices,mat4::identity(),hairData) &&
            hairData.size()>=before,
            "bad procedural occluder must be rejected");
    }
    // 7AQ: standard alpha-mask GPU geometry must sample the original
    // base texture and UV transform per fragment, preserving the source
    // triangle ID only if the pixel survives the alpha cutoff.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};model.vertices[0].uv={0,0};
        model.vertices[1].pos={1,0,0};model.vertices[1].uv={1,0};
        model.vertices[2].pos={0,1,0};model.vertices[2].uv={0,1};
        model.indices={0,1,2};
        MeshDef mesh;
        Primitive prim;
        prim.firstIndex=0;prim.indexCount=3;prim.material=0;
        mesh.primitives.push_back(prim);
        model.meshes.push_back(mesh);
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        model.materials.resize(1);
        auto& material=model.materials[0];
        material.alpha=AlphaMode::Mask;
        material.alphaCutoff=0.55f;
        material.baseColor.w=0.9f;
        material.baseTex=0;
        material.uvOffset={0.2f,0.3f};
        material.uvScale={0.7f,0.8f};
        material.uvRotation=0.25f;
        model.images.resize(1);
        model.images[0].tex=aiverse::gfx::TextureHandle{17u};
        const std::array<mat4,1> pose{{mat4{}}};
        SkinnedWeightSurface surface;
        req(prepareSkinnedWeightSurface(model,0,pose,surface),
            "alpha-mask sculpt geometry reference pose missing");
        std::vector<SculptGpuIdVertex> opaque;
        std::vector<SculptGpuMaskBatch> masks;
        req(appendSculptGpuMaterialGeometry(
                model,surface,3,{},true,opaque,masks) &&
            opaque.empty() && masks.size()==1 &&
            masks[0].vertices.size()==3 &&
            masks[0].baseTexture==model.images[0].tex &&
            masks[0].vertices[0].rgba[0]==1 &&
            std::fabs(masks[0].uniform.alpha[0]-0.9f)<1.e-6f &&
            std::fabs(masks[0].uniform.alpha[1]-0.55f)<1.e-6f &&
            std::fabs(masks[0].uniform.uv0[0]-0.2f)<1.e-6f &&
            std::fabs(masks[0].uniform.uv1[0]-0.25f)<1.e-6f,
            "GPU mask did not bind source texture or material alpha/UV");
        std::vector<SculptGpuIdVertex> occluded;
        std::vector<SculptGpuMaskBatch> occlusionMasks;
        req(appendSculptGpuMaterialGeometry(
                model,surface,0,{},false,occluded,occlusionMasks) &&
            occluded.empty() && occlusionMasks.size()==1 &&
            occlusionMasks[0].vertices[0].rgba[0]==0,
            "other mesh alpha mask must sample texture while writing zero ID");
        const auto shader=sculptGpuMaskShaderDesc();
        const auto pipeline=sculptGpuMaskPipelineDesc(
            aiverse::gfx::ShaderHandle{5});
        req(shader.textureSlots==1 && shader.uniformSlots==2 &&
            pipeline.stride==32 && pipeline.attrCount==3 &&
            pipeline.attrs[2].format==aiverse::gfx::VertexFormat::Float4 &&
            pipeline.attrs[2].offset==16 &&
            pipeline.depthTest && pipeline.depthWrite &&
            pipeline.blend==aiverse::gfx::BlendMode::Opaque,
            "alpha-tested GPU pass lost uniform/vertex/depth contract");
        req(std::string_view(kSculptGpuMaskFS).find(
                "texture(tex0,v_uv).a")!=std::string_view::npos &&
            std::string_view(kSculptGpuMaskMSL).find(
                "discard_fragment()")!=std::string_view::npos,
            "GLSL/Metal alpha-tested fragment source missing");
        model.images[0].tex={};
        opaque.clear();masks.clear();
        req(appendSculptGpuMaterialGeometry(
                model,surface,3,{},true,opaque,masks) &&
            opaque.size()==3 && masks.empty() &&
            opaque[0].rgba[0]==0,
            "unavailable GPU mask texture must fail closed to zero-ID depth");
        model.images[0].tex=aiverse::gfx::TextureHandle{17u};
        material.shaderGraph=std::make_shared<
            const aiverse::render::graph::Program>();
        opaque.clear();masks.clear();
        req(appendSculptGpuMaterialGeometry(
                model,surface,3,{},true,opaque,masks) &&
            opaque.size()==3 && masks.empty() &&
            opaque[0].rgba[0]==0,
            "unknown custom ShaderGraph alpha must remain conservative");
    }
    // 7AT: the ID compute index source addresses the SAME published 60B
    // ModelVertex GPU buffer Renderer3D borrowed. Only palette/triangle
    // metadata are authored on CPU; morphed positions are GPU-resident.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(4);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.vertices[3]=model.vertices[0]; // seam-cloned source vertex
        model.indices={3,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        const std::array<mat4,1> pose{{mat4::identity()}};
        const std::array<std::uint32_t,4> authorIds{{0,1,2,0}};
        SculptGpuVertexSource built;
        const mat4 world=mat4::translate({0,0,2});
        req(prepareSculptGpuVertexSource(
                model,0,pose,world,{71},4,authorIds,3,built) &&
            built.actualVertexOverride==aiverse::gfx::BufferHandle{71} &&
            built.params.count==3 &&
            built.params.vertices==4 &&
            built.params.skinned==0 &&
            built.indices.size()==3 &&
            built.indices[0].vertex==3 &&
            built.indices[0].triangleId==1 &&
            built.indices[1].vertex==1 &&
            built.indices[2].vertex==2 &&
            built.influences.empty() &&
            built.jointMatrices.empty() &&
            std::fabs(built.params.rigidWorld.at(2,3)-2.f)<1.e-6f,
            "actual GPU-source ID requests lost source clone/topology/rigid world");
        const auto goodCount=built.indices.size();
        req(!prepareSculptGpuVertexSource(
                model,0,pose,world,{71},3,authorIds,3,built) &&
            built.indices.size()==goodCount && built.params.vertices==4,
            "invalid override size replaced last-good indexed GPU source");
        const std::array<std::uint32_t,4> nonauthorable{{
            0,1,2,std::numeric_limits<std::uint32_t>::max()
        }};
        req(prepareSculptGpuVertexSource(
                model,0,pose,world,{71},4,nonauthorable,3,built) &&
            built.indices[0].triangleId==0 &&
            built.indices[1].triangleId==0,
            "non-authorable seam/LOD triangle incorrectly got a selectable ID");
        const auto shader=std::string_view(kSculptGpuSourceExpandMSL);
        req(shader.find("source[offset]")!=std::string_view::npos &&
            shader.find("influences[request.vertexIndex]")!=std::string_view::npos &&
            shader.find("output[threadIndex].world")!=std::string_view::npos,
            "GPU actor stream gather MSL did not reference actual 60B positions");
    }
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        for(auto& vertex:model.vertices) {
            vertex.joints[0]=0;vertex.weights[0]=128;
            vertex.joints[1]=1;vertex.weights[1]=64;
        }
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.indices={0,1,2};
        MeshDef mesh;
        Primitive p;
        p.firstIndex=0;p.indexCount=3;
        p.jointPalette={0,1};
        mesh.primitives.push_back(p);
        model.meshes.push_back(mesh);
        model.nodes.resize(3);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1,2};
        model.skins[0].inverseBind={mat4::identity(),mat4::identity()};
        const std::array<mat4,3> pose{{
            mat4::identity(),mat4::translate({0,0,2}),
            mat4::translate({0,0,4})
        }};
        SculptGpuVertexSource source;
        const auto rootWorld=mat4::translate({0,0,3});
        req(prepareSculptGpuVertexSource(
                model,0,pose,rootWorld,{11},3,{},3,source) &&
            source.params.skinned==1 &&
            source.params.jointMatrices==2 &&
            source.influences.size()==3 &&
            source.influences[0].joints[0]==0 &&
            source.influences[0].joints[1]==1 &&
            std::fabs(source.influences[0].weights[0]-
                      128.f/255.f)<1.e-6f &&
            std::fabs(source.influences[0].weights[1]-
                      64.f/255.f)<1.e-6f &&
            std::fabs(source.jointMatrices[0].at(2,3)-2.f)<1.e-6f &&
            std::fabs(source.jointMatrices[1].at(2,3)-4.f)<1.e-6f &&
            std::fabs(source.params.rigidWorld.at(2,3)-3.f)<1.e-6f,
            "GPU source must preserve Renderer3D raw UNORM8 weights and local skin matrices");
        // Renderer3D blends 3x4 palette ROWS before applying I.model;
        // verify numeric reference with two nonuniform U8 influences.
        const auto packed=sculptGpuRendererSkinRows(
            std::span<const mat4>(source.jointMatrices));
        const float wa=source.influences[0].weights[0];
        const float wb=source.influences[0].weights[1];
        const float sum=wa+wb;
        const auto localZ=(packed[0].row2[3]*wa+
                           packed[1].row2[3]*wb)/sum;
        const auto worldZ=source.params.rigidWorld.transformPoint(
            {0,0,localZ}).z;
        req(packed.size()==2 && sizeof(SculptGpuSkinRows)==48 &&
            std::fabs(worldZ-(3.f+(2.f*128.f+4.f*64.f)/192.f))<1.e-5f,
            "VIVID skinned GPU position differs from renderer 3x4 row reference");
        const auto msl=std::string_view(kSculptGpuSourceExpandMSL);
        const auto maskedMsl=std::string_view(kSculptGpuSourceMaskExpandMSL);
        const auto renderMsl=std::string_view(shaders::kModelMSL);
        req(msl.find("dot(r0,bind)")!=std::string_view::npos &&
            msl.find("params.rigidWorld*skinned")!=std::string_view::npos &&
            maskedMsl.find("dot(r0,bind)")!=std::string_view::npos &&
            maskedMsl.find("params.rigidWorld*skinned")!=
                std::string_view::npos &&
            renderMsl.find("r0 += sk.v[slot * 3] * wv[k]")!=
                std::string_view::npos &&
            renderMsl.find("dot(r0, d.p)")!=std::string_view::npos,
            "Metal sculpt compute no longer mirrors Renderer3D skin row order");
        const auto good=source.indices;
        model.meshes[0].primitives[0].jointPalette={999};
        req(!prepareSculptGpuVertexSource(
                model,0,pose,rootWorld,{11},3,{},3,source) &&
            source.indices.size()==good.size() &&
            source.indices[0].triangleId==good[0].triangleId,
            "invalid skinned GPU joint palette overwrote published inputs");
        model.meshes[0].primitives[0].jointPalette.assign(
            std::size_t(kMaxPaletteJoints)+1u,0u);
        req(!prepareSculptGpuVertexSource(
                model,0,pose,rootWorld,{11},3,{},3,source) &&
            source.indices.size()==good.size(),
            "over-limit GPU skin palette would differ from truncated renderer U3");
    }
    // 7AU: homogeneous sampled masks can use the GPU's actual 60B
    // positions with immutable UV/alpha metadata. An unsupported/mixed
    // material must not replace a previous valid transactional source.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        model.vertices[0].uv={0.25f,0.5f};
        model.vertices[1].uv={0.75f,0.5f};
        model.vertices[2].uv={0.5f,0.9f};
        model.vertices[0].color=0x7fffffff;
        model.indices={0,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        primitive.material=0;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        model.images.emplace_back();
        model.images[0].tex=aiverse::gfx::TextureHandle{17u};
        model.materials.emplace_back();
        auto& material=model.materials[0];
        material.alpha=AlphaMode::Mask;
        material.baseTex=0;
        material.alphaCutoff=0.5f;
        material.baseColor.w=0.75f;
        material.vertexColors=true;
        const std::array<mat4,1> pose{{mat4::identity()}};
        SculptGpuVertexSource source;
        req(prepareSculptGpuVertexSource(
                model,0,pose,mat4::identity(),{71},3,{},3,source) &&
            source.maskedSelection &&
            source.maskBaseTexture==model.images[0].tex &&
            source.maskInputs.size()==3 &&
            source.indices.size()==3 &&
            source.indices[0].triangleId==1 &&
            std::fabs(source.maskInputs[0].uvAlpha[0]-0.25f)<1.e-6f &&
            std::fabs(source.maskInputs[0].uvAlpha[2]-127.f/255.f)<1.e-6f &&
            std::fabs(source.maskUniform.alpha[0]-0.75f)<1.e-6f &&
            std::fabs(source.maskUniform.alpha[1]-0.5f)<1.e-6f,
            "GPU selected Mask lost exact actor source IDs, UV or alpha");
        const auto savedCount=source.indices.size();
        // 7AY: independently GPU-resident masked depth blocker has a
        // distinct 60B actor source, and must reject malformed indices,
        // invalid per-source UV or a reused selected source handle.
        SculptGpuVertexSource blocker=source;
        blocker.actualVertexOverride={72};
        req(sculptGpuMaskedOccluderInputsValid(blocker,{71}),
            "valid independent GPU masked occluder source was rejected");
        // Stage 7BA: up to four distinct independent Mask GPU sources
        // in one depth-target ID pass, with no partial acceptance.
        std::array<SculptGpuVertexSource,5> masks{};
        std::array<const SculptGpuVertexSource*,5> refs{};
        for(std::size_t i=0;i<masks.size();++i) {
            masks[i]=blocker;
            masks[i].actualVertexOverride={
                std::uint32_t(72u+i)};
            refs[i]=&masks[i];
        }
        req(sculptGpuMaskSourceSetValid(
                std::span<const SculptGpuVertexSource* const>(
                    refs.data(),4u),{71}),
            "four valid GPU Mask occluders exceeded the source limit");
        req(!sculptGpuMaskSourceSetValid(refs,{71}),
            "fifth GPU Mask occluder bypassed fixed scene budget");
        masks[1].actualVertexOverride=masks[0].actualVertexOverride;
        req(!sculptGpuMaskSourceSetValid(
                std::span<const SculptGpuVertexSource* const>(
                    refs.data(),4u),{71}),
            "duplicate 60B GPU Mask stream was accepted twice");
        masks[1].actualVertexOverride={73};
        masks[3].maskInputs[0].uvAlpha[0]=
            std::numeric_limits<float>::quiet_NaN();
        req(!sculptGpuMaskSourceSetValid(
                std::span<const SculptGpuVertexSource* const>(
                    refs.data(),4u),{71}),
            "invalid last Mask source was silently ignored");

        req(!sculptGpuMaskedOccluderInputsValid(blocker,{72}),
            "selected GPU actor cannot be its own unrelated occluder");
        blocker.indices[0].vertex=999999u;
        req(!sculptGpuMaskedOccluderInputsValid(blocker,{71}),
            "out-of-range GPU masked occluder source ID accepted");
        blocker=source;blocker.actualVertexOverride={72};
        blocker.maskInputs[0].uvAlpha[0]=
            std::numeric_limits<float>::quiet_NaN();
        req(!sculptGpuMaskedOccluderInputsValid(blocker,{71}),
            "nonfinite original source UV accepted for GPU mask depth");
        blocker=source;blocker.actualVertexOverride={72};
        blocker.params.count=99;
        req(!sculptGpuMaskedOccluderInputsValid(blocker,{71}),
            "incorrect GPU mask occluder source count accepted");
        // Renderer-derived Mask source is prepared only from one accepted
        // direct main-colour draw, not a hand-waved GPU buffer handle.
        Renderer3D::ModelViewProvenance maskView{};
        maskView.frameSerial=79;
        maskView.hasModel=maskView.complete=true;
        Renderer3D::ModelViewPrimitive maskPrim{};
        maskPrim.model=&model;
        maskPrim.nodeIndex=0;maskPrim.meshIndex=0;
        maskPrim.primitiveIndex=0;
        maskPrim.drawInvocation=2;
        maskPrim.firstIndex=0;maskPrim.indexCount=3;
        maskPrim.visible=maskPrim.colourCandidate=true;
        maskPrim.customGpuStream=true;
        maskPrim.maskedMaterial=true;
        maskPrim.maskBaseTexture=model.images[0].tex;
        maskPrim.maskAlpha=material.baseColor.w;
        maskPrim.maskCutoff=material.alphaCutoff;
        maskPrim.maskVertexColors=material.vertexColors;
        maskPrim.vertexOverride={71};
        maskPrim.vertexOverrideCount=3;
        maskPrim.world=mat4::identity();
        maskView.primitives={maskPrim};
        Renderer3D::ModelSubmissionProvenance maskSubmitted{};
        maskSubmitted.frameSerial=79;
        maskSubmitted.mainPassEncoded=true;
        Renderer3D::ModelSubmittedPrimitive maskDraw{};
        maskDraw.source=maskPrim;
        maskDraw.kind=Renderer3D::ModelSubmissionKind::Direct;
        maskDraw.phase=1;
        maskDraw.pipeline={3};
        maskSubmitted.commands={maskDraw};
        const SculptGpuMaskSceneCandidate candidate{&model,0,pose};
        SculptGpuVertexSource preparedOccluder;
        req(prepareSculptGpuRendererMaskOccluder(
                maskView,maskSubmitted,candidate,79,{70},preparedOccluder) &&
            preparedOccluder.maskedSelection &&
            preparedOccluder.actualVertexOverride==aiverse::gfx::BufferHandle{71},
            "accepted Renderer3D Mask source was rejected by GPU collector");
        const auto publishedCount=preparedOccluder.indices.size();
        maskSubmitted.commands[0].source.maskCutoff=.6f;
        req(!prepareSculptGpuRendererMaskOccluder(
                maskView,maskSubmitted,candidate,79,{70},preparedOccluder) &&
            preparedOccluder.indices.size()==publishedCount,
            "changed actual Renderer3D Mask cutoff bypassed source validation");
        maskSubmitted.commands[0].source.maskCutoff=material.alphaCutoff;
        req(!prepareSculptGpuRendererMaskOccluder(
                maskView,maskSubmitted,candidate,80,{70},preparedOccluder) &&
            preparedOccluder.indices.size()==publishedCount,
            "stale masked renderer source modified last-good GPU collector");
        maskSubmitted.commands[0].source.indexCount=6;
        req(!prepareSculptGpuRendererMaskOccluder(
                maskView,maskSubmitted,candidate,79,{70},preparedOccluder),
            "changed source index range was accepted by renderer collector");
        maskSubmitted.commands[0].source.indexCount=3;
        maskSubmitted.commands.clear();
        req(!prepareSculptGpuRendererMaskOccluder(
                maskView,maskSubmitted,candidate,79,{70},preparedOccluder),
            "missing submitted masked renderer source was accepted");
        material.shaderGraph=std::make_shared<
            const aiverse::render::graph::Program>();
        req(!prepareSculptGpuVertexSource(
                model,0,pose,mat4::identity(),{71},3,{},3,source) &&
            source.maskedSelection && source.indices.size()==savedCount,
            "custom masked graph overwrote last-good GPU source");
        material.shaderGraph.reset();
        model.vertices[1].uv.x=std::numeric_limits<float>::quiet_NaN();
        req(!prepareSculptGpuVertexSource(
                model,0,pose,mat4::identity(),{71},3,{},3,source) &&
            source.maskedSelection && source.indices.size()==savedCount,
            "nonfinite source UV overwrote GPU mask source");
        model.vertices[1].uv.x=0.75f;
        model.materials.emplace_back();
        model.meshes[0].primitives.push_back(primitive);
        model.meshes[0].primitives.back().material=1;
        req(!prepareSculptGpuVertexSource(
                model,0,pose,mat4::identity(),{71},3,{},3,source) &&
            source.maskedSelection && source.indices.size()==savedCount,
            "mixed selected opaque/masked GPU draw was accepted");
        const auto shader=std::string_view(kSculptGpuSourceMaskExpandMSL);
        req(sizeof(SculptGpuSourceMaskInput)==16 &&
            sizeof(SculptGpuMaskVertex)==32 &&
            shader.find("source[offset]")!=std::string_view::npos &&
            shader.find("masks[request.vertexIndex].uvAlpha")!=
                std::string_view::npos &&
            shader.find("packed_float3 world")!=std::string_view::npos,
            "GPU Mask vertex shader lost shared source/32B UV layout");
    }
    // 7AS: confirm that an *actual Renderer3D-selected* ModelItem view
    // is required before our independent CPU-fed GPU ID pass can run.
    // This is a pure snapshot-fixture; the native renderer path still
    // needs compilation and real GPU visual acceptance.
    {
        using namespace aiverse::render;
        Model model;
        model.meshes.resize(2);
        Primitive source;
        source.firstIndex=0;source.indexCount=3;
        model.meshes[0].primitives={source};
        source.firstIndex=3;
        model.meshes[1].primitives={source};
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[1].mesh=1;
        Renderer3D::ModelViewProvenance view;
        view.frameSerial=77;
        view.hasModel=true;
        view.complete=true;
        Renderer3D::ModelViewPrimitive selected;
        selected.model=&model;
        selected.nodeIndex=0;
        selected.drawInvocation=1;
        selected.meshIndex=0;
        selected.primitiveIndex=0;
        selected.firstIndex=0;
        selected.indexCount=3;
        selected.visible=true;
        selected.colourCandidate=true;
        Renderer3D::ModelViewPrimitive other=selected;
        other.nodeIndex=1;
        other.meshIndex=1;
        other.firstIndex=3;
        view.primitives={selected,other};
        auto check=validateSculptGpuRendererView(view,model,0,77);
        req(check.canRenderDiagnostic &&
            check.reason==SculptGpuRendererViewReason::EligibleCpuSnapshot &&
            check.selectedNode==0 &&
            check.otherVisibleMeshes==std::vector<std::uint32_t>{1} &&
            !check.gpuVertexOverridePresent,
            "renderer-derived visible meshes/primitive ranges lost");
        view.primitives[0].customGpuStream=true;
        view.primitives[0].vertexOverride={71};
        view.primitives[0].vertexOverrideCount=6;
        check=validateSculptGpuRendererView(view,model,0,77);
        req(check.canRenderDiagnostic && check.gpuVertexOverridePresent,
            "renderer vertex-override provenance must remain explicit");
        view.primitives[0].customGpuStream=false;
        view.primitives[0].vertexOverride={};
        view.primitives[0].vertexOverrideCount=0;
        req(!validateSculptGpuRendererView(view,model,0,78)
                .canRenderDiagnostic,
            "stale Renderer3D frame was accepted for GPU ID");
        view.primitives[0].firstIndex=99;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::PrimitiveIndexMismatch,
            "Renderer3D different index-range source was accepted");
        view.primitives[0].firstIndex=0;
        view.primitives[0].lodFade=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "Renderer3D LOD-fade triangle was accepted");
        view.primitives[0].lodFade=false;
        view.primitives[0].batched=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "renderer material-batched primitive was accepted");
        view.primitives[0].batched=false;
        view.primitives[0].shaderVertexOffset=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "shader-deformed vertex geometry cannot be reconstructed");
        view.primitives[0].shaderVertexOffset=false;
        view.primitives[0].gpuLodConditional=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "GPU-selected indirect LOD cannot be guessed in VIVID");
        view.primitives[0].gpuLodConditional=false;
        view.primitives[0].translucent=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "translucent/depth-after source must not claim opaque GPU ID parity");
        view.primitives[0].translucent=false;
        view.primitives[1].nodeIndex=0;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "mesh from a different renderer LOD/node cannot be invented");
        view.primitives[1].nodeIndex=1;
        // Renderer-selected base mesh remains eligible even when an authored
        // LOD exists; the renderer-selected alternative must be rejected.
        model.meshes[0].lods.push_back({1,20.f});
        req(validateSculptGpuRendererView(view,model,0,77)
                .canRenderDiagnostic,
            "actual renderer-selected LOD0 should remain a valid CPU snapshot");
        view.primitives[0].alternativeMeshLod=true;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::RendererUnsupportedView,
            "actual alternate renderer LOD must fail closed");
        view.primitives[0].alternativeMeshLod=false;
        model.meshes[0].lods.clear();
        // Multiple node owners are supported in Renderer3D but NOT by our
        // single-surface CPU picking reconstructor.
        const auto originalOther=view.primitives.back();
        view.primitives.pop_back(); // isolate the selected mesh's owners
        model.nodes[1].mesh=0;
        view.primitives.push_back(view.primitives[0]);
        view.primitives.back().nodeIndex=1;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::MultipleMeshOwners,
            "multiple node owners of same mesh must fail closed");
        model.nodes[1].mesh=1;
        view.primitives.pop_back();
        view.primitives.push_back(originalOther);
        // Renderer3D can draw the same Model twice with identical internal
        // node indices but a different world transform. Such instances must
        // not reuse a single morphed/skinned CPU source surface.
        view.primitives.push_back(view.primitives[0]);
        view.primitives.back().drawInvocation=2;
        req(validateSculptGpuRendererView(view,model,0,77).reason==
                SculptGpuRendererViewReason::MultipleMeshOwners,
            "same-mesh separate renderer draw submissions were conflated");
        view.primitives.pop_back();
        req(check.reason==SculptGpuRendererViewReason::EligibleCpuSnapshot,
            "original valid renderer source snapshot was mutated");
    }
    // 7AX: conservative colour-vs-ID coverage, not raw RGB equality
    // or an inferred depth sample. This tests the pure helper only;
    // actual same-frame Metal pixels are checked in renderer_preview.
    {
        using Pixel=SculptGpuColorPixelResult;
        const std::array<std::uint8_t,3> clear{{20,26,33}};
        const auto covered=compareSculptGpuColorIdCoverage(
            {{182,93,66,255}},{{1,0,0,0}},clear);
        req(covered.verdict==Pixel::Verdict::Covered &&
            covered.triangleIdVisible && covered.distanceFromClear>36,
            "same-frame GPU actor foreground pixel must match colour coverage");
        const auto mismatched=compareSculptGpuColorIdCoverage(
            {{20,26,33,255}},{{1,0,0,0}},clear);
        req(mismatched.verdict==Pixel::Verdict::Mismatch,
            "GPU triangle ID must not confirm an unchanged clear-colour pixel");
        const auto background=compareSculptGpuColorIdCoverage(
            {{20,26,33,255}},{{0,0,0,0}},clear);
        req(background.verdict==Pixel::Verdict::ClearBackground,
            "clear framebuffer and empty GPU ID should agree");
        const auto unrelated=compareSculptGpuColorIdCoverage(
            {{182,93,66,255}},{{0,0,0,0}},clear);
        req(unrelated.verdict==Pixel::Verdict::Inconclusive,
            "other occluders must not be falsely classified as ID mismatch");
        const auto invalid=compareSculptGpuColorIdCoverage(
            {{182,93,66,255}},{{1,0,0,0}},clear,766);
        req(invalid.verdict==Pixel::Verdict::Inconclusive,
            "invalid colour coverage threshold was accepted");
        const auto depthOk=compareSculptGpuDepthPixels(0.2f,0.2008f);
        req(depthOk.samplesValid && depthOk.matching &&
            depthOk.absoluteDifference<0.003f,
            "same-frame native depth tolerance rejected valid GPU samples");
        const auto depthBad=compareSculptGpuDepthPixels(0.2f,0.4f);
        req(depthBad.samplesValid && !depthBad.matching,
            "unrelated nearer occluder depth was incorrectly confirmed");
        const auto depthClear=compareSculptGpuDepthPixels(1.f,1.f);
        req(!depthClear.samplesValid && !depthClear.matching,
            "two clear depth pixels cannot confirm a foreground actor");
        const auto depthNaN=compareSculptGpuDepthPixels(
            std::numeric_limits<float>::quiet_NaN(),0.2f);
        req(!depthNaN.samplesValid && !depthNaN.matching,
            "invalid GPU depth values were accepted as measured parity");
    }
    // 7AW: staging a ModelItem is NOT proof its real colour pipeline
    // emitted a draw. Match one-to-one against the actual renderer-owned
    // accepted main-pass commands, reject pipeline/ICB/LOD/old frame.
    {
        using namespace aiverse::render;
        Model model;
        model.meshes.resize(1);
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        model.meshes[0].primitives={primitive};
        model.nodes.resize(1);
        model.nodes[0].mesh=0;
        Renderer3D::ModelViewProvenance staged;
        staged.hasModel=true;
        staged.complete=true;
        staged.frameSerial=41u;
        Renderer3D::ModelViewPrimitive p;
        p.model=&model;
        p.nodeIndex=0;
        p.meshIndex=0;
        p.primitiveIndex=0;
        p.drawInvocation=1;
        p.firstIndex=0;
        p.indexCount=3;
        p.visible=p.colourCandidate=true;
        p.vertexOverride={71};
        p.vertexOverrideCount=3;
        p.customGpuStream=true;
        p.world=mat4::identity();
        staged.primitives={p};
        Renderer3D::ModelSubmissionProvenance encoded;
        encoded.frameSerial=41u;
        Renderer3D::ModelSubmittedPrimitive item;
        item.source=p;
        item.kind=Renderer3D::ModelSubmissionKind::Direct;
        item.phase=1;
        item.pipeline={7};
        encoded.commands={item};
        encoded.mainPassEncoded=true;
        req(validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "renderer encoded direct source was rejected");
        encoded.mainPassEncoded=false;
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "staged ModelItem falsely claimed main pass submission");
        encoded.mainPassEncoded=true;
        encoded.commands[0].pipeline={};
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "missing compiled GPU pipeline accepted as colour draw");
        encoded.commands[0].pipeline={7};
        encoded.commands[0].kind=
            Renderer3D::ModelSubmissionKind::GpuIcb;
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "GPU ICB visibility was falsely treated as direct primitive");
        encoded.commands[0].kind=
            Renderer3D::ModelSubmissionKind::Direct;
        encoded.commands[0].source.firstIndex=3;
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "actual draw changed source primitive index range");
        encoded.commands[0].source.firstIndex=0;
        encoded.commands[0].source.world=mat4::translate({1,0,0});
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "actual draw changed source model/world transform");
        encoded.commands[0].source.world=mat4::identity();
        encoded.truncated=true;
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "truncated encoded renderer source was accepted");
        encoded.truncated=false;
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,42)
                .accepted,
            "old frame GPU draw was accepted for new click");
        encoded.commands.clear();
        req(!validateSculptGpuSubmittedView(staged,encoded,model,0,41)
                .accepted,
            "no encoded colour draw was accepted as VIVID GPU hit");
    }
    // 7AZ: one main-colour submission for an actor does NOT authorize
    // borrowing another actor's GPU Mask buffer. Both source Models need
    // independent 60B handles and accepted direct draws in the same frame.
    {
        using Renderer=aiverse::render::Renderer3D;
        aiverse::render::Model selected,blocker;
        selected.meshes.resize(1);
        blocker.meshes.resize(1);
        Renderer::ModelViewPrimitive p{};
        p.model=&selected;
        p.meshIndex=0;p.nodeIndex=0;p.primitiveIndex=0;
        p.drawInvocation=1;p.indexCount=3;
        p.visible=p.colourCandidate=p.customGpuStream=true;
        p.vertexOverride={71};p.vertexOverrideCount=3;
        Renderer::ModelViewProvenance sourceView{};
        sourceView.frameSerial=55;
        sourceView.hasModel=sourceView.complete=true;
        sourceView.primitives={p};
        Renderer::ModelSubmissionProvenance sourceSubmitted{};
        sourceSubmitted.frameSerial=55;
        sourceSubmitted.mainPassEncoded=true;
        Renderer::ModelSubmittedPrimitive command{};
        command.source=p;command.kind=Renderer::ModelSubmissionKind::Direct;
        command.phase=1;command.pipeline={7};
        sourceSubmitted.commands={command};

        p.model=&blocker;p.vertexOverride={72};
        p.drawInvocation=2;
        Renderer::ModelViewProvenance maskView{};
        maskView.frameSerial=55;
        maskView.hasModel=maskView.complete=true;
        maskView.primitives={p};
        Renderer::ModelSubmissionProvenance maskSubmitted{};
        maskSubmitted.frameSerial=55;
        maskSubmitted.mainPassEncoded=true;
        command.source=p;command.pipeline={8};
        maskSubmitted.commands={command};

        auto paired=[&] {
            return sculptGpuPairedRendererViewsAccepted(
                sourceView,sourceSubmitted,selected,0,
                maskView,maskSubmitted,blocker,0,55);
        };
        req(paired(),
            "separate submitted Renderer3D actor sources were rejected");
        maskView.primitives[0].vertexOverride={71};
        maskSubmitted.commands[0].source.vertexOverride={71};
        req(!paired(),"shared selected/occluder GPU handle was accepted");
        maskView.primitives[0].vertexOverride={72};
        maskSubmitted.commands[0].source.vertexOverride={72};
        maskSubmitted.frameSerial=56;
        req(!paired(),"stale GPU Mask renderer submission was accepted");
        maskSubmitted.frameSerial=55;
        maskSubmitted.commands[0].source.indexCount=6;
        req(!paired(),"changed Mask submitted primitive was accepted");
        maskSubmitted.commands[0].source.indexCount=3;
        maskSubmitted.commands.clear();
        req(!paired(),"missing Mask draw was accepted");
    }
    // 7AQ: no false claim of hardware pixel parity when the normal
    // Renderer3D may draw an alternate authored LOD or an ambiguously
    // instanced mesh. In those cases the CPU BVH remains authoritative.
    {
        aiverse::render::Model model;
        model.meshes.resize(2);
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[1].mesh=1;
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::Eligible,
            "ordinary selected-mesh ID pick should be supported");
        model.meshes[1].lods.push_back({0,50.f});
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::AuthoredLodSelection,
            "another scene mesh's authored LOD must disallow unverified picking");
        model.meshes[1].lods.clear();
        model.meshes[0].lods.push_back({1,20.f});
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::AuthoredLodSelection,
            "selected face authored LOD mismatch must fail closed");
        model.meshes[0].lods.clear();
        model.nodes[1].mesh=0;
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::AmbiguousOwner,
            "same mesh under multiple owners cannot share one source pose");
        model.nodes[1].mesh=-1;
        model.nodes[0].mesh=-1;
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::MissingOwner,
            "unreferenced mesh should not be treated as visible");
        model.nodes[0].mesh=99;
        req(sculptGpuPickEligibility(model,0)==
                SculptGpuPickEligibility::InvalidSceneMesh,
            "corrupt scene mesh node must disallow GPU confirmation");
    }
    // 7AM: a cached palette plan must match the original reference
    // under animated joint updates and non-unit integer-weight totals.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        for(auto& v:model.vertices) {
            v.joints[0]=0;v.weights[0]=128;
            v.joints[1]=1;v.weights[1]=64;
        }
        model.indices={0,1,2};
        Primitive prim;
        prim.firstIndex=0;prim.indexCount=3;
        prim.jointPalette={0,1};
        MeshDef mesh;
        mesh.primitives.push_back(prim);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(3);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1,2};
        model.skins[0].inverseBind={mat4{},mat4{}};
        SculptSkinningPlan plan;
        req(plan.build(model,0) && plan.ready() &&
            plan.jointCount()==2,"cached skin palette plan build failed");
        SkinnedWeightSurface reference,cached,scratch;
        for(float offset:{1.f,-2.f,3.f}) {
            const std::array<mat4,3> pose{{
                mat4{},mat4::translate({0,0,offset}),
                mat4::translate({0,0,offset*2.f})
            }};
            req(prepareSkinnedWeightSurface(model,0,pose,reference) &&
                plan.evaluate(model,pose,{},cached,scratch) &&
                cached.ready && cached.included==reference.included,
                "cached skin plan failed to reconstruct reference surface");
            for(std::size_t i=0;i<model.vertices.size();++i)
                req(std::fabs(reference.world[i].x-cached.world[i].x)<1.e-5f &&
                    std::fabs(reference.world[i].y-cached.world[i].y)<1.e-5f &&
                    std::fabs(reference.world[i].z-cached.world[i].z)<1.e-5f,
                    "cached joint transforms changed skinned CPU geometry");
        }
        const auto good=cached.world[0];
        auto invalid=std::array<mat4,3>{mat4{},mat4{},mat4{}};
        invalid[1].at(0,3)=std::numeric_limits<float>::infinity();
        req(!plan.evaluate(model,invalid,{},cached,scratch) &&
            cached.ready && cached.world[0].z==good.z,
            "invalid joint pose mutated last-good skin-pick surface");
        // A malformed candidate must not replace an already valid cached
        // influence plan. A real editor rebuild explicitly resets it once
        // the candidate is successfully published.
        model.meshes[0].primitives[0].jointPalette={0};
        req(!plan.build(model,0) && plan.ready() &&
            plan.jointCount()==2,
            "failed skin plan build replaced the last-good plan");
        plan.clear();
        req(!plan.build(model,0) && !plan.ready(),
            "invalid cached skin palette must fail closed");
    }
    // 7AL: the accelerated picker must be equivalent to the original
    // triangle-order reference for neutral and animated morph poses.
    // On a dense planar grid a narrow ray should visit far fewer triangles
    // than the total model; repeated clicks retain CPU buffer capacities.
    {
        using namespace aiverse::render;
        constexpr int side=41;
        Model model;
        model.vertices.resize(side*side);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x)
            model.vertices[std::size_t(y*side+x)].pos={
                float(x)/float(side-1),float(y)/float(side-1),0};
        for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x) {
            const auto a=std::uint32_t(y*side+x),b=a+1,c=a+side;
            model.indices.insert(model.indices.end(),{
                a,b,c,b,c+1,c
            });
        }
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=std::uint32_t(model.indices.size());
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        Node owner;
        owner.mesh=0;
        model.nodes.push_back(owner);
        const std::array<mat4,1> pose{{mat4{}}};
        MorphTargetInput target;
        target.name="lift";
        MorphDelta delta;
        delta.vertex=std::uint32_t((side/2)*side+side/2);
        delta.position={0,0,0.1f};
        target.deltas.push_back(delta);
        const auto morphs=SparseMorphLibrary::compile(
            model.vertices.size(),{target});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.f}
        }};
        const auto binding=FaceShapeBinding::compile(morphs,routes);
        DeformedSculptWorkspace workspace;
        FaceFrame face;
        for(float expression:{0.f,1.f}) {
            face.values[std::size_t(Channel::JawOpen)]=expression;
            for(float x:{0.12f,0.26f,0.51f,0.82f}) {
                const float3 origin{x,0.51f,2.f},direction{0,0,-1};
                const auto brute=pickDeformedSculptVertex(
                    model,0,pose,morphs,binding,face,origin,direction,
                    model.vertices.size());
                const auto fast=workspace.pick(
                    model,0,pose,morphs,binding,face,origin,direction,
                    model.vertices.size());
                req(brute.hit && fast.hit && brute.vertex==fast.vertex &&
                    std::fabs(brute.rayDistance-fast.rayDistance)<1.e-5f &&
                    std::fabs(brute.worldPosition.x-fast.worldPosition.x)<1.e-5f &&
                    std::fabs(brute.worldPosition.y-fast.worldPosition.y)<1.e-5f &&
                    std::fabs(brute.worldPosition.z-fast.worldPosition.z)<1.e-5f,
                    "refitted BVH sculpt hit differs from full-triangle reference");
            }
        }
        req(workspace.stats().triangles==model.indices.size()/3 &&
            workspace.stats().testedTriangles<
                workspace.stats().triangles/4,
            "BVH did not cull triangle candidates for a narrow ray");
        const auto warmCapacity=workspace.vertexCapacity();
        face.values[std::size_t(Channel::JawOpen)]=0.5f;
        req(workspace.pick(model,0,pose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit &&
            workspace.vertexCapacity()==warmCapacity,
            "sculpt workspace unexpectedly grew after warm-up");
        auto badPose=pose;
        badPose[0].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        req(!workspace.pick(model,0,badPose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "refitted picking accepted a nonfinite node pose");
        req(workspace.pick(model,0,pose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "valid subsequent pick did not recover after rejected pose");
        // Both scratch layers must leave their previous published outputs
        // untouched when the next pose/weights are rejected.
        std::vector<float3> bindPositions,bindScratch;
        const std::array<float,1> goodWeights{{1.f}};
        const std::array<float,1> badWeights{{
            std::numeric_limits<float>::quiet_NaN()
        }};
        req(prepareSculptMorphPositions(model,morphs,goodWeights,
                                        bindPositions,&bindScratch),
            "initial morph scratch reconstruction failed");
        const auto originalZ=bindPositions[std::size_t(delta.vertex)].z;
        req(!prepareSculptMorphPositions(model,morphs,badWeights,
                                         bindPositions,&bindScratch) &&
            bindPositions[std::size_t(delta.vertex)].z==originalZ,
            "failed morph reconstruction overwrote last-good bind positions");
        SkinnedWeightSurface skinSurface;
        SkinnedWeightScratch skinScratch;
        req(prepareSkinnedWeightSurface(model,0,pose,skinSurface,
                                         bindPositions,&skinScratch),
            "initial skinned scratch reconstruction failed");
        const auto originalWorld=skinSurface.world[std::size_t(delta.vertex)].z;
        req(!prepareSkinnedWeightSurface(model,0,badPose,skinSurface,
                                          bindPositions,&skinScratch) &&
            skinSurface.ready &&
            skinSurface.world[std::size_t(delta.vertex)].z==originalWorld,
            "failed skin reconstruction overwrote last-good world positions");
        workspace.reset();
        req(workspace.stats().triangles==0 &&
            workspace.pick(model,0,pose,morphs,binding,face,
                {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "BVH did not rebuild after explicit candidate reset");
    }
    // 7AJ: published GPU morph is sampled in bind space BEFORE skinning.
    // Picking must return the original vertex ID and final world position,
    // including the case where the morphed triangle differs from bind pose.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        for(auto& v:model.vertices) {
            v.joints[0]=0;
            v.weights[0]=255;
        }
        model.indices={0,1,2};
        MeshDef mesh;
        Primitive prim;
        prim.firstIndex=0;
        prim.indexCount=3;
        prim.jointPalette={0};
        mesh.primitives.push_back(prim);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1};
        model.skins[0].inverseBind={mat4{}};
        const std::array<mat4,2> pose{{mat4{},mat4::translate({0,0,2})}};
        MorphTargetInput shape;
        shape.name="lift";
        MorphDelta delta;
        delta.vertex=0;
        delta.position={0.4f,0,0};
        shape.deltas.push_back(delta);
        const auto library=SparseMorphLibrary::compile(3,{shape});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.0f}
        }};
        const auto binding=FaceShapeBinding::compile(library,routes);
        FaceFrame face;
        face.values[std::size_t(Channel::JawOpen)]=1.0f;
        const auto hit=pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.42f,0.05f,4.0f},{0,0,-1},3);
        req(hit.hit && hit.vertex==0 &&
            std::fabs(hit.worldPosition.x-0.4f)<1.e-5f &&
            std::fabs(hit.worldPosition.z-2.0f)<1.e-5f,
            "morphed/skinned sculpt pick missed actual GPU-visible vertex");
        face.values[std::size_t(Channel::JawOpen)]=0.0f;
        const auto neutral=pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.05f,0.05f,4.0f},{0,0,-1},3);
        req(neutral.hit && neutral.vertex==0 &&
            std::fabs(neutral.worldPosition.x)<1.e-6f &&
            std::fabs(neutral.worldPosition.z-2.0f)<1.e-5f,
            "neutral morph/rig picking differs from expected skinned geometry");
        req(!pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{2,2,4},{0,0,-1},3).hit,
            "deformed sculpt accepted ray missing all triangles");
        req(!pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},2).hit,
            "deformed sculpt authored generated/non-source triangle");
        auto badPose=pose;
        badPose[1].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        req(!pickDeformedSculptVertex(model,0,badPose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},3).hit,
            "invalid MotionWeave pose did not fail closed");
        auto badModel=model;
        badModel.meshes[0].primitives[0].jointPalette.clear();
        req(!pickDeformedSculptVertex(badModel,0,pose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},3).hit,
            "invalid skin palette accepted for deformed sculpt pick");
        auto shortLibrary=SparseMorphLibrary::compile(2,{});
        std::vector<float3> scratch{{9,9,9}};
        req(!prepareSculptMorphPositions(model,shortLibrary,{},scratch) &&
            scratch.size()==1 && scratch[0].x==9,
            "invalid morph source changed last-good picking scratch");
    }
    // 7AK: after palette seam splitting the *visible* triangle can use
    // cloned global indices, but sculpting must still edit the author's
    // original source vertex. Generated LOD IDs stay unselectable.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(5);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.vertices[3]=model.vertices[0]; // seam clone of vertex zero
        model.vertices[4]=model.vertices[0]; // generated LOD, not authorable
        for(auto& v:model.vertices){v.joints[0]=0;v.weights[0]=255;}
        model.indices={3,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        primitive.jointPalette={0};
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1};
        model.skins[0].inverseBind={mat4{}};
        const std::array<mat4,2> pose{{mat4{},mat4::translate({0,0,2})}};
        MorphTargetInput shape;
        shape.name="lift";
        for(std::uint32_t vertex:{0u,3u}) {
            MorphDelta delta;
            delta.vertex=vertex;
            delta.position={0.4f,0,0};
            shape.deltas.push_back(delta);
        }
        const auto library=SparseMorphLibrary::compile(5,{shape});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.0f}
        }};
        const auto binding=FaceShapeBinding::compile(library,routes);
        FaceFrame face;
        face.values[std::size_t(Channel::JawOpen)]=1.0f;
        std::vector<std::uint32_t> authorIds;
        const std::array<SkinSeamSplit::Clone,1> clones{{
            {0,3,0,0}
        }};
        req(buildSculptSourceVertexMap(5,3,clones,authorIds) &&
            authorIds.size()==5 && authorIds[0]==0 &&
            authorIds[3]==0 &&
            authorIds[4]==std::numeric_limits<std::uint32_t>::max(),
            "seam mapping must preserve source IDs and exclude generated LODs");
        const auto hit=pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,authorIds);
        req(hit.hit && hit.vertex==0 &&
            std::fabs(hit.worldPosition.x-0.4f)<1.e-5f &&
            std::fabs(hit.worldPosition.z-2.0f)<1.e-5f,
            "seam clone did not resolve to original source vertex");
        DeformedSculptWorkspace cached;
        const auto accelerated=cached.pick(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,authorIds);
        req(accelerated.hit && accelerated.vertex==hit.vertex &&
            std::fabs(accelerated.worldPosition.x-hit.worldPosition.x)<1.e-5f &&
            std::fabs(accelerated.rayDistance-hit.rayDistance)<1.e-5f,
            "accelerated BVH failed seam clone original-source ID remapping");
        model.indices[0]=4; // same triangle geometry, only LOD index changes
        req(!pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.05f,0.05f,4.f},{0,0,-1},3,authorIds).hit,
            "generated LOD triangle leaked into original sculpt vertex selection");
        // Changing topology in-place requires a BVH reset; normal Creator
        // candidate publication always performs it before any new click.
        cached.reset();
        req(!cached.pick(model,0,pose,library,binding,face,
            {0.05f,0.05f,4.f},{0,0,-1},3,authorIds).hit,
            "BVH selected an unauthorable generated LOD triangle");

        const std::array<SkinSeamSplit::Clone,2> sourceAndLodClones{{
            {0,3,0,0},{4,5,0,1}
        }};
        std::vector<std::uint32_t> expandedIds;
        req(buildSculptSourceVertexMap(6,3,sourceAndLodClones,expandedIds) &&
            expandedIds.size()==6 && expandedIds[3]==0 &&
            expandedIds[5]==std::numeric_limits<std::uint32_t>::max(),
            "LOD seam clones must remain excluded without blocking publication");
        const auto lastGood=authorIds;
        const std::array<SkinSeamSplit::Clone,2> duplicated{{
            {0,3,0,0},{1,3,0,0}
        }};
        req(!buildSculptSourceVertexMap(5,3,duplicated,authorIds) &&
            authorIds==lastGood,
            "duplicate clone mapping must fail without overwriting last-good map");
        const std::array<SkinSeamSplit::Clone,1> invalid{{
            {8,3,0,0}
        }};
        req(!buildSculptSourceVertexMap(5,3,invalid,authorIds) &&
            authorIds==lastGood,
            "invalid clone source ID must preserve last-good map");
        const std::array<std::uint32_t,1> shortMapping{{0}};
        model.indices[0]=3;
        req(!pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,shortMapping).hit,
            "truncated published vertex mapping must reject picking");
    }
    {
        auto target=MorphTargetInput{};
        target.name="symmetric";
        auto sculpt=MorphAuthoring::fromCompiled(
            SparseMorphLibrary::compile(2,{target}));
        const std::array<MorphVec3,2> source{{{-1,0,0},{1,0,0}}};
        req(sculpt.brush(0,source,0,0.2f,{0.1f,0,0},true),
            "bilateral sculpt stroke rejected");
        const auto compiledSculpt=sculpt.compile();
        const auto gpu=compiledSculpt.gpuDeltas();
        req(gpu.size()==2&&gpu[0].position[0]>0.09f &&
            gpu[1].position[0]<-0.09f,
            "sculpt symmetry did not mirror horizontal displacement");
        req(sculpt.undo()&&sculpt.compile().deltas()==0,
            "symmetric brush did not undo as one transaction");
    }
    {
        MorphTargetInput shape;
        shape.name="eyeBlinkLeft";
        MorphDelta delta;
        delta.vertex=2;delta.position={0,0.1f,0};
        shape.deltas.push_back(delta);
        auto source=SparseMorphLibrary::compile(4,{shape});
        const std::array<std::uint32_t,3> mapping{{0,2,3}};
        const std::array<aiverse::render::float3,4> sourcePositions{{
            {0,0,0},{1,0,0},{2,0,0},{3,0,0}}};
        const std::array<aiverse::render::float3,3> lodPositions{{
            {0,0,0},{2,0,0},{3,0,0}}};
        req(MorphLodTransfer::matchPositions(sourcePositions,lodPositions)==
            std::vector<std::uint32_t>(mapping.begin(),mapping.end()),
            "exact geometric LOD matching returned incorrect indices");
        auto lod=MorphLodTransfer::transfer(source,mapping);
        req(lod.vertices()==3 && lod.targets()==1 && lod.deltas()==1 &&
            lod.gpuDeltas()[0].vertex==1 &&
            std::fabs(lod.gpuDeltas()[0].position[1]-0.1f)<1.e-6f,
            "facial LOD source vertex mapping lost corrective delta");
        bool rejected=false;
        const std::array<std::uint32_t,1> bad{{100}};
        try {(void)MorphLodTransfer::transfer(source,bad);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"facial LOD accepted invalid original vertex ID");
    }
    {
        // Independently decimated vertex inside a source face has no exact
        // source-vertex match, but receives barycentrically blended deltas.
        const std::array<aiverse::render::float3,3> sourcePositions{{
            {0,0,0},{2,0,0},{0,2,0}}};
        const std::array<std::uint32_t,3> faces{{0,1,2}};
        const std::array<aiverse::render::float3,2> lodPositions{{
            {0.5f,0.5f,0},{0.25f,0.25f,0.01f}}};
        const auto correspondence=MorphLodSurface::project(
            sourcePositions,faces,lodPositions,0.05f);
        const auto accelerated=MorphLodSurface::projectAccelerated(
            sourcePositions,faces,lodPositions,0.05f);
        req(accelerated.size()==correspondence.size(),
            "BVH LOD bake returned different number of mappings");
        for(std::size_t i=0;i<accelerated.size();++i) {
            req(accelerated[i].triangle==correspondence[i].triangle &&
                std::fabs(accelerated[i].bary.x-correspondence[i].bary.x)<1.e-5f &&
                std::fabs(accelerated[i].bary.y-correspondence[i].bary.y)<1.e-5f &&
                std::fabs(accelerated[i].bary.z-correspondence[i].bary.z)<1.e-5f,
                "BVH facial LOD projection differs from reference");
        }
        req(correspondence.size()==2 &&
            correspondence[0].triangle==0 &&
            std::fabs(correspondence[0].bary.x-0.5f)<1.e-5f &&
            std::fabs(correspondence[0].bary.y-0.25f)<1.e-5f &&
            std::fabs(correspondence[0].bary.z-0.25f)<1.e-5f,
            "surface projected LOD barycentric coordinates incorrect");
        MorphTargetInput shape;
        shape.name="faceLift";
        MorphDelta a,b;
        a.vertex=0;a.position={0,1,0};
        b.vertex=1;b.position={0,2,0};
        shape.deltas={a,b};
        const auto sourceMorph=SparseMorphLibrary::compile(3,{shape});
        MorphLodMapping mappingSidecar;
        mappingSidecar.sourceVertices=3;
        mappingSidecar.sourceTriangles=1;
        mappingSidecar.targetVertexIds={4,7};
        mappingSidecar.sourceFingerprint=MorphLodMapping::fingerprint(sourcePositions,faces);
        mappingSidecar.samples=correspondence;
        const auto restored=MorphLodMapping::fromJson(mappingSidecar.toJson());
        req(restored.samples.size()==2 &&
            restored.targetVertexIds==std::vector<std::uint32_t>({4,7}) &&
            std::fabs(restored.samples[0].bary.x-0.5f)<1.e-5f,
            "LOD barycentric sidecar lost mapping data");
        const auto restoredMorphs=restored.transfer(sourceMorph,sourcePositions,faces);
        req(restoredMorphs.targets()==1 && restoredMorphs.deltas()==2,
            "LOD sidecar did not reapply stored surface projection");
        auto alteredSource=sourcePositions;
        alteredSource[0].x=0.2f;
        bool fingerprintRejected=false;
        try {(void)restored.transfer(sourceMorph,alteredSource,faces);}
        catch(const std::invalid_argument&){fingerprintRejected=true;}
        req(fingerprintRejected,
            "LOD sidecar accepted changed same-size source geometry");
        const auto projected=MorphLodSurface::transfer(
            sourceMorph,faces,correspondence);
        req(projected.vertices()==2 && projected.targets()==1 &&
            projected.deltas()==2 &&
            std::fabs(projected.gpuDeltas()[0].position[1]-1.0f)<1.e-5f,
            "decimated facial LOD did not interpolate source morph");
        bool rejected=false;
        const std::array<aiverse::render::float3,1> tooFar{{{0.5f,0.5f,1}}};
        try {(void)MorphLodSurface::project(sourcePositions,faces,tooFar,0.01f);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"LOD projected a vertex outside authored surface tolerance");
    }
    {
        // A single actor CSR must contain both original facial mesh vertices
        // and independently authored LOD vertices with the same target weight.
        MorphTargetInput original;
        original.name="jawOpen";
        MorphDelta a;
        a.vertex=0;
        a.position={0,0.2f,0};
        original.deltas.push_back(a);
        const auto full=SparseMorphLibrary::compile(5,{original});
        MorphTargetInput lodTarget;
        lodTarget.name="jawOpen";
        MorphDelta b;
        b.vertex=1;
        b.position={0,0.4f,0};
        lodTarget.deltas.push_back(b);
        const auto compact=SparseMorphLibrary::compile(2,{lodTarget});
        const std::array<std::uint32_t,2> ids{{3,4}};
        const auto merged=MorphLodGpuAuthoring::combine(full,compact,ids);
        req(merged.vertices()==5 && merged.targets()==1 &&
            merged.deltas()==2 && merged.gpuDeltas()[0].vertex==0 &&
            merged.gpuDeltas()[1].vertex==4,
            "full-model GPU actor CSR omitted source or authored LOD delta");
        MorphTargetInput additional;
        additional.name="jawOpen";
        MorphDelta third;
        third.vertex=0;
        third.position={0,0.6f,0};
        additional.deltas.push_back(third);
        const auto secondCompact=SparseMorphLibrary::compile(1,{additional});
        const std::array<std::uint32_t,1> secondIds{{2}};
        const std::array<MorphLodGpuAuthoring::Layer,2> layers{{
            {&compact,ids},{&secondCompact,secondIds}}};
        const auto combined=MorphLodGpuAuthoring::combineMany(full,layers);
        req(combined.vertices()==5 && combined.targets()==1 &&
            combined.deltas()==3 &&
            combined.gpuDeltas()[0].vertex==0 &&
            combined.gpuDeltas()[1].vertex==2 &&
            combined.gpuDeltas()[2].vertex==4,
            "multi-LOD facial CSR missing shared semantic deltas");
        const std::array<std::uint32_t,1> conflictingIds{{4}};
        const std::array<MorphLodGpuAuthoring::Layer,2> conflictLayers{{
            {&compact,ids},{&secondCompact,conflictingIds}}};
        bool multiRejected=false;
        try {(void)MorphLodGpuAuthoring::combineMany(full,conflictLayers);}
        catch(const std::invalid_argument&){multiRejected=true;}
        req(multiRejected,"overlapping multi-LOD target vertices accepted");
        const std::array<std::uint32_t,2> overlap{{0,4}};
        bool rejected=false;
        try {(void)MorphLodGpuAuthoring::combine(full,compact,overlap);}
        catch(const std::invalid_argument&){rejected=true;}
        req(!rejected,
            "LOD delta on unused target index must not conflict with source");
        MorphDelta c;
        c.vertex=0;
        c.position={0,0.1f,0};
        lodTarget.deltas.push_back(c);
        const auto overlapCompact=SparseMorphLibrary::compile(2,{lodTarget});
        rejected=false;
        try {(void)MorphLodGpuAuthoring::combine(full,overlapCompact,overlap);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"GPU actor LOD CSR accepted overlapping authored deltas");
    }
    // Failure to replace a destination MUST leave the last-good user
    // data intact. A directory target makes rename fail on every platform,
    // even where ordinary file replacement by rename is supported.
    const auto protectedDir=std::filesystem::temp_directory_path()/
        "aiverse-vivid-human-creator-protected";
    std::filesystem::create_directories(protectedDir);
    const auto sentinel=protectedDir/"last-good-dna.json";
    {
        std::ofstream out(sentinel,std::ios::binary|std::ios::trunc);
        out<<"{\\\"preserve\\\":true}";
    }
    req(!session.saveFile(protectedDir.string(),&error) &&
        std::filesystem::is_directory(protectedDir) &&
        std::filesystem::exists(sentinel),
        "failed DNA replace destroyed the existing destination");
    std::filesystem::remove(protectedDir.string()+".vivid.tmp");
    std::filesystem::remove(sentinel);
    std::filesystem::remove(protectedDir);
    std::filesystem::remove(file);
}
}
int main(){try{test();std::cout<<"VIVID creator transactions PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID creator FAIL: "<<e.what()<<"\n";return 1;}return 0;}
