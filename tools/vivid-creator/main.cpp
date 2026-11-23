#include "engine/render/PostFx.h"
#include "engine/character/vivid/HumanPreviewAnimation.h"
#include "engine/character/vivid/HumanWalkPreview.h"
#include "engine/character/vivid/HumanStudioLighting.h"
// VIVID Creator: standalone non-destructive digital-human authoring and
// actual Renderer3D preview. Uses AIVERSE's existing ImGui/RHI/Model and
// MotionWeave animation systems; no duplicate game simulation.
#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/HumanPreview.h"
#include "engine/character/vivid/VividPresentationSettings.h"
#include "engine/character/vivid/VividRendererQuality.h"
#include "engine/character/vivid/HumanImport.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/MorphLodMapping.h"
#include "engine/character/vivid/MorphLodGpuAuthoring.h"
#include "engine/character/vivid/ViewportSculptPick.h"
#include "engine/character/vivid/SkinnedWeightPick.h"
#include "engine/character/vivid/DeformedSculptPick.h"
#include "engine/character/vivid/DeformedSculptWorkspace.h"
#include "engine/character/vivid/SculptGpuIdPass.h"
#include "engine/character/vivid/SculptGpuPickEligibility.h"
#include "engine/character/vivid/SculptGpuRendererView.h"
#include "engine/character/vivid/SculptGpuSubmittedView.h"
#include "engine/character/vivid/SculptGpuSceneOccluders.h"
#include "engine/character/vivid/SkinWeightLodTransfer.h"
#include "engine/character/vivid/SkinSeamSplit.h"
#include "engine/character/vivid/SkinLodSeamRemap.h"
#include "engine/character/vivid/RigAuthoring.h"
#include "engine/character/vivid/BindPoseRebind.h"
#include "engine/character/vivid/SkinWeightAuthoring.h"
#include "engine/character/vivid/ScalpSkinning.h"
#include "engine/character/vivid/GpuActorStream.h"
#include "engine/character/vivid/HumanRetirementQueue.h"
#include "engine/character/vivid/CpuActorStream.h"
#include "engine/character/vivid/GpuMorphAssetCache.h"
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include <aiverselib/motionweave/MotionWeave.h>
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "tools/vivid-creator/OpticalCaptureCases.h"
#include <fstream>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
using namespace render;
using namespace vivid;
struct CreatorApp {
    gfx::Device* device=nullptr;
    Renderer3D renderer;
    ImGuiLayer ui;
    Mesh floor;
    std::vector<float3> floorCpuVertices;
    std::vector<std::uint32_t> floorCpuIndices;
    HumanCreatorSession session;
    HumanDNA draft;
    HumanPreviewAsset human;
    std::unique_ptr<GpuMorphAssetCache> cache;
    std::unique_ptr<HumanRetirementQueue> retiredHumans;
    std::unique_ptr<GpuActorStream> actor;
    std::unique_ptr<CpuActorStream> cpuActor;
    std::unique_ptr<World> world;
    Entity entity{};
    std::shared_ptr<const AnimationGraph> graph;
    std::vector<mat4> facePose;
    std::vector<mat4> rigPoseScratch;
    RigAuthoring rigCorrections;
    SkinWeightAuthoring skinWeightEdits;
    bool authorBindPose=false;
    int skinVertexId=0,skinJointNode=0,skinMeshIndex=-1;
    float skinWeightValue=0.5f,skinBrushRadius=0.12f,skinBrushStrength=0.5f;
    bool skinBrushArmed=false,skinMirrorX=false,skinHeatmap=false;
    int skinStrokeLastVertex=-1;
    char skinWeightFile[512]="/tmp/vivid-skin-weights.json";
    int selectedRigNode=0;
    float rigOffset[3]={0,0,0};
    char rigCorrectionFile[512]="/tmp/vivid-rig-corrections.json";
    FaceController controller{0.04f};
    Camera3D camera;
    bool pendingRebuild=true, rendererReady=false, uiReady=false;
    bool animate=true, inspectGroom=false, wetPreview=false;
    bool smoke=false, smokeCaptureRequested=false, smokeCompleted=false;
    bool portraitCapture=false,closeupCapture=false,eyeDetailCapture=false;
    bool previewMotion=false,previewWalk=false;int previewPose=0;float previewTime=-1;
    std::string previewBvhPath;
    motionweave::AnimationPoseComponent previewCapturePose;
    int sequenceFrames=0,sequenceFrame=0,sequenceSampled=-1,sequenceBase=-1,walkClip=-1;
    bool automaticLod=true;
    render::PostFx antialiasing;bool antialiasingReady=false,aaEnabled=true;
    bool previewHeadSweep=false,previewFixedPose=false,previewBindPose=false,previewLightSweep=false;
    float previewFootDrift=0;size_t previewFootSamples=0;
    float captureCameraDistance=0;
    ShadowQuality captureShadowQuality=ShadowQuality::High;
    render::AdaptiveShadowResolution shadowResolution;
    render::Animator sequenceAnimator; // offline diagnostic only; no runtime clock

    bool studioLightingEnabled=false;
    vivid::HumanStudioLighting studioLighting;
    float3 studioFoot{};float studioHeight=3.8f;
    bool forceCpuMorph=false; // explicit fallback acceptance on compute hosts
    int smokeFrames=0, smokeReady=0;
    std::string smokePath="/tmp/aiverse-vivid-creator.ppm";
    VividPresentationSettings presentation{};
    bool groomSelfCollision=true,groomSegmentCollision=true;
    float groomSelfSeparation=0.014f,groomSelfResponse=0.6f;
    float groomFriction=0.25f,groomCurlPreservation=0.3f;
    float jaw=0.0f, smile=0.0f, blink=0.0f, yaw=0.18f;
    float wind=0.15f;
    int draftGuides=512,draftSegments=8;
    std::array<char,128> rootBone{},headBone{},jawBone{},leftEyeBone{},rightEyeBone{};
    char recipeFile[512]="/tmp/aiverse-vivid-human.json";
    char presentationFile[512]="/tmp/aiverse-vivid-presentation.json";
    char manifestFile[512]="/tmp/aiverse-human-package.json";
    std::string importedManifest;
    HumanImportManifest importedPackage;
    std::optional<HumanAppearance> appearance, appearanceOverride;
    std::unique_ptr<MorphAuthoring> morphDraft;
    std::size_t morphSourceVertices=0;
    std::vector<MorphVec3> morphSourcePositions;
    std::vector<std::uint32_t> sculptSourceIds; // candidate-published source IDs
    DeformedSculptWorkspace sculptPicker; // persistent per-creator CPU scratch/BVH
    SculptGpuIdPass gpuSculptPass; // native multi-backend optional ID checker
    SculptGpuPickProtocol gpuSculptToken;
    SculptGpuPickRequest gpuSculptToRecord{};
    float3 gpuSculptRayOrigin{},gpuSculptRayDirection{};
    std::uint64_t gpuSculptPublication=1;
    int gpuSculptExpectedSourceVertex=-1;
    bool gpuSculptRendererUsedOverride=false;
    bool gpuSculptEnabled=false;
    float morphBrushRadius=0.1f;
    bool sculptMirrorX=false;
    int sculptMeshIndex=-1;
    int sourceLodMesh=-1,targetLodMesh=-1;
    float lodProjectionTolerance=0.02f;
    struct AuthoredFacialLod {
        MorphLodMapping mapping;
        std::vector<std::uint32_t> sourceIndices;
        int targetMesh=-1;
    };
    std::vector<AuthoredFacialLod> authoredFacialLods;
    bool lodGpuMorphEnabled=false;
    bool sculptPickArmed=false;
    render::float3 sculptPickMarkerWorld{};
    bool sculptPickMarkerValid=false;
    int sculptPickMarkerVertex=-1,sculptPickMarkerMesh=-1;
    int morphTargetIndex=0,morphVertexIndex=0;
    int routeChannelIndex=0;
    char newMorphName[129]="newCorrective";
    render::float3 morphEditOffset{};
    bool morphDirty=false;
    std::array<char,129> scalpProxyName{};
    // Immutable bind-pose vertices and a single rigid MotionWeave node.
    // Updated positions are always supplied in the groom's local space.
    std::vector<render::float3> scalpBindPositions;
    std::vector<render::float3> scalpAnimatedPositions;
    std::vector<render::float3> scalpSkinScratch;
    int scalpPoseNode=-1;
    std::unique_ptr<ScalpSkinning> scalpSkinning;
    render::mat4 scalpBindNodeInverse{};
    render::mat4 scalpBindHeadRigid{};
    bool externalMode=false;
    bool commandLineAsset=false;
    std::string status;
    std::size_t displayedRevision=0;

    CreatorApp():draft(session.dna()) {syncDraft();}
    void syncDraft() {
        draft=session.dna();
        draftGuides=int(draft.groom.guideCount);
        draftSegments=int(draft.groom.segments);
        auto copy=[](std::array<char,128>& dst,const std::string& source) {
            std::snprintf(dst.data(),dst.size(),"%s",source.c_str());
        };
        copy(rootBone,draft.rig.rootBone);
        copy(headBone,draft.rig.headBone);
        copy(jawBone,draft.rig.jawBone);
        copy(leftEyeBone,draft.rig.leftEyeBone);
        copy(rightEyeBone,draft.rig.rightEyeBone);
    }
    void init(gfx::Device& dev) {
        device=&dev;
        if(!renderer.init(dev))throw std::runtime_error("VIVID Creator: renderer init failed");
        rendererReady=true;
        renderer.setModelLodEnabled(automaticLod);
        renderer.setLodDiagnosticsEnabled(true);
        if(!antialiasing.init(dev))throw std::runtime_error("VIVID anti-aliasing init failed");
        antialiasingReady=true;
        PostFxSettings aa;aa.hdr=HdrMode::Off;aa.enabled=aaEnabled;
        aa.effects.push_back(defaultPostFxEffect(PostFxType::Fxaa));
        antialiasing.setSettings(std::move(aa));antialiasing.setQuality(PostFxQuality::Medium);
        applyVividMaterialQuality(renderer,presentation);
        if(!ui.init(dev,AIVERSE_SOURCE_DIR "/engine/assets/fonts/ui.ttf",16.0f))
            throw std::runtime_error("VIVID Creator: ImGui init failed");
        uiReady=true;
        renderer.setShadowQuality(captureShadowQuality);
        renderer.setModelReflectionsEnabled(false);
        renderer.setTraceEnabled(false);
        renderer.setModelInstancingEnabled(false);
        renderer.setModelMultiDrawEnabled(false);
        renderer.setShadowBounds({-4,-4,-4},{4,4,4});
        renderer.lighting.fogDensity=0.0f;
        renderer.lighting.sunIntensity=1.35f;
        // Frontal authoring key keeps eye optics visible below the brow.
        renderer.lighting.sunDir={0.25f,0.4f,1.0f};
        camera.target={0,-0.65f,0};
        camera.distance=captureCameraDistance>0?captureCameraDistance:eyeDetailCapture?.85f:closeupCapture?1.05f:portraitCapture?2.1f:6.4f;
        if(portraitCapture)camera.target={0,0.96f,0};
        if(eyeDetailCapture)camera.target={0,.96f,0};
        camera.minDistance=eyeDetailCapture?.2f:closeupCapture?.5f:2.0f;
        camera.pitch=0.08f;
        camera.yaw=0.08f;
        if(previewWalk){camera.target.z=1.2f;camera.distance=captureCameraDistance>0?captureCameraDistance:10.5f;camera.yaw=0.65f;}
        cache=std::make_unique<GpuMorphAssetCache>(dev);
        retiredHumans=std::make_unique<HumanRetirementQueue>(dev);
        if(commandLineAsset)loadExternal(manifestFile);
        regenerate();
        MeshBuilder ground;
        if(studioLightingEnabled&&human.head){const auto& m=*human.head;studioHeight=std::max(.01f,m.boundsMax.y-m.boundsMin.y);studioFoot={(m.boundsMin.x+m.boundsMax.x)*.5f,m.boundsMin.y,(m.boundsMin.z+m.boundsMax.z)*.5f};ground=vivid::humanStudioCyclorama(studioFoot,studioHeight);}
        else ground.box({0,-2.78f,0},{12,0.16f,12});
        floorCpuVertices.clear();
        floorCpuVertices.reserve(ground.vertices().size());
        for(const auto& vertex:ground.vertices())
            floorCpuVertices.push_back(vertex.pos);
        floorCpuIndices=ground.indices();
        floor=ground.upload(dev);
        if(!floor.vb.valid()||!floor.ib.valid())
            throw std::runtime_error("VIVID Creator: floor upload failed");

    }
    bool releaseHuman() {
        if(!device)return true;
        // This method is called before any current-frame compute/render
        // encoder. The GPU queue barrier proves all older submitted uses
        // are complete; user edits are rare, never per-frame GPU stalls.
        if(actor && !actor->shutdownAfterIdle()) {
            status=actor->lastError();
            return false;
        }
        if(cpuActor && !cpuActor->shutdownAfterIdle()) {
            status="VIVID CPU actor: GPU idle barrier failed";
            return false;
        }
        actor.reset();
        cpuActor.reset();
        // MotionWeave entities and compiled graph borrow Model nodes/clips.
        // Following animation source-epoch/lifetime integration, detach all
        // presentation owners BEFORE retiring the old model's CPU/GPU data.
        // The preceding actor idle barrier has already proven no in-flight
        // morph/renderer buffers need the old model any more.
        world.reset();
        graph.reset();
        facePose.clear();
        rigPoseScratch.clear();
        if(human.hair)releaseModel(*device,*human.hair);
        if(human.head)releaseModel(*device,*human.head);
        sequenceAnimator.setModel(nullptr);
        human={};
        scalpBindPositions.clear();
        scalpAnimatedPositions.clear();
        scalpSkinScratch.clear();
        scalpPoseNode=-1;
        scalpSkinning.reset();
        cache->pruneExpired();
        return true;
    }
    static std::string fileContents(const std::filesystem::path& file,
                                    std::size_t maxSize) {
        std::ifstream in(file,std::ios::binary);
        if(!in)throw std::runtime_error("VIVID Creator: unable to open "+file.string());
        std::string bytes((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        if(bytes.size()>maxSize)
            throw std::runtime_error("VIVID Creator: file budget exceeded");
        return bytes;
    }
    static HumanImportManifest readManifest(const std::string& path) {
        return HumanImportManifest::fromJson(Json::parse(
            fileContents(path,1024*1024)));
    }
    static render::FileReader readerFor(const std::string& path) {
        const auto directory=std::filesystem::absolute(
            std::filesystem::path(path)).parent_path();
        return [directory](const std::string& relative,std::string& bytes) {
            if(relative.empty() || relative[0]=='/' ||
               relative.find("..")!=std::string::npos ||
               relative.find('\\')!=std::string::npos ||
               relative.find(':')!=std::string::npos)return false;
            try {
                bytes=fileContents(directory/relative,128ull*1024ull*1024ull);
                return true;
            }catch(...){return false;}
        };
    }
    static std::vector<AuthoredFacialLod> readIndexedLods(
            const HumanImportManifest& manifest,const render::Model& model,
            const std::string& path,
            std::span<const MorphVec3> originalPositions,
            const SparseMorphLibrary& shapes) {
        std::vector<AuthoredFacialLod> layers;
        layers.reserve(manifest.facialLods.size());
        std::vector<float3> sourcePositions;
        sourcePositions.reserve(originalPositions.size());
        for(const auto& p:originalPositions)
            sourcePositions.push_back({p[0],p[1],p[2]});
        for(const auto& entry:manifest.facialLods) {
            const auto sourceId=std::find_if(model.meshes.begin(),model.meshes.end(),
                [&](const MeshDef& mesh){return mesh.name==entry.sourceMeshName;});
            const auto targetId=std::find_if(model.meshes.begin(),model.meshes.end(),
                [&](const MeshDef& mesh){return mesh.name==entry.meshName;});
            if(sourceId==model.meshes.end()||targetId==model.meshes.end())
                throw std::runtime_error("VIVID LOD: indexed mesh not found");
            const int targetMesh=int(targetId-model.meshes.begin());
            auto collect=[&](const MeshDef& mesh) {
                std::vector<std::uint32_t> ids;
                for(const auto& primitive:mesh.primitives) {
                    const std::size_t first=primitive.firstIndex;
                    if(first>model.indices.size() ||
                       primitive.indexCount>model.indices.size()-first)
                        throw std::runtime_error("VIVID LOD: invalid indexed primitive");
                    for(std::size_t i=first;i<first+primitive.indexCount;++i)
                        ids.push_back(model.indices[i]);
                }
                return ids;
            };
            auto indices=collect(*sourceId);
            for(auto id:indices)if(id>=originalPositions.size())
                throw std::runtime_error("VIVID LOD: source mesh no longer has original vertices");
            auto targets=collect(*targetId);
            std::sort(targets.begin(),targets.end());
            targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
            const auto filename=std::filesystem::absolute(
                std::filesystem::path(path)).parent_path()/entry.mappingPath;
            const auto mapping=MorphLodMapping::fromJson(
                Json::parse(fileContents(filename.string(),64ull*1024ull*1024ull)));
            if(mapping.targetVertexIds!=targets || targets.empty())
                throw std::runtime_error("VIVID LOD: indexed target vertex IDs changed");
            for(auto id:targets)if(id>=model.vertices.size())
                throw std::runtime_error("VIVID LOD: target vertex index outside model");
            (void)mapping.transfer(shapes,sourcePositions,indices);
            layers.push_back({mapping,std::move(indices),targetMesh});
        }
        // Preflight cross-layer overlaps before publishing this package.
        std::vector<SparseMorphLibrary> compact;
        compact.reserve(layers.size());
        for(const auto& layer:layers)
            compact.push_back(layer.mapping.transfer(
                shapes,sourcePositions,layer.sourceIndices));
        std::vector<MorphLodGpuAuthoring::Layer> gpuLayers;
        for(std::size_t i=0;i<compact.size();++i)
            gpuLayers.push_back({&compact[i],layers[i].mapping.targetVertexIds});
        if(!gpuLayers.empty())
            (void)MorphLodGpuAuthoring::combineMany(shapes,gpuLayers);
        return layers;
    }
    void loadExternal(const std::string& path) {
        auto manifest=readManifest(path);
        auto imported=importHumanAsset(manifest,readerFor(path));
        // Validate the complete package, including every indexed LOD,
        // before touching the live Creator session or morph state.
        const auto sourceVertexCount=imported.sourceVertexCount;
        std::vector<MorphVec3> originalPositions;
        originalPositions.reserve(sourceVertexCount);
        for(std::size_t i=0;i<sourceVertexCount;++i) {
            const auto& p=imported.model->vertices[i].pos;
            originalPositions.push_back({p.x,p.y,p.z});
        }
        auto pendingMorph=std::make_unique<MorphAuthoring>(
            MorphAuthoring::fromCompiled(imported.facialShapes));
        const auto importedLods=readIndexedLods(
            manifest,*imported.model,path,originalPositions,
            imported.facialShapes);
        std::string error;
        if(!session.loadJson(imported.dna.toJson(),&error))
            throw std::runtime_error("VIVID Creator: imported DNA invalid: "+error);
        syncDraft();
        morphSourceVertices=sourceVertexCount;
        morphSourcePositions=std::move(originalPositions);
        morphDraft=std::move(pendingMorph);
        morphTargetIndex=0;
        morphVertexIndex=0;
        morphEditOffset={};
        authoredFacialLods=importedLods;
        lodGpuMorphEnabled=!authoredFacialLods.empty();
        sculptMeshIndex=-1;
        sculptPickArmed=false;
        morphDirty=false;
        importedManifest=path;
        importedPackage=std::move(manifest);
        std::snprintf(scalpProxyName.data(),scalpProxyName.size(),"%s",
                      importedPackage.scalpColliderMesh.c_str());
        externalMode=true;
        pendingRebuild=true;
        status="Imported open human package: "+path;
    }
    static render::mat4 rigidHeadMatrix(render::mat4 m) {
        for(int col=0;col<3;++col) {
            float lengthSq=0;
            for(int row=0;row<3;++row)
                lengthSq+=m.at(row,col)*m.at(row,col);
            const float inv=1.0f/std::sqrt(std::max(lengthSq,0.00001f));
            for(int row=0;row<3;++row)m.at(row,col)*=inv;
        }
        return m;
    }
    struct ScalpPoseBinding {
        std::vector<render::float3> bindPositions;
        int node=-1;
        std::unique_ptr<ScalpSkinning> skinning;
        render::mat4 nodeBindInverse{};
        render::mat4 headBindRigid{};
    };
    static ScalpPoseBinding installImportedScalpCollider(
        HumanGroomRuntime& groom,const render::Model& model,
        const std::string& meshName,const HumanRigBinding& rig) {
        if(meshName.empty())return {};
        const auto it=std::find_if(model.meshes.begin(),model.meshes.end(),
            [&](const render::MeshDef& mesh){return mesh.name==meshName;});
        if(it==model.meshes.end())
            throw std::runtime_error("VIVID Creator: scalp collider mesh not found");
        const auto meshId=int(it-model.meshes.begin());
        int poseNode=-1;
        for(std::size_t i=0;i<model.nodes.size();++i) {
            const auto& node=model.nodes[i];
            if(node.mesh!=meshId)continue;
            if(poseNode>=0)
                throw std::runtime_error("VIVID Creator: proxy must belong to one model node");
            poseNode=int(i);
        }
        if(poseNode<0 || !rig.valid() ||
           std::size_t(poseNode)>=model.bindWorld.size() ||
           std::size_t(rig.head)>=model.bindWorld.size())
            throw std::runtime_error("VIVID Creator: missing scalp/head bind-pose node");
        // The authoring contract is a closed, bind-pose proxy whose vertices
        // are already expressed in the groom's local coordinate system.
        std::vector<render::float3> positions;
        std::vector<std::uint32_t> indices;
        std::vector<std::uint32_t> sourceIds;
        const bool skinned=model.nodes[std::size_t(poseNode)].skin>=0;
        std::vector<std::uint32_t> remap(model.vertices.size(),
            std::numeric_limits<std::uint32_t>::max());
        for(const auto& primitive:it->primitives) {
            if(skinned!=!primitive.jointPalette.empty())
                throw std::runtime_error("VIVID Creator: scalp proxy skin/palette mismatch");
            const auto begin=std::size_t(primitive.firstIndex);
            const auto end=begin+std::size_t(primitive.indexCount);
            if(begin>model.indices.size() || end>model.indices.size())
                throw std::runtime_error("VIVID Creator: scalp proxy index range invalid");
            for(std::size_t i=begin;i<end;++i) {
                const auto original=model.indices[i];
                if(original>=model.vertices.size())
                    throw std::runtime_error("VIVID Creator: scalp proxy vertex out of bounds");
                auto& mapped=remap[original];
                if(mapped==std::numeric_limits<std::uint32_t>::max()) {
                    mapped=std::uint32_t(positions.size());
                    positions.push_back(model.vertices[original].pos);
                    sourceIds.push_back(original);
                }
                indices.push_back(mapped);
            }
        }
        ScalpPoseBinding binding;
        binding.node=poseNode;
        binding.nodeBindInverse=model.bindWorld[std::size_t(poseNode)].inverse();
        binding.headBindRigid=rigidHeadMatrix(model.bindWorld[std::size_t(rig.head)]);
        if(skinned) {
            binding.skinning=std::make_unique<ScalpSkinning>(
                ScalpSkinning::compile(model,std::size_t(meshId),
                                      std::size_t(poseNode),sourceIds));
            // Skinning produces model-world points. Convert to the same
            // head-local frame used to draw the procedural hair cards.
            if(!binding.skinning->evaluate(model.bindWorld,binding.headBindRigid,
                                           positions))
                throw std::runtime_error("VIVID Creator: invalid skinned scalp bind pose");
        }
        if(!groom.setCollisionMesh(positions,indices))
            throw std::runtime_error("VIVID Creator: scalp proxy must be a closed nondegenerate mesh within the 16K/32K budget");
        if(!groom.attachRootsToCollisionMesh())
            throw std::runtime_error("VIVID Creator: could not attach groom roots to scalp proxy");
        binding.bindPositions=std::move(positions);
        return binding;
    }
    void regenerate() {
        if(!device)return;
        // Admission occurs before any new CPU mesh or GPU asset is built.
        // An unresolved completion marker retains the old character; it
        // never authorizes additional unbounded residency.
        if(retiredHumans && (human.head || human.hair || actor || cpuActor)) {
            const auto retiredBytes=HumanRetirementQueue::estimatedCurrentBytes(
                human,actor.get(),cpuActor.get());
            if(!retiredHumans->canPublish(retiredBytes)) {
                const auto usage=retiredHumans->residency();
                status="VIVID Creator: character rebuild deferred; "+
                       std::to_string(usage.pending)+" pending old characters / "+
                       std::to_string(usage.estimatedBytes/1024/1024)+
                       " MiB estimated retired GPU residency";
                return; // retain pendingRebuild until fences complete
            }
        }
        try {
            // Build/validate in CPU scratch first, keeping last-good visual
            // data if DNA or shader-graph authoring fails.
            HumanPreviewAsset proposed;
            ScalpPoseBinding proposedScalp;
            if(externalMode) {
                const auto& manifest=importedPackage;
                auto imported=importHumanAsset(manifest,readerFor(importedManifest));
                appearance=appearanceOverride?appearanceOverride:imported.appearance;
                proposed.head=std::move(imported.model);
                proposed.rig=imported.rig;
                proposed.facialShapes=morphDraft
                    ? morphDraft->compile() : std::move(imported.facialShapes);
                if(lodGpuMorphEnabled && !authoredFacialLods.empty()) {
                    std::vector<float3> sourcePositions;
                    sourcePositions.reserve(morphSourcePositions.size());
                    for(const auto& p:morphSourcePositions)
                        sourcePositions.push_back({p[0],p[1],p[2]});
                    std::vector<SparseMorphLibrary> compact;
                    compact.reserve(authoredFacialLods.size());
                    for(const auto& lod:authoredFacialLods)
                        compact.push_back(lod.mapping.transfer(
                            proposed.facialShapes,sourcePositions,lod.sourceIndices));
                    std::vector<MorphLodGpuAuthoring::Layer> layers;
                    layers.reserve(compact.size());
                    for(std::size_t i=0;i<compact.size();++i)
                        layers.push_back({&compact[i],
                            authoredFacialLods[i].mapping.targetVertexIds});
                    proposed.facialShapes=MorphLodGpuAuthoring::combineMany(
                        proposed.facialShapes,layers);
                }
                proposed.facialBinding=FaceShapeBinding::compile(
                    proposed.facialShapes,manifest.routes);
                for(const auto& [name,surface]:manifest.materials) {
                    const auto index=proposed.head->findMaterial(name);
                    if(index>=0)proposed.head->materials[std::size_t(index)]=
                        humanMaterialFromSource(
                            surface,session.dna(),
                            proposed.head->materials[std::size_t(index)],
                            presentation.materialQuality);
                }
                if(appearance)applyHumanAppearance(*proposed.head,*appearance);
                const bool hasAuthoredHair=manifest.authoredHair || std::any_of(
                    manifest.materials.begin(),manifest.materials.end(),
                    [](const auto& pair) {
                        return pair.second==HumanSurface::Hair ||
                               pair.second==HumanSurface::Eyelash;
                    });
                if(!hasAuthoredHair) {
                    proposed.groom.generate(session.dna(),presentation.groomQuality);
                    proposedScalp=installImportedScalpCollider(
                        proposed.groom,*proposed.head,
                        manifest.scalpColliderMesh,proposed.rig);
                    proposed.hair=proposed.groom.makeCardModel(session.dna(),presentation.materialQuality);
                }
                // Artist-authored hair meshes are already in proposed.head,
                // with their original strand/UV/alpha maps preserved by
                // humanMaterialFromSource. Never overlay a second wig.
            } else {
                proposed=generateHumanPreview(session.dna(),presentation);
            }
            SkinSeamSplit::Result seamSplit;
            if(!skinWeightEdits.edits().empty()) {
                // Split incompatible primitive skin palettes only on the
                // private candidate, preserving original vertex IDs and
                // expanding the complete extended facial morph CSR.
                // Authored LOD sidecars refer to original source/target IDs;
                // these remain unchanged, while clones append after them.
                seamSplit=SkinSeamSplit::apply(
                    *proposed.head,proposed.facialShapes);
                const auto authored=skinWeightEdits.withSplitClones(seamSplit.clones);
                (void)authored.apply(*proposed.head);
            }
            // Transfer edited base-skin weights to every explicitly
            // authored LOD. Keep the same indexed correspondence used by
            // facial morph baking; no silent transfer to generated LODs.
            if(!skinWeightEdits.edits().empty() &&
               !authoredFacialLods.empty()) {
                for(const auto& lod:authoredFacialLods) {
                    if(lod.targetMesh<0 ||
                       std::size_t(lod.targetMesh)>=proposed.head->meshes.size())
                        throw std::runtime_error("VIVID skin LOD: target mesh missing");
                    int source=-1;
                    for(const auto& indexed:importedPackage.facialLods) {
                        if(indexed.meshName!=proposed.head->meshes[
                            std::size_t(lod.targetMesh)].name)continue;
                        const auto found=std::find_if(
                            proposed.head->meshes.begin(),proposed.head->meshes.end(),
                            [&](const MeshDef& mesh){return mesh.name==indexed.sourceMeshName;});
                        if(found!=proposed.head->meshes.end())
                            source=int(found-proposed.head->meshes.begin());
                    }
                    if(source<0)
                        throw std::runtime_error("VIVID skin LOD: indexed source mesh missing");
                    const auto candidateMapping=SkinLodSeamRemap::expandTarget(
                        lod.mapping,seamSplit.clones,std::size_t(lod.targetMesh));
                    (void)SkinWeightLodTransfer::apply(
                        *proposed.head,std::size_t(source),
                        std::size_t(lod.targetMesh),candidateMapping,lod.sourceIndices);
                }
            }
            // Authorable source prefix plus optional seam clones. Original
            // IDs remain stable after authored LOD expansion. The mapping is
            // published together with the candidate model/CSR, never before.
            // A huge model may still render: picking is disabled above the
            // per-click CPU reconstruction budget instead of failing import.
            std::vector<std::uint32_t> proposedSculptSourceIds;
            if(externalMode && morphSourceVertices &&
               proposed.head->vertices.size()<=1000000u) {
                if(proposed.facialShapes.vertices()!=
                   proposed.head->vertices.size() ||
                   !buildSculptSourceVertexMap(
                       proposed.head->vertices.size(),morphSourceVertices,
                       seamSplit.clones,proposedSculptSourceIds))
                    throw std::runtime_error(
                        "VIVID sculpt: source/LOD/seam model indices inconsistent");
            }
            if(authorBindPose && !rigCorrections.edits().empty()) {
                // Stage all bind / inverse-bind changes on this candidate
                // before model GPU upload and AnimationGraph compilation.
                // Import source and previously visible model stay immutable.
                (void)BindPoseRebind::apply(*proposed.head,rigCorrections);
            }
            // Default preview uses the same body/face idle as Studio; no separate
            // head-only bob clip that makes a full character appear static.
            AnimClip idle=humanPreviewClip(*proposed.head,previewPose,true);
            idle.name="vivid_idle";
            proposed.head->clips.push_back(std::move(idle));
            if(sequenceFrames)sequenceBase=appendHumanPreviewClips(*proposed.head);
            int proposedWalkClip=-1;
            if(previewWalk){
                WalkCycleSettings walk;
                walk.duration=1.0f;
                walk.rootSpeed=120.0f; // source MetaHuman joints are centimetres under a 0.01 scene scale
                walk.rootBob=0.018f; // fraction of leg length, independent of source scale
                proposedWalkClip=int(proposed.head->clips.size());
                HumanWalkPreviewStats walkStats;
                BvhMotion sourceMotion;
                const BvhMotion* motion=nullptr;
                if(!previewBvhPath.empty()) {
                    std::ifstream source(previewBvhPath);
                    if(!source)throw std::runtime_error("Could not open BVH walk source");
                    sourceMotion=parseBvhMotion(std::string(std::istreambuf_iterator<char>(source),{}));
                    motion=&sourceMotion;
                }
                proposed.head->clips.push_back(humanWalkPreviewClip(*proposed.head,walk,&walkStats,motion));
                std::printf("VIVID walk rigs=%d arms relaxed=%d A=%d T=%d unknown=%d headChannels=%d\n",
                    walkStats.completeRigs,walkStats.relaxedArms,walkStats.aPoseArms,
                    walkStats.tPoseArms,walkStats.unknownArms,walkStats.headChannels);
            }
            proposed.head->uploadPrepared=true;
            auto newGraph=AnimationGraph::compile(*proposed.head,Json::parse(
                R"({"nodes":[{"id":"idle","kind":"clip","clip":"vivid_idle"}],"output":"idle"})"));
            // Phase 1: allocate and validate the replacement while the
            // old character remains drawable. The unique_ptr<Model> address
            // remains stable when HumanPreviewAsset ownership is moved.
            std::unique_ptr<GpuActorStream> nextActor;
            std::unique_ptr<CpuActorStream> nextCpuActor;
            std::unique_ptr<World> nextWorld;
            Entity nextEntity{};
            std::vector<mat4> nextFacePose(proposed.head->nodes.size());
            std::vector<float3> nextScalpAnimated(
                proposedScalp.bindPositions.size());
            std::vector<float3> nextScalpSkinScratch(
                proposedScalp.bindPositions.size());
            try {
                nextWorld=std::make_unique<World>();
                nextEntity=nextWorld->create();
                auto& anim=nextWorld->add<motionweave::AnimationAssetComponent>(
                    nextEntity);
                anim.model=proposed.head.get();
                anim.graph=newGraph;
                nextWorld->add<motionweave::AnimationPlayerComponent>(nextEntity);
                nextWorld->add<motionweave::AnimationPoseComponent>(nextEntity);

                if(!uploadModel(*device,*proposed.head,true))
                    throw std::runtime_error("VIVID Creator: candidate head GPU upload failed");
                if(proposed.hair) {
                    gfx::BufferDesc vb;
                    vb.type=gfx::BufferType::Vertex;
                    vb.usage=gfx::Usage::Dynamic;
                    vb.snapshotUpdates=true;
                    vb.size=proposed.hair->vertices.size()*sizeof(ModelVertex);
                    vb.data=proposed.hair->vertices.data();
                    vb.label="VIVID dynamic groom cards";
                    proposed.hair->vb=device->createBuffer(vb);
                    gfx::BufferDesc ib;
                    ib.type=gfx::BufferType::Index;
                    ib.size=proposed.hair->indices.size()*sizeof(std::uint32_t);
                    ib.data=proposed.hair->indices.data();
                    proposed.hair->ib=device->createBuffer(ib);
                    if(!proposed.hair->vb.valid() || !proposed.hair->ib.valid())
                        throw std::runtime_error("VIVID Creator: candidate groom buffer upload failed");
                }
                if(device->supportsCompute() && !forceCpuMorph) {
                    auto gpu=cache->acquire(
                        proposed.facialShapes,proposed.head->vertices);
                    if(!gpu)
                        throw std::runtime_error(
                            "VIVID Creator: candidate shared morph CSR upload failed");
                    nextActor=std::make_unique<GpuActorStream>();
                    if(!nextActor->initializeShared(
                        *device,*proposed.head,std::move(gpu)))
                        throw std::runtime_error(
                            "VIVID Creator: "+nextActor->lastError());
                }else{
                    nextCpuActor=std::make_unique<CpuActorStream>();
                    if(!nextCpuActor->initialize(
                        *device,*proposed.head,proposed.facialShapes,
                        proposed.head->vertices))
                        throw std::runtime_error(
                            "VIVID Creator: "+nextCpuActor->lastError());
                }
            }catch(...) {
                // Candidate construction must never retire the old scene.
                // Synchronize before releasing any candidate-owned RHI
                // buffers or shared immutable shader inputs.
                if(!device->waitForResourceIdle()) {
                    status="VIVID Creator: candidate GPU idle barrier failed; "
                           "candidate resources retained for device shutdown";
                    pendingRebuild=false;
                    // Do not release potentially in-flight handles.
                    // A failed native barrier makes the candidate unsafe
                    // to destroy; preserve its lifetime until process exit.
                    (void)proposed.head.release();
                    (void)proposed.hair.release();
                    (void)nextActor.release();
                    (void)nextCpuActor.release();
                    throw;
                }
                if(nextActor) (void)nextActor->shutdownAfterIdle();
                if(nextCpuActor) (void)nextCpuActor->shutdownAfterIdle();
                nextWorld.reset();
                newGraph.reset();
                if(proposed.hair)releaseModel(*device,*proposed.hair);
                if(proposed.head)releaseModel(*device,*proposed.head);
                throw;
            }
            // Phase 2: allocate an empty retirement queue slot BEFORE
            // modifying active state. No GPU-wide idle stall on a normal
            // revision change. A completed fence eventually retires the
            // old actor AND its Model/texture resources together.
            HumanRetirementQueue::Resident* retired=nullptr;
            try {
                if(human.head || human.hair || actor || cpuActor || world)
                    retired=retiredHumans->enqueueEmpty();
            }catch(...) {
                // Queue admission failed. Keep the old live character and
                // discard the candidate after the safe idle fallback.
                if(device->waitForResourceIdle()) {
                    if(nextActor)(void)nextActor->shutdownAfterIdle();
                    if(nextCpuActor)(void)nextCpuActor->shutdownAfterIdle();
                    nextWorld.reset();
                    newGraph.reset();
                    if(proposed.hair)releaseModel(*device,*proposed.hair);
                    if(proposed.head)releaseModel(*device,*proposed.head);
                }else {
                    (void)proposed.head.release();
                    (void)proposed.hair.release();
                    (void)nextActor.release();
                    (void)nextCpuActor.release();
                }
                throw;
            }
            sequenceAnimator.setModel(nullptr); // never retain a retiring Model
            if(retired) {
                // All transfers here are pointer/owned-state moves; the
                // retirement queue already owns the slot on allocation.
                retired->world=std::move(world);
                retired->graph=std::move(graph);
                retired->actor=std::move(actor);
                retired->cpuActor=std::move(cpuActor);
                retired->human=std::move(human);
            }
            human=std::move(proposed);
            walkClip=proposedWalkClip;
            sequenceAnimator.setModel(human.head.get());sequenceSampled=-1;
            sculptSourceIds=std::move(proposedSculptSourceIds);
            gpuSculptToken.cancel();
            gpuSculptToRecord={};
            gpuSculptExpectedSourceVertex=-1;
            gpuSculptRendererUsedOverride=false;
            if(++gpuSculptPublication==0)gpuSculptPublication=1;
            sculptPicker.reset(); // published Model owns new mesh topology
            sculptPickMarkerValid=false;
            actor=std::move(nextActor);
            cpuActor=std::move(nextCpuActor);
            world=std::move(nextWorld);
            entity=nextEntity;
            graph=std::move(newGraph);
            facePose=std::move(nextFacePose);
            scalpBindPositions=std::move(proposedScalp.bindPositions);
            scalpAnimatedPositions=std::move(nextScalpAnimated);
            scalpSkinScratch=std::move(nextScalpSkinScratch);
            scalpPoseNode=proposedScalp.node;
            scalpSkinning=std::move(proposedScalp.skinning);
            scalpBindNodeInverse=proposedScalp.nodeBindInverse;
            scalpBindHeadRigid=proposedScalp.headBindRigid;
            displayedRevision=session.revision();
            pendingRebuild=false;
            status="Loaded "+session.dna().displayName+" / "+
                   std::to_string(human.head->vertices.size())+" vertices / "+
                   std::to_string(human.groom.stats().guides)+" hair guides";
        }catch(const std::exception& e){
            status=e.what();
            // Do not publish an invalid partial character or hide the
            // diagnostics. It can be rebuilt after the next valid edit.
            pendingRebuild=false;
        }
    }
    void editCommit() {
        if(!ImGui::IsItemDeactivatedAfterEdit())return;
        std::string error;
        if(session.edit([this](HumanDNA& dna){dna=draft;},&error)) {
            pendingRebuild=true;
        }else {
            status=error;
            syncDraft();
        }
    }
    void slider(const char* label,float* value,float lo,float hi) {
        ImGui::SliderFloat(label,value,lo,hi,"%.3f");
        editCommit();
    }
    void color(const char* label,std::array<float,3>& rgb) {
        ImGui::ColorEdit3(label,rgb.data());
        editCommit();
    }
    void commitPreset(const char* id) {
        if(!ImGui::Button(id))return;
        std::string err;
        if(!session.preset(id,&err))status=err;
        else {syncDraft();pendingRebuild=true;}
    }
    void boneAlias(const char* label,std::array<char,128>& text,
                   std::string HumanRigDNA::*member) {
        ImGui::InputText(label,text.data(),text.size());
        if(!ImGui::IsItemDeactivatedAfterEdit())return;
        draft.rig.*member=text.data();
        std::string error;
        if(session.edit([this](HumanDNA& profile){profile=draft;},&error))
            pendingRebuild=true;
        else {status=error;syncDraft();}
    }
    void guiControls() {
        ImGui::SetNextWindowPos({14,14},ImGuiCond_Once);
        ImGui::SetNextWindowSize({354,750},ImGuiCond_Once);
        ImGui::Begin("VIVID Creator - Human DNA");
        ImGui::Text("Revision %zu %s",session.revision(),
                    session.dirty()?"*unsaved":"saved");
        ImGui::TextWrapped("%s",status.c_str());
        if(retiredHumans) {
            const auto memory=retiredHumans->residency();
            ImGui::Text("GPU retirement: %zu pending / %zu peak",
                memory.pending,memory.highWatermark);
            ImGui::Text("Estimated retired GPU: %llu MiB, deferred rebuilds: %llu",
                static_cast<unsigned long long>(
                    memory.estimatedBytes/(1024ull*1024ull)),
                static_cast<unsigned long long>(memory.blockedPublications));
        }
        if(ImGui::Button("Undo") && session.undo()) {
            syncDraft();pendingRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Redo") && session.redo()) {
            syncDraft();pendingRebuild=true;
        }
        ImGui::Separator();
        commitPreset("neutral");ImGui::SameLine();commitPreset("warm");
        ImGui::SameLine();commitPreset("cool");ImGui::SameLine();
        commitPreset("stylized");
        ImGui::Separator();
        if(ImGui::CollapsingHeader("Face and body proportions",ImGuiTreeNodeFlags_DefaultOpen)) {
            slider("Head scale",&draft.anatomy.headScale,0.5f,1.8f);
            slider("Head width",&draft.anatomy.headWidth,0.5f,1.8f);
            slider("Jaw width",&draft.anatomy.jawWidth,0.5f,1.8f);
            slider("Jaw length",&draft.anatomy.jawLength,0.5f,1.8f);
            slider("Cheek fullness",&draft.anatomy.cheekFullness,0,1);
            slider("Nose length",&draft.anatomy.noseLength,0,1);
            slider("Nose width",&draft.anatomy.noseWidth,0,1);
            slider("Eye spacing",&draft.anatomy.eyeSpacing,0,1);
            slider("Eye scale",&draft.anatomy.eyeScale,0.5f,1.8f);
            slider("Brow height",&draft.anatomy.browHeight,0,1);
            slider("Lip fullness",&draft.anatomy.lipFullness,0,1);
            slider("Mouth width",&draft.anatomy.mouthWidth,0,1);
            slider("Ear scale",&draft.anatomy.earScale,0.5f,1.8f);
            slider("Neck length",&draft.anatomy.neckLength,0,1);
            slider("Shoulder width",&draft.anatomy.shoulderWidth,0,1);
            slider("Body build",&draft.anatomy.bodyBuild,0,1);
            slider("Height metres",&draft.anatomy.heightMeters,0.8f,2.6f);
        }
        if(ImGui::CollapsingHeader("Rig correction authoring")) {
            ImGui::TextWrapped("Default edits are presentation-only. Opt-in rebinding "
                "applies local bone translation to imported pre-upload skeleton and "
                "recomputes inverse-bind matrices. Original asset files remain unchanged.");
            if(ImGui::Checkbox("Rebind skeleton from authored bone translations",
                                &authorBindPose))pendingRebuild=true;
            if(authorBindPose)
                ImGui::TextWrapped("Rebind requires valid skinned joint palettes; "
                    "bone offsets are NOT applied again as a pose overlay.");
            if(human.head && !human.head->nodes.empty()) {
                selectedRigNode=std::clamp(selectedRigNode,0,int(human.head->nodes.size())-1);
                const char* selected=human.head->nodes[std::size_t(selectedRigNode)].name.c_str();
                if(ImGui::BeginCombo("Rig node",selected)) {
                    for(std::size_t i=0;i<human.head->nodes.size();++i)
                        if(ImGui::Selectable(human.head->nodes[i].name.c_str(),
                                            selectedRigNode==int(i))) {
                            selectedRigNode=int(i);
                            const auto found=rigCorrections.edits().find(
                                human.head->nodes[i].name);
                            rigOffset[0]=rigOffset[1]=rigOffset[2]=0;
                            if(found!=rigCorrections.edits().end()) {
                                rigOffset[0]=found->second.x;
                                rigOffset[1]=found->second.y;
                                rigOffset[2]=found->second.z;
                            }
                        }
                    ImGui::EndCombo();
                }
                ImGui::DragFloat3("Local translation",rigOffset,0.002f,-1.0f,1.0f);
                if(ImGui::Button("Apply rig correction")) {
                    if(rigCorrections.setTranslation(selected,
                        {rigOffset[0],rigOffset[1],rigOffset[2]})) {
                        if(authorBindPose)pendingRebuild=true;
                        status="Rig correction applied";
                    }else status="Rig correction rejected";
                }
                ImGui::SameLine();
                if(ImGui::Button("Undo rig") && rigCorrections.undo() &&
                   authorBindPose)pendingRebuild=true;
                ImGui::SameLine();
                if(ImGui::Button("Redo rig") && rigCorrections.redo() &&
                   authorBindPose)pendingRebuild=true;
            }
            ImGui::InputText("Rig correction JSON",rigCorrectionFile,
                             sizeof(rigCorrectionFile));
            if(ImGui::Button("Save rig corrections")) {
                try {
                    const auto data=rigCorrections.toJson().dump(2);
                    const auto temp=std::string(rigCorrectionFile)+".vivid.tmp";
                    {std::ofstream output(temp,std::ios::binary|std::ios::trunc);
                     if(!output)throw std::runtime_error("Unable to stage rig file");
                     output.write(data.data(),std::streamsize(data.size()));
                     output.flush();
                     if(!output)throw std::runtime_error("Rig file write failed");}
                    std::error_code error;
                    std::filesystem::rename(temp,rigCorrectionFile,error);
                    if(error)throw std::runtime_error(error.message());
                    status="Rig corrections saved";
                }catch(const std::exception& e){status=e.what();}
            }
            ImGui::SameLine();
            if(ImGui::Button("Load rig corrections")) {
                try {
                    auto next=RigAuthoring::fromJson(Json::parse(
                        fileContents(rigCorrectionFile,1024*1024)));
                    rigCorrections=std::move(next);
                    if(authorBindPose)pendingRebuild=true;
                    status="Rig corrections loaded";
                }catch(const std::exception& e){status=e.what();}
            }
        }
        if(ImGui::CollapsingHeader("Skin weight authoring")) {
            ImGui::TextWrapped("Edit one model-relative vertex and named bone. "
                "Other influences are normalized automatically to 255 bytes. "
                "Shared vertices with incompatible skin palettes are rejected.");
            if(human.head) {
                const auto& meshes=human.head->meshes;
                const char* current=skinMeshIndex>=0 &&
                    std::size_t(skinMeshIndex)<meshes.size()
                    ?meshes[std::size_t(skinMeshIndex)].name.c_str():"Select skin mesh";
                if(ImGui::BeginCombo("Weight paint mesh",current)) {
                    for(std::size_t i=0;i<meshes.size();++i)
                        if(ImGui::Selectable(meshes[i].name.c_str(),
                            skinMeshIndex==int(i)))skinMeshIndex=int(i);
                    ImGui::EndCombo();
                }
            }
            ImGui::SliderFloat("Brush radius",&skinBrushRadius,0.005f,1.f,"%.3f");
            ImGui::SliderFloat("Brush strength",&skinBrushStrength,0.01f,1.f,"%.2f");
            ImGui::Checkbox("Mirror weight paint X",&skinMirrorX);
            ImGui::Checkbox("Show skin weight heatmap",&skinHeatmap);
            if(ImGui::Button("Paint weight on next click")) {
                skinBrushArmed=true;
                status="VIVID weight paint armed: hold left mouse and drag over skinned mesh";
            }
            ImGui::SameLine();
            if(ImGui::Button("Undo weights")&&skinWeightEdits.undo())
                pendingRebuild=true;
            ImGui::SameLine();
            if(ImGui::Button("Redo weights")&&skinWeightEdits.redo())
                pendingRebuild=true;
            if(human.head && !human.head->vertices.empty() &&
               !human.head->nodes.empty()) {
                skinVertexId=std::clamp(skinVertexId,0,
                    int(human.head->vertices.size()-1));
                skinJointNode=std::clamp(skinJointNode,0,
                    int(human.head->nodes.size()-1));
                ImGui::InputInt("Skin vertex ID",&skinVertexId);
                if(ImGui::BeginCombo("Skin bone",
                    human.head->nodes[std::size_t(skinJointNode)].name.c_str())) {
                    for(std::size_t i=0;i<human.head->nodes.size();++i)
                        if(ImGui::Selectable(human.head->nodes[i].name.c_str(),
                            int(i)==skinJointNode))skinJointNode=int(i);
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Target bone weight",&skinWeightValue,0,1,"%.3f");
                if(ImGui::Button("Set skin weight")) {
                    if(skinVertexId>=0 && skinWeightEdits.set(
                        std::uint32_t(skinVertexId),
                        human.head->nodes[std::size_t(skinJointNode)].name,
                        skinWeightValue)) {
                        pendingRebuild=true;
                        status="Skin weight edit staged for next model rebuild";
                    }else status="Skin weight edit rejected";
                }
                ImGui::SameLine();
                if(ImGui::Button("Clear vertex edit") && skinVertexId>=0 &&
                   skinWeightEdits.erase(std::uint32_t(skinVertexId))) {
                    pendingRebuild=true;
                }
                ImGui::Text("Authored weight edits: %zu",skinWeightEdits.edits().size());
            }
            ImGui::InputText("Skin weights JSON",skinWeightFile,sizeof(skinWeightFile));
            if(ImGui::Button("Save skin weights")) {
                try {
                    const auto data=skinWeightEdits.toJson().dump(2);
                    const auto tmp=std::string(skinWeightFile)+".vivid.tmp";
                    {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
                     if(!out)throw std::runtime_error("VIVID skin: cannot stage file");
                     out.write(data.data(),std::streamsize(data.size()));
                     out.flush();
                     if(!out)throw std::runtime_error("VIVID skin: save failed");}
                    std::error_code ec;
                    std::filesystem::rename(tmp,skinWeightFile,ec);
                    if(ec)throw std::runtime_error(ec.message());
                    status="Skin weight sidecar saved";
                }catch(const std::exception& e){status=e.what();}
            }
            ImGui::SameLine();
            if(ImGui::Button("Load skin weights")) {
                try {
                    auto next=SkinWeightAuthoring::fromJson(
                        Json::parse(fileContents(skinWeightFile,4ull*1024ull*1024ull)));
                    skinWeightEdits=std::move(next);
                    pendingRebuild=true;
                    status="Skin weight sidecar loaded";
                }catch(const std::exception& e){status=e.what();}
            }
        }
        if(ImGui::CollapsingHeader("Rig / imported bone names")) {
            boneAlias("Root bone",rootBone,&HumanRigDNA::rootBone);
            boneAlias("Head bone",headBone,&HumanRigDNA::headBone);
            boneAlias("Jaw bone",jawBone,&HumanRigDNA::jawBone);
            boneAlias("Left eye bone",leftEyeBone,&HumanRigDNA::leftEyeBone);
            boneAlias("Right eye bone",rightEyeBone,&HumanRigDNA::rightEyeBone);
            ImGui::TextWrapped("Retargets imported glTF/FBX bone names. "
                               "Morph-only jaw and eyes can omit joints.");
        }
        if(ImGui::CollapsingHeader("Skin / skin scattering",ImGuiTreeNodeFlags_DefaultOpen)) {
            color("Pigment",draft.skin.melaninColor);
            slider("Melanin",&draft.skin.melanin,0,1);
            slider("Hemoglobin",&draft.skin.hemoglobin,0,1);
            slider("Scattering mm",&draft.skin.scatteringMm,0.01f,6.0f);
            slider("Pore frequency",&draft.skin.poreScale,0,2);
            slider("Wrinkles",&draft.skin.wrinkleStrength,0,1);
            slider("Roughness",&draft.skin.roughness,0.02f,1);
            slider("Oiliness",&draft.skin.oiliness,0,1);
            slider("Wetness",&draft.skin.wetness,0,1);
        }
        if(ImGui::CollapsingHeader("Eyes / iris / cornea")) {
            color("Iris color",draft.eyes.irisColor);
            slider("Iris radius",&draft.eyes.irisRadius,0.15f,0.75f);
            slider("Pupil",&draft.eyes.pupil,0.05f,0.95f);
            slider("Cornea IOR",&draft.eyes.corneaIor,1.25f,1.45f);
            slider("Limbus",&draft.eyes.limbus,0,1);
            slider("Sclera redness",&draft.eyes.scleraRedness,0,1);
            slider("Tear film",&draft.eyes.tearFilm,0,1);
        }
        if(ImGui::CollapsingHeader("Facial morph / 52 semantic channels")) {
            ImGui::TextWrapped("Author default facial channel weights. "
                               "Live jaw, blink and smile controls override "
                               "their corresponding default channels.");
            for(std::size_t i=0;i<kChannelCount;++i) {
                const auto name=channelName(static_cast<Channel>(i));
                const std::string label(name);
                ImGui::PushID(int(i));
                ImGui::SliderFloat(label.c_str(),
                                   &draft.expressionRest[i],0.0f,1.0f,"%.2f");
                editCommit();
                ImGui::PopID();
            }
            if(ImGui::Button("Reset facial defaults")) {
                std::string error;
                if(session.edit([](HumanDNA& dna) {
                    dna.expressionRest.fill(0.0f);
                },&error)) {
                    syncDraft();
                    pendingRebuild=true;
                } else status=error;
            }
        }
        if(ImGui::CollapsingHeader("Hair / groom")) {
            ImGui::Checkbox("Hair self-collision",&groomSelfCollision);
            ImGui::Checkbox("Continuous strand segment collision",
                            &groomSegmentCollision);
            ImGui::SliderFloat("Strand separation",&groomSelfSeparation,
                               0.002f,0.045f,"%.4f");
            ImGui::SliderFloat("Strand friction",&groomFriction,0,1,"%.2f");
            ImGui::SliderFloat("Curl shape preservation",&groomCurlPreservation,
                               0,1,"%.2f");
            ImGui::SliderFloat("Strand collision response",&groomSelfResponse,
                               0.05f,1.0f,"%.2f");
            if(human.groom.stats().guides) {
                const auto contacts=human.groom.selfCollisionStats();
                ImGui::Text("Strand pairs %zu / contacts %zu%s",
                    contacts.pairChecks,contacts.contacts,
                    contacts.budgetHit?" / budget reached":"");
            }
            color("Hair pigment",draft.groom.color);
            slider("Length m",&draft.groom.lengthMeters,0,2);
            slider("Density",&draft.groom.density,0,1);
            slider("Curl",&draft.groom.curl,0,1);
            slider("Clumping",&draft.groom.clump,0,1);
            slider("Stiffness",&draft.groom.stiffness,0,1);
            slider("Damping",&draft.groom.damping,0,1);
            slider("Gravity",&draft.groom.gravity,0,3);
            slider("Wind response",&draft.groom.wind,0,3);
            ImGui::Text("Guides %u  Segments %u",draft.groom.guideCount,
                        draft.groom.segments);
            ImGui::SliderInt("Hair guides",&draftGuides,4,8192);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.guideCount=std::uint32_t(draftGuides);
                std::string error;
                if(session.edit([this](HumanDNA& d){d=draft;},&error))
                    pendingRebuild=true;
                else {status=error;syncDraft();}
            }
            ImGui::SliderInt("Hair segments",&draftSegments,2,32);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.segments=std::uint32_t(draftSegments);
                std::string error;
                if(session.edit([this](HumanDNA& d){d=draft;},&error))
                    pendingRebuild=true;
                else {status=error;syncDraft();}
            }
            ImGui::Checkbox("Simulate hair",&inspectGroom);
            ImGui::SliderFloat("Wind",&wind,0,3);
            if(ImGui::Button("Hair High")) {presentation.groomQuality=GroomQuality::High;pendingRebuild=true;}
            ImGui::SameLine();
            if(ImGui::Button("Med")) {presentation.groomQuality=GroomQuality::Medium;pendingRebuild=true;}
            ImGui::SameLine();
            if(ImGui::Button("Low")) {presentation.groomQuality=GroomQuality::Low;pendingRebuild=true;}
        }
        ImGui::Separator();
        ImGui::Text("Material quality (Skin / Eye / Hair)");
        if(ImGui::Button("Material Low")) {
            presentation.materialQuality=HumanMaterialQuality::Low;pendingRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Material Medium")) {
            presentation.materialQuality=HumanMaterialQuality::Medium;pendingRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Material High")) {
            presentation.materialQuality=HumanMaterialQuality::High;pendingRebuild=true;
        }
        ImGui::Text("Active tier: %s",
            presentation.materialQuality==HumanMaterialQuality::Low?"Low":
            presentation.materialQuality==HumanMaterialQuality::Medium?"Medium":"High");
        ImGui::InputText("Presentation config",presentationFile,
                         sizeof(presentationFile));
        if(ImGui::Button("Save presentation config")) {
            if(presentation.saveFile(presentationFile,&status))
                status="Saved visual profile "+std::string(presentationFile);
        }
        ImGui::SameLine();
        if(ImGui::Button("Load presentation config")) {
            try {
                auto updated=VividPresentationSettings::loadFile(presentationFile);
                presentation=updated;
                pendingRebuild=true;
                status="Loaded visual profile "+std::string(presentationFile);
            }catch(const std::exception& e){status=e.what();}
        }
        ImGui::Separator();
        ImGui::Checkbox("Animate MotionWeave",&animate);
        ImGui::SliderFloat("Jaw open",&jaw,0,1);
        ImGui::SliderFloat("Smile",&smile,0,1);
        ImGui::SliderFloat("Blink",&blink,0,1);
        ImGui::SliderFloat("Camera yaw",&camera.yaw,-3.14f,3.14f);
        ImGui::SliderFloat("Camera pitch",&camera.pitch,-0.4f,0.95f);
        ImGui::SliderFloat("Camera distance",&camera.distance,2.5f,12.0f);
        ImGui::Separator();
        ImGui::InputText("Recipe",recipeFile,sizeof(recipeFile));
        if(ImGui::Button("Save DNA JSON")) {
            if(session.saveFile(recipeFile,&status))
                status="Saved "+std::string(recipeFile);
        }
        ImGui::SameLine();
        if(ImGui::Button("Load DNA JSON")) {
            if(session.loadFile(recipeFile,&status)) {
                syncDraft();pendingRebuild=true;
                status="Loaded "+std::string(recipeFile);
            }
        }
        ImGui::Separator();
        ImGui::InputText("Package manifest",manifestFile,sizeof(manifestFile));
        if(ImGui::Button("Import glTF/FBX/OBJ + DNA + morphs")) {
            try {loadExternal(manifestFile);}
            catch(const std::exception& e){status=e.what();}
        }
        ImGui::SameLine();
        if(ImGui::Button("Use procedural reference")) {
            externalMode=false;
            importedManifest.clear();
            pendingRebuild=true;
        }
        if(externalMode && ImGui::CollapsingHeader("Imported facial morph bindings")) {
            ImGui::TextWrapped("Optional closed, unskinned proxy in groom-local "
                               "coordinates. Leave empty for DNA ellipsoid.");
            ImGui::InputText("Scalp collision mesh",
                             scalpProxyName.data(),scalpProxyName.size());
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                auto proposed=importedPackage;
                proposed.scalpColliderMesh=scalpProxyName.data();
                try {
                    proposed.validate();
                    importedPackage=std::move(proposed);
                    pendingRebuild=true;
                    status="Scalp proxy selection updated";
                } catch(const std::exception& e) {
                    status=e.what();
                    std::snprintf(scalpProxyName.data(),scalpProxyName.size(),
                                  "%s",importedPackage.scalpColliderMesh.c_str());
                }
            }
            if(human.groom.hasCollisionMesh())
                ImGui::Text("Scalp: %s MotionWeave deformation",
                            scalpSkinning?"multi-joint CPU skinning":"rigid-node");
            else
                ImGui::TextUnformatted("Scalp: DNA ellipsoid fallback");
            ImGui::TextWrapped("Adjust gain for each imported ARKit channel -> shape route. "
                               "The source target names and vertex data remain unchanged.");
            for(std::size_t i=0;i<importedPackage.routes.size();++i) {
                auto& route=importedPackage.routes[i];
                ImGui::PushID(int(i));
                ImGui::Text("%s -> %s",
                    std::string(channelName(route.channel)).c_str(),
                    route.targetName.c_str());
                ImGui::SliderFloat("Gain",&route.gain,0.0f,4.0f,"%.2f");
                if(ImGui::IsItemDeactivatedAfterEdit())pendingRebuild=true;
                ImGui::PopID();
            }
            if(morphDraft && ImGui::TreeNode("Sparse morph vertex sculpt")) {
                ImGui::InputText("New morph name",newMorphName,sizeof(newMorphName));
                if(ImGui::Button("Add named target")) {
                    if(morphDraft->addTarget(newMorphName)) {
                        morphTargetIndex=int(morphDraft->targets())-1;
                        morphDirty=true;pendingRebuild=true;
                        status="VIVID named corrective added";
                    } else status="VIVID target creation rejected";
                }
                if(morphDraft->targets()>0) {
                ImGui::TextWrapped("Edit one source vertex position delta in "
                    "the chosen target. Source head geometry stays unchanged.");
                if(human.head && !human.head->meshes.empty()) {
                    const char* current=(sculptMeshIndex>=0 &&
                        std::size_t(sculptMeshIndex)<human.head->meshes.size())
                        ? human.head->meshes[std::size_t(sculptMeshIndex)].name.c_str()
                        : "Select face mesh";
                    if(ImGui::BeginCombo("3D pick mesh",current)) {
                        for(std::size_t mesh=0;mesh<human.head->meshes.size();++mesh) {
                            if(ImGui::Selectable(human.head->meshes[mesh].name.c_str(),
                                sculptMeshIndex==int(mesh)) &&
                               sculptMeshIndex!=int(mesh)) {
                                sculptMeshIndex=int(mesh);
                                sculptPicker.reset();
                                sculptPickMarkerValid=false;
                                gpuSculptToken.cancel();
                                gpuSculptToRecord={};
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if(ImGui::Button(sculptPickArmed
                        ?"Cancel viewport picking":"Pick vertex in 3D viewport"))
                        sculptPickArmed=!sculptPickArmed;
                    if(ImGui::Checkbox("Experimental GPU ID check",
                                       &gpuSculptEnabled)) {
                        gpuSculptToken.cancel();
                        gpuSculptToRecord={};
                    }
                    ImGui::TextWrapped(
                        "Experimental ID/depth check: Metal, Vulkan, native "
                        "GLES3/GL33 when supported. GPU renders a CPU-fed "
                        "morph/skin snapshot, not the Renderer3D vertexOverride; "
                        "it never silently replaces the CPU BVH selection.");
                    ImGui::TextWrapped("Select the source face mesh, click Pick, "
                        "then click its visible triangles outside inspector.");
                }
                const int maxTarget=int(morphDraft->targets())-1;
                ImGui::SliderInt("Morph target",&morphTargetIndex,0,maxTarget);
                morphTargetIndex=std::clamp(morphTargetIndex,0,maxTarget);
                const auto& target=morphDraft->shapes()[std::size_t(morphTargetIndex)];
                ImGui::Text("Target: %s",target.name.c_str());
                ImGui::SliderInt("Route ARKit channel",&routeChannelIndex,0,
                                 int(kChannelCount)-1);
                ImGui::Text("Channel: %s",
                    std::string(channelName(static_cast<Channel>(routeChannelIndex))).c_str());
                if(ImGui::Button("Bind channel to target")) {
                    auto proposed=importedPackage;
                    const auto channel=static_cast<Channel>(routeChannelIndex);
                    const auto match=std::find_if(proposed.routes.begin(),proposed.routes.end(),
                        [&](const ShapeRoute& route) {
                            return route.channel==channel && route.targetName==target.name;
                        });
                    if(match==proposed.routes.end()) {
                        proposed.routes.push_back({channel,target.name,1.0f});
                        try {
                            proposed.validate();
                            importedPackage=std::move(proposed);
                            pendingRebuild=true;
                            status="VIVID facial route added; save manifest to publish";
                        }catch(const std::exception& e){status=e.what();}
                    }
                }
                bool removedThisFrame=false;
                if(ImGui::Button("Delete selected target")) {
                    const std::string removedName=target.name;
                    if(morphDraft->removeTarget(std::size_t(morphTargetIndex))) {
                        importedPackage.routes.erase(
                            std::remove_if(importedPackage.routes.begin(),
                                           importedPackage.routes.end(),
                                [&](const ShapeRoute& route) {
                                    return route.targetName==removedName;
                                }),
                            importedPackage.routes.end());
                        removedThisFrame=true;
                        morphTargetIndex=0;
                        morphDirty=true;pendingRebuild=true;
                        status="VIVID corrective target deleted";
                    }
                }
                if(!removedThisFrame) {
                const int maxVertex=int(morphSourceVertices)-1;
                ImGui::SliderInt("Source vertex",&morphVertexIndex,0,
                                 std::max(0,maxVertex));
                const auto index=std::uint32_t(morphVertexIndex);
                const auto existing=std::find_if(target.deltas.begin(),target.deltas.end(),
                    [&](const MorphDelta& d){return d.vertex==index;});
                if(ImGui::Button("Read vertex delta")) {
                    morphEditOffset=existing==target.deltas.end()
                        ? float3{} : float3{existing->position[0],
                                          existing->position[1],
                                          existing->position[2]};
                }
                ImGui::DragFloat3("Position delta",&morphEditOffset.x,0.001f,-1.0f,1.0f);
                if(ImGui::Button("Apply vertex delta")) {
                    if(morphDraft->setPosition(
                        std::size_t(morphTargetIndex),index,
                        {morphEditOffset.x,morphEditOffset.y,morphEditOffset.z})) {
                        morphDirty=true;
                        pendingRebuild=true;
                        status="VIVID sparse morph vertex edited";
                    }else status="VIVID sparse morph edit rejected";
                }
                ImGui::SliderFloat("Brush radius",&morphBrushRadius,
                                   0.001f,2.0f,"%.3f");
                ImGui::Checkbox("Mirror brush across X=0",&sculptMirrorX);
                if(ImGui::Button("Apply falloff brush")) {
                    if(morphDraft->brush(std::size_t(morphTargetIndex),
                        morphSourcePositions,index,morphBrushRadius,
                        {morphEditOffset.x,morphEditOffset.y,morphEditOffset.z},
                        sculptMirrorX)) {
                        morphDirty=true;
                        pendingRebuild=true;
                        status="VIVID radius-falloff brush stroke applied";
                    }else status="VIVID brush stroke rejected";
                }
                ImGui::SameLine();
                if(ImGui::Button("Undo sculpt") && morphDraft->undo()) {
                    morphDirty=true;pendingRebuild=true;
                }
                ImGui::SameLine();
                if(ImGui::Button("Redo sculpt") && morphDraft->redo()) {
                    morphDirty=true;pendingRebuild=true;
                }
                } // valid selected target for this frame
                ImGui::TextUnformatted(morphDirty?"Morph changes not exported":"Morph source unchanged");
                } // nonempty morph targets
                if(ImGui::Button("Export morph JSON")) {
                    try {
                        if(importedManifest.empty() || morphSourceVertices==0)
                            throw std::runtime_error("VIVID morph export: no imported source");
                        // Keep author-specified source intact; new packages use
                        // an owned sidecar in the same manifest directory.
                        const auto basename=importedPackage.morphPath.empty()
                            ? std::string("vivid-authored-morphs.json")
                            : importedPackage.morphPath;
                        const auto destination=std::filesystem::absolute(
                            std::filesystem::path(importedManifest)).parent_path()/basename;
                        const auto data=morphDraft->toJson(morphSourceVertices).dump(2);
                        const auto staging=std::filesystem::path(
                            destination.string()+".vivid.tmp");
                        {
                            std::ofstream out(staging,std::ios::binary|std::ios::trunc);
                            if(!out)throw std::runtime_error("Morph staging open failed");
                            out.write(data.data(),std::streamsize(data.size()));
                            out.flush();
                            if(!out)throw std::runtime_error("Morph staging write failed");
                        }
                        std::error_code ec;
                        std::filesystem::rename(staging,destination,ec);
                        if(ec)throw std::runtime_error(
                            "Morph atomic replacement failed: "+ec.message());
                        importedPackage.morphPath=basename;
                        morphDirty=false;
                        status="VIVID morph sidecar saved; save manifest to publish path";
                    }catch(const std::exception& e){status=e.what();}
                }
                ImGui::TreePop();
            }
            if(human.head && morphDraft &&
               ImGui::TreeNode("Facial LOD surface bake")) {
                ImGui::TextWrapped("Bake morphs for an authored lower-detail mesh. "
                    "Source and LOD meshes must share model-local coordinates. "
                    "This does not change the original model or switch rendered LOD.");
                auto meshCombo=[&](const char* label,int& selection) {
                    const auto& meshes=human.head->meshes;
                    const char* current=selection>=0&&std::size_t(selection)<meshes.size()
                        ? meshes[std::size_t(selection)].name.c_str() : "Choose mesh";
                    if(ImGui::BeginCombo(label,current)) {
                        for(std::size_t i=0;i<meshes.size();++i)
                            if(ImGui::Selectable(meshes[i].name.c_str(),
                                                selection==int(i)))selection=int(i);
                        ImGui::EndCombo();
                    }
                };
                meshCombo("Base face mesh",sourceLodMesh);
                meshCombo("Target face LOD mesh",targetLodMesh);
                ImGui::SliderFloat("Projection tolerance",&lodProjectionTolerance,
                                   0.001f,0.2f,"%.3f");
                if(!authoredFacialLods.empty()) {
                    if(ImGui::Checkbox("Publish LOD morphs to GPU actor",
                                       &lodGpuMorphEnabled))
                        pendingRebuild=true;
                    ImGui::Text("Prepared facial LODs: %zu / 8",
                                authoredFacialLods.size());
                    for(std::size_t level=0;level<authoredFacialLods.size();++level) {
                        const auto& lod=authoredFacialLods[level];
                        ImGui::BulletText("Mesh %d: %zu vertices",
                            lod.targetMesh,lod.mapping.samples.size());
                        ImGui::SameLine();
                        ImGui::PushID(int(level));
                        if(ImGui::SmallButton("Remove level")) {
                            try {
                                auto proposed=importedPackage;
                                if(lod.targetMesh<0 ||
                                   std::size_t(lod.targetMesh)>=human.head->meshes.size())
                                    throw std::runtime_error("VIVID LOD: selected mesh missing");
                                const auto name=human.head->meshes[std::size_t(lod.targetMesh)].name;
                                proposed.facialLods.erase(std::remove_if(
                                    proposed.facialLods.begin(),proposed.facialLods.end(),
                                    [&](const HumanLodEntry& entry){return entry.meshName==name;}),
                                    proposed.facialLods.end());
                                proposed.validate();
                                const auto data=proposed.toJson().dump(2);
                                const auto tmp=std::string(importedManifest)+".vivid.tmp";
                                {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
                                 if(!out)throw std::runtime_error("VIVID LOD: manifest staging open failed");
                                 out.write(data.data(),std::streamsize(data.size()));
                                 out.flush();
                                 if(!out)throw std::runtime_error("VIVID LOD: manifest staging write failed");}
                                std::error_code error;
                                std::filesystem::rename(tmp,importedManifest,error);
                                if(error)throw std::runtime_error(error.message());
                                importedPackage=std::move(proposed);
                                authoredFacialLods.erase(
                                    authoredFacialLods.begin()+std::ptrdiff_t(level));
                                if(authoredFacialLods.empty())lodGpuMorphEnabled=false;
                                pendingRebuild=true;
                                status="VIVID LOD: removed level from package index";
                            }catch(const std::exception& e){status=e.what();}
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TextWrapped("Actor deforms both source and LOD "
                        "vertices in one full-model GPU stream; Renderer3D "
                        "chooses the authored mesh LOD.");
                }
                if(ImGui::Button("Load baked LOD mapping")) {
                    try {
                        if(sourceLodMesh<0 || targetLodMesh<0 ||
                           sourceLodMesh==targetLodMesh || !human.head ||
                           std::size_t(sourceLodMesh)>=human.head->meshes.size() ||
                           std::size_t(targetLodMesh)>=human.head->meshes.size())
                            throw std::runtime_error("VIVID LOD: select source and target meshes first");
                        const auto targetName=human.head->meshes[std::size_t(targetLodMesh)].name;
                        const auto registered=std::find_if(
                            importedPackage.facialLods.begin(),
                            importedPackage.facialLods.end(),
                            [&](const HumanLodEntry& e){return e.meshName==targetName;});
                        if(registered==importedPackage.facialLods.end())
                            throw std::runtime_error("VIVID LOD: no indexed mapping for target mesh");
                        const auto path=std::filesystem::absolute(
                            std::filesystem::path(importedManifest)).parent_path()/
                            registered->mappingPath;
                        const auto restored=MorphLodMapping::fromJson(
                            Json::parse(fileContents(path,64ull*1024ull*1024ull)));
                        std::vector<std::uint32_t> sourceIndices;
                        const auto& model=*human.head;
                        for(const auto& primitive:model.meshes[std::size_t(sourceLodMesh)].primitives) {
                            const std::size_t first=primitive.firstIndex;
                            if(first>model.indices.size() ||
                               primitive.indexCount>model.indices.size()-first)
                                throw std::runtime_error("VIVID LOD: invalid source index range");
                            for(std::size_t j=first;j<first+primitive.indexCount;++j)
                                sourceIndices.push_back(model.indices[j]);
                        }
                        std::vector<float3> positions;
                        positions.reserve(morphSourcePositions.size());
                        for(const auto& p:morphSourcePositions)
                            positions.push_back({p[0],p[1],p[2]});
                        if(restored.targetVertexIds.empty())
                            throw std::runtime_error("VIVID LOD: mapping lacks model vertex IDs");
                        const auto sourceMorph=morphDraft->compile();
                        (void)restored.transfer(sourceMorph,positions,sourceIndices);
                        // Verify every target ID belongs to the authored LOD.
                        std::vector<std::uint32_t> validIds;
                        for(const auto& primitive:model.meshes[std::size_t(targetLodMesh)].primitives) {
                            const std::size_t first=primitive.firstIndex;
                            if(first>model.indices.size() ||
                               primitive.indexCount>model.indices.size()-first)
                                throw std::runtime_error("VIVID LOD: invalid target index range");
                            for(std::size_t j=first;j<first+primitive.indexCount;++j)
                                validIds.push_back(model.indices[j]);
                        }
                        std::sort(validIds.begin(),validIds.end());
                        validIds.erase(std::unique(validIds.begin(),validIds.end()),validIds.end());
                        if(validIds!=restored.targetVertexIds)
                            throw std::runtime_error("VIVID LOD: target model vertex IDs changed");
                        if(authoredFacialLods.size()>=8 &&
                           std::none_of(authoredFacialLods.begin(),authoredFacialLods.end(),
                             [&](const AuthoredFacialLod& l){return l.targetMesh==targetLodMesh;}))
                            throw std::runtime_error("VIVID LOD: maximum eight authored levels");
                        AuthoredFacialLod layer{restored,std::move(sourceIndices),targetLodMesh};
                        auto existing=std::find_if(authoredFacialLods.begin(),authoredFacialLods.end(),
                            [&](const AuthoredFacialLod& l){return l.targetMesh==targetLodMesh;});
                        if(existing==authoredFacialLods.end())
                            authoredFacialLods.push_back(std::move(layer));
                        else *existing=std::move(layer);
                        lodGpuMorphEnabled=true;
                        pendingRebuild=true;
                        status="VIVID LOD: validated mapping loaded for GPU actor";
                    }catch(const std::exception& e){status=e.what();}
                }
                if(ImGui::Button("Bake and export LOD morphs")) {
                    try {
                        if(!externalMode || importedManifest.empty() ||
                           sourceLodMesh<0 || targetLodMesh<0 ||
                           sourceLodMesh==targetLodMesh ||
                           std::size_t(sourceLodMesh)>=human.head->meshes.size() ||
                           std::size_t(targetLodMesh)>=human.head->meshes.size())
                            throw std::runtime_error("VIVID LOD: select distinct source and target meshes");
                        const auto& model=*human.head;
                        std::vector<std::uint32_t> faceIndices;
                        const auto& sourceMesh=model.meshes[std::size_t(sourceLodMesh)];
                        for(const auto& primitive:sourceMesh.primitives) {
                            const auto begin=std::size_t(primitive.firstIndex);
                            const auto count=std::size_t(primitive.indexCount);
                            if(begin>model.indices.size()||count>model.indices.size()-begin)
                                throw std::runtime_error("VIVID LOD: bad source triangle indices");
                            for(std::size_t j=begin;j<begin+count;++j) {
                                const auto id=model.indices[j];
                                if(id>=morphSourceVertices)
                                    throw std::runtime_error("VIVID LOD: generated source vertex is not authorable");
                                faceIndices.push_back(id);
                            }
                        }
                        const auto& targetMesh=model.meshes[std::size_t(targetLodMesh)];
                        std::vector<std::uint32_t> targetIds;
                        for(const auto& primitive:targetMesh.primitives) {
                            const auto begin=std::size_t(primitive.firstIndex);
                            const auto count=std::size_t(primitive.indexCount);
                            if(begin>model.indices.size()||count>model.indices.size()-begin)
                                throw std::runtime_error("VIVID LOD: bad target triangle indices");
                            for(std::size_t j=begin;j<begin+count;++j) {
                                const auto id=model.indices[j];
                                if(id>=model.vertices.size())
                                    throw std::runtime_error("VIVID LOD: target vertex outside mesh");
                                targetIds.push_back(id);
                            }
                        }
                        std::sort(targetIds.begin(),targetIds.end());
                        targetIds.erase(std::unique(targetIds.begin(),targetIds.end()),targetIds.end());
                        if(targetIds.empty())throw std::runtime_error("VIVID LOD: target mesh empty");
                        std::vector<float3> lodPositions;
                        lodPositions.reserve(targetIds.size());
                        for(auto id:targetIds)lodPositions.push_back(model.vertices[id].pos);
                        std::vector<float3> originalPositions;
                        originalPositions.reserve(morphSourcePositions.size());
                        for(const auto& p:morphSourcePositions)
                            originalPositions.push_back({p[0],p[1],p[2]});
                        MorphLodMapping map;
                        map.sourceVertices=std::uint32_t(originalPositions.size());
                        map.sourceTriangles=std::uint32_t(faceIndices.size()/3);
                        map.targetVertexIds=targetIds;
                        map.sourceFingerprint=MorphLodMapping::fingerprint(
                            originalPositions,faceIndices);
                        map.samples=MorphLodSurface::projectAccelerated(
                            originalPositions,faceIndices,lodPositions,
                            lodProjectionTolerance);
                        const auto lodMorph=map.transfer(morphDraft->compile(),
                                                         originalPositions,faceIndices);
                        const auto lodSidecar=MorphAuthoring::fromCompiled(lodMorph)
                            .toJson(lodPositions.size()).dump(2);
                        const auto directory=std::filesystem::absolute(
                            std::filesystem::path(importedManifest)).parent_path();
                        if(morphDirty)
                            throw std::runtime_error("VIVID LOD: export edited source morphs before publishing package");
                        if(authoredFacialLods.size()>=8 &&
                           std::none_of(authoredFacialLods.begin(),authoredFacialLods.end(),
                             [&](const AuthoredFacialLod& l){return l.targetMesh==targetLodMesh;}))
                            throw std::runtime_error("VIVID LOD: maximum eight authored levels");
                        const auto mappingJson=map.toJson().dump(2);
                        // Content-addressed immutable files. A failure before
                        // the final manifest rename leaves the old package
                        // valid; unreferenced sidecars can be cleaned later.
                        std::uint64_t digest=14695981039346656037ull;
                        for(unsigned char c:mappingJson) {
                            digest^=c;digest*=1099511628211ull;
                        }
                        for(unsigned char c:lodSidecar) {
                            digest^=c;digest*=1099511628211ull;
                        }
                        const char hex[]="0123456789abcdef";
                        std::string suffix(16,'0');
                        for(int i=15;i>=0;--i) {
                            suffix[std::size_t(i)]=hex[digest&15u];digest>>=4;
                        }
                        const auto name="vivid-facial-lod-"+
                            std::to_string(targetLodMesh)+"-"+suffix;
                        const auto mappingName=name+".mapping.json";
                        const auto mappingFile=directory/mappingName;
                        const auto morphFile=directory/(name+".morphs.json");
                        auto publishImmutable=[&](const std::filesystem::path& file,
                                                   const std::string& contents) {
                            if(std::filesystem::exists(file)) {
                                if(fileContents(file.string(),64ull*1024ull*1024ull)!=contents)
                                    throw std::runtime_error("VIVID LOD: immutable filename collision");
                                return;
                            }
                            const auto tmp=std::filesystem::path(file.string()+".vivid.tmp");
                            {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
                             if(!out)throw std::runtime_error("VIVID LOD: cannot stage sidecar");
                             out.write(contents.data(),std::streamsize(contents.size()));
                             out.flush();
                             if(!out)throw std::runtime_error("VIVID LOD: sidecar write failed");}
                            std::error_code ec;
                            std::filesystem::rename(tmp,file,ec);
                            if(ec)throw std::runtime_error("VIVID LOD: sidecar rename failed: "+ec.message());
                        };
                        auto proposedPackage=importedPackage;
                        const auto sourceName=sourceMesh.name;
                        const auto targetName=targetMesh.name;
                        const auto found=std::find_if(
                            proposedPackage.facialLods.begin(),
                            proposedPackage.facialLods.end(),
                            [&](const HumanLodEntry& entry){return entry.meshName==targetName;});
                        const HumanLodEntry entry{targetName,sourceName,mappingName};
                        if(found==proposedPackage.facialLods.end())
                            proposedPackage.facialLods.push_back(entry);
                        else *found=entry;
                        proposedPackage.validate();
                        publishImmutable(mappingFile,mappingJson);
                        publishImmutable(morphFile,lodSidecar);
                        const auto packageText=proposedPackage.toJson().dump(2);
                        const auto packageTmp=std::filesystem::path(importedManifest+".vivid.tmp");
                        {std::ofstream out(packageTmp,std::ios::binary|std::ios::trunc);
                         if(!out)throw std::runtime_error("VIVID LOD: cannot stage manifest");
                         out.write(packageText.data(),std::streamsize(packageText.size()));
                         out.flush();
                         if(!out)throw std::runtime_error("VIVID LOD: manifest staging failed");}
                        std::error_code manifestEc;
                        std::filesystem::rename(packageTmp,importedManifest,manifestEc);
                        if(manifestEc)throw std::runtime_error(
                            "VIVID LOD: manifest publish failed: "+manifestEc.message());
                        importedPackage=std::move(proposedPackage);
                        // Publish the full source/target CSR only at the next
                        // authoring rebuild, not by swapping a GPU stream
                        // during an active render pass.
                        AuthoredFacialLod layer{std::move(map),std::move(faceIndices),targetLodMesh};
                        auto existing=std::find_if(authoredFacialLods.begin(),authoredFacialLods.end(),
                            [&](const AuthoredFacialLod& l){return l.targetMesh==targetLodMesh;});
                        if(existing==authoredFacialLods.end())
                            authoredFacialLods.push_back(std::move(layer));
                        else *existing=std::move(layer);
                        lodGpuMorphEnabled=true;
                        pendingRebuild=true;
                        status="VIVID LOD: baked "+
                            std::to_string(lodPositions.size())+
                            " projected vertices; mapping and morph sidecars saved";
                    }catch(const std::exception& e){status=e.what();}
                }
                ImGui::TreePop();
            }
            if(ImGui::Button("Save imported manifest")) {
                try {
                    importedPackage.validate();
                    const auto data=importedPackage.toJson().dump(2);
                    const std::filesystem::path destination(importedManifest);
                    const auto staging=std::filesystem::path(importedManifest+".vivid.tmp");
                    {
                        std::ofstream out(staging,std::ios::binary|std::ios::trunc);
                        if(!out)throw std::runtime_error("Manifest staging open failed");
                        out.write(data.data(),std::streamsize(data.size()));
                        out.flush();
                        if(!out)throw std::runtime_error("Manifest staging write failed");
                    }
                    std::error_code ec;
                    std::filesystem::rename(staging,destination,ec);
                    if(ec)throw std::runtime_error("Manifest atomic replace failed: "+ec.message());
                    status="Saved imported human manifest: "+importedManifest;
                }catch(const std::exception& e){status=e.what();}
            }
        }
        ImGui::TextWrapped("%s",externalMode ?
            "Imported head: edit material DNA, morph expressions and groom. "
            "Mesh proportions remain authored in the original glTF/FBX asset." :
            "Procedural head: all geometric proportions are editable. "
            "For realistic scanned characters import a glTF/FBX package.");

        ImGui::End();
    }
    void frame(gfx::Device& dev,double dt) {
        if(antialiasing.pendingTargetRetirements())antialiasing.collectRetiredTargets();
        // Animated characters may move after a ray sample; do not display
        // a stale sampled world-space marker across animation frames.
        if(animate)sculptPickMarkerValid=false;
        ui.newFrame(dt);
        SculptGpuIdPass::Completed gpuReply;
        if(gpuSculptPass.takeCompleted(gpuReply)) {
            SculptGpuPickPixel pixel{};
            if(gpuReply.rgba.size()==4) {
                for(std::size_t i=0;i<4;++i)
                    pixel.idRgba[i]=gpuReply.rgba[i];
            }
            std::uint32_t selectedTriangle=0;
            // An obsolete asynchronous reply must never change a newer
            // CPU source selection or even overwrite its status message.
            const bool replyForActive=gpuSculptToken.pendingIs(gpuReply.request);
            const bool valid=gpuSculptToken.consume(
                gpuReply.request,pixel,gpuSculptPublication,
                dev.frameSerial(),std::uint32_t(dev.framebufferWidth()),
                std::uint32_t(dev.framebufferHeight()),selectedTriangle);
            if(valid && human.head &&
               gpuReply.request.meshId<human.head->meshes.size()) {
                const auto verified=sculptPicker.resolveGpuTriangle(
                    *human.head,gpuReply.request.meshId,selectedTriangle,
                    gpuSculptRayOrigin,gpuSculptRayDirection,
                    morphSourceVertices,sculptSourceIds,
                    gpuReply.request.frameSerial,
                    gpuReply.request.modelRevision);
                if(verified.hit &&
                   int(verified.vertex)==gpuSculptExpectedSourceVertex) {
                    // Do NOT replace the CPU vertex: this experimental GPU
                    // pass draws a CPU-expanded morph/skin snapshot, not the
                    // actual Renderer3D GPU vertex-override stream/LOD.
                    status=gpuReply.actualOverride
                        ?"GPU ID confirmed actual actor 60B GPU source / vertex "
                        :"GPU ID confirmed CPU-derived renderer source / vertex ";
                    status+=std::to_string(gpuSculptExpectedSourceVertex);
                    if(gpuReply.actualOverride)
                        status+=" (GPU skin and final colour pixel parity unverified)";
                }else status="GPU/CPU ID mismatch; CPU BVH selection retained";
            }else if(replyForActive && !valid && gpuReply.rgba.empty()) {
                gpuSculptToken.cancel();
                status="GPU ID readback failed; CPU BVH retained";
            }else if(replyForActive && !valid &&
                     gpuReply.rgba.size()==4 &&
                     !gpuSculptToken.pending()) {
                // Zero means background, unselectable LOD/alpha material,
                // or another scene mesh covering the selected triangle.
                // Preserve the CPU selection, but do not claim GPU success.
                status="GPU scene blocker/zero ID; CPU BVH retained";
            }else if(replyForActive && !valid) {
                gpuSculptToken.cancel();
                status="GPU ID frame/viewport/pose changed; CPU BVH retained";
            }
        }
        // Offline sequences record a fixed asset, not an interactive edit session.
        // Keep the normal Creator UI unchanged outside diagnostic recording.
        if(!sequenceFrames)guiControls();
        // A material tier is also a Renderer3D pipeline setting. Apply at
        // the owner-thread frame boundary; setMaterialQuality retires old
        // programs, while regenerate publishes a new character candidate.
        if(rendererReady && renderer.materialQuality()!=
           rendererMaterialQuality(presentation.materialQuality))
            applyVividMaterialQuality(renderer,presentation);
        if(pendingRebuild&&sequenceFrames&&sequenceSampled>=0)
            throw std::runtime_error("Character changed during offline recording; restart capture");
        if(pendingRebuild)regenerate();
        if(!human.head||(!actor&&!cpuActor)||!world||
           (actor&&!actor->ready())||
           (cpuActor&&!cpuActor->ready())) {
            gfx::PassDesc empty;
            dev.beginPass(empty);ui.render();dev.endPass();
            return;
        }
        const float step=std::clamp(float(dt),0.0f,0.04f);
        motionweave::MotionWeave::update(*world,animate&&!sequenceFrames?step:0.0f);
        const auto& sourcePose=world->get<motionweave::AnimationPoseComponent>(entity);
        const auto* poseSource=&sourcePose;
        if(previewMotion&&previewTime>=0&&!sequenceFrames){previewCapturePose=sourcePose;bindPose(*human.head,previewCapturePose.local);sampleClip(human.head->clips.back(),previewTime,previewCapturePose.local);worldMatrices(*human.head,previewCapturePose.local,previewCapturePose.matrices);poseSource=&previewCapturePose;}
        if(sequenceFrames){
            if(sequenceSampled!=sequenceFrame){
                if(sequenceSampled<0&&!previewWalk)sequenceAnimator.setModel(human.head.get());
                // 3s relaxed, A, T, portrait, then relaxed. Face close-up stays
                // portrait to show uninterrupted blink/gaze and loop continuity.
                previewCapturePose=sourcePose;
                if(previewWalk){
                    const float time=float(sequenceFrame)/24.f;
                    const auto& clip=human.head->clips[size_t(walkClip)];
                    const float cycle=std::floor(time/clip.duration);
                    bindPose(*human.head,previewCapturePose.local);
                    sampleClip(clip,time-cycle*clip.duration,previewCapturePose.local);
                    for(const auto& channel:clip.channels){
                        if(channel.path!=AnimChannel::Translation||channel.times.size()<2)continue;
                        const auto name=humanBoneSemantic(human.head->nodes[size_t(channel.node)].name);
                        if(name!="root"&&name.rfind("root.",0)!=0)continue;
                        const size_t end=channel.values.size()-3;
                        previewCapturePose.local[size_t(channel.node)].t.x+=
                            (channel.values[end]-channel.values[0])*cycle;
                        previewCapturePose.local[size_t(channel.node)].t.z+=
                            (channel.values[end+2]-channel.values[2])*cycle;
                    }
                }else{
                    const int poses[]={0,1,2,3,0};
                    const int index=previewFixedPose?previewPose:(portraitCapture?3:poses[std::min(sequenceFrame/72,4)]);
                    const int clip=sequenceBase+index*2+1;
                    if(sequenceAnimator.clip(0)!=clip)playHumanPreview(sequenceAnimator,clip,true);
                    if(sequenceSampled>=0)sequenceAnimator.update(1.f/24);
                    sequenceAnimator.evaluate(previewCapturePose.local);
                }
                if(previewBindPose)bindPose(*human.head,previewCapturePose.local);
                worldMatrices(*human.head,previewCapturePose.local,previewCapturePose.matrices);
                if(previewHeadSweep){
                    const int head=human.head->findNode(session.dna().rig.headBone);
                    if(head<0)throw std::runtime_error("Head sweep requires mapped head bone");
                    const int parentIndex=human.head->nodes[size_t(head)].parent;
                    const auto parent=parentIndex<0?quat{}:rotationOf(previewCapturePose.matrices[size_t(parentIndex)]);
                    const quat inverse{-parent.x,-parent.y,-parent.z,parent.w};
                    const float phase=float(sequenceFrame)*(6.28318530718f/(24.f*6.f));
                    auto& local=previewCapturePose.local[size_t(head)];
                    local.r=qnormalize(inverse*quat::axisAngle({0,1,0},.45f*std::sin(phase))*quat::axisAngle({1,0,0},.20f*std::sin(phase*2))*parent*local.r);
                    worldMatrices(*human.head,previewCapturePose.local,previewCapturePose.matrices);
                }
                for(size_t node=0;node<human.head->nodes.size();++node){const auto name=humanBoneSemantic(human.head->nodes[node].name);if(name=="foot_l"||name=="foot_r"||name=="ball_l"||name=="ball_r"){
                    previewFootDrift=std::max(previewFootDrift,length(previewCapturePose.matrices[node].transformPoint({})-human.head->bindWorld[node].transformPoint({})));++previewFootSamples;
                }}
                sequenceSampled=sequenceFrame;
            }
            poseSource=&previewCapturePose;
        }
        const auto& pose=*poseSource;
        if(!pose.valid || pose.matrices.size()!=human.head->nodes.size()) {
            status="MotionWeave pose temporarily unavailable";
            return;
        }
        // The DNA 52-channel neutral pose is applied first. Interactive
        // demonstration controls below override their named channels.
        for(std::size_t i=0;i<kChannelCount;++i)
            controller.set(Layer::Tracking,static_cast<Channel>(i),
                           session.dna().expressionRest[i]);
        if(previewMotion){std::array<float,kChannelCount> values{};for(size_t n=0;n<human.head->nodes.size();++n){const int mesh=human.head->nodes[n].mesh;if(mesh<0)continue;const auto& names=human.head->meshes[size_t(mesh)].morphTargetNames;for(size_t k=0;k<std::min(names.size(),size_t(kMaxMorphTargets));++k){Channel channel;if(channelFromName(names[k],channel))values[size_t(channel)]=std::max(values[size_t(channel)],pose.local[n].w[k]);}}for(size_t i=0;i<kChannelCount;++i)controller.set(Layer::Tracking,Channel(i),values[i]);}
        if(!previewMotion||jaw>0)controller.set(Layer::Tracking,Channel::JawOpen,jaw);
        if(!previewMotion||blink>0)controller.set(Layer::Tracking,Channel::EyeBlinkLeft,blink);
        if(!previewMotion||blink>0)controller.set(Layer::Tracking,Channel::EyeBlinkRight,blink);
        if(!previewMotion||smile>0)controller.set(Layer::Tracking,Channel::MouthSmileLeft,smile);
        if(!previewMotion||smile>0)controller.set(Layer::Tracking,Channel::MouthSmileRight,smile);
        if(smoke && !animate)controller.snap();
        else controller.advance(step);
        if(!human.rig.compose(pose.matrices,facePose,controller.frame())) {
            status="VIVID facial rig: invalid composition";
            return;
        }
        if(!authorBindPose && !rigCorrections.edits().empty()) {
            rigPoseScratch.resize(facePose.size());
            if(!rigCorrections.apply(*human.head,facePose,rigPoseScratch))
                status="VIVID rig correction invalid: last-good pose retained";
            else facePose.swap(rigPoseScratch);
        }
        if(appearance) {
            try {applyHumanBody(*human.head,*appearance,facePose);}
            catch(const std::exception& e){status=e.what();return;}
        }
        if(actor) {
            if(!actor->dispatch(controller.frame(),human.facialBinding))
                status=actor->lastError();
        }else if(cpuActor) {
            if(!cpuActor->dispatch(controller.frame(),human.facialBinding))
                status=cpuActor->lastError();
        }
        // Reconstruct skin-deformed surface for the currently selected
        // mesh, using MotionWeave node-world matrices and inverse binds.
        // The CPU reference does not include the GPU facial morph offset.
        SkinnedWeightSurface skinSurface;
        const bool skinSurfaceReady=externalMode && human.head &&
            skinMeshIndex>=0 &&
            std::size_t(skinMeshIndex)<human.head->meshes.size() &&
            (skinBrushArmed || skinHeatmap) &&
            prepareSkinnedWeightSurface(*human.head,
                std::size_t(skinMeshIndex),facePose,skinSurface);

        if(skinBrushArmed && human.head &&
           skinJointNode>=0 &&
           std::size_t(skinJointNode)<human.head->nodes.size()) {
            const auto& io=ImGui::GetIO();
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
               !io.WantCaptureMouse && skinSurfaceReady &&
               !skinWeightEdits.strokeActive()) {
                (void)skinWeightEdits.beginStroke();
                skinStrokeLastVertex=-1;
            }
            if(skinWeightEdits.strokeActive() &&
               ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               !io.WantCaptureMouse && skinSurfaceReady) {
                const auto mouse=ImGui::GetMousePos();
                float3 origin{},direction{};
                if(camera.screenRay(mouse.x,mouse.y,
                                    float(dev.framebufferWidth()),
                                    float(dev.framebufferHeight()),
                                    origin,direction)) {
                    const auto pick=pickSkinnedWeightVertex(
                        *human.head,skinSurface,origin,direction);
                    if(pick.hit && skinStrokeLastVertex!=int(pick.vertex)) {
                        skinVertexId=int(pick.vertex);
                        skinStrokeLastVertex=int(pick.vertex);
                        const auto& bone=human.head->nodes[
                            std::size_t(skinJointNode)].name;
                        if(!skinWeightEdits.strokeBrush(
                            *human.head,std::size_t(skinMeshIndex),
                            pick.vertex,bone,skinWeightValue,skinBrushRadius,
                            skinBrushStrength,skinMirrorX)) {
                            // Cancel entire gesture on invalid palette,
                            // oversized brush or incompatible source.
                            (void)skinWeightEdits.endStroke(false);
                            skinBrushArmed=false;
                            status="Weight paint cancelled: invalid skin/palette/stroke";
                        }
                    }
                }
            }
            if(skinWeightEdits.strokeActive() &&
               !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if(skinWeightEdits.endStroke()) {
                    pendingRebuild=true;
                    status="Skin weight drag committed as one undoable stroke";
                }
                skinBrushArmed=false;
                skinStrokeLastVertex=-1;
            }
        }
        if(skinHeatmap && skinSurfaceReady && human.head &&
           skinJointNode>=0 &&
           std::size_t(skinJointNode)<human.head->nodes.size()) {
            const auto& bone=human.head->nodes[
                std::size_t(skinJointNode)].name;
            std::size_t visited=0;
            std::size_t budget=0;
            for(const auto inside:skinSurface.included)
                if(inside)++budget;
            const std::size_t stride=std::max<std::size_t>(1,(budget+999)/1000);
            for(std::size_t id=0;id<skinSurface.world.size();++id) {
                if(!skinSurface.included[id])continue;
                if(visited++%stride)continue;
                try {
                    const float weight=SkinWeightAuthoring::weightAt(
                        *human.head,std::size_t(skinMeshIndex),
                        std::uint32_t(id),bone);
                    float2 screen{};
                    if(!camera.worldToScreen(skinSurface.world[id],
                        float(dev.framebufferWidth()),
                        float(dev.framebufferHeight()),screen))continue;
                    ImGui::GetForegroundDrawList()->AddCircleFilled(
                        ImVec2(screen.x,screen.y),2.3f,
                        IM_COL32(int(weight*255),40,
                                 int((1.f-weight)*255),190));
                }catch(const std::invalid_argument&) {}
            }
        }
        // Picking uses the actual camera ray and rendered node-world pose,
        // never a guessed nearest projected vertex behind the head.
        if(externalMode && sculptPickArmed && morphDraft && human.head &&
           sculptSourceIds.empty() &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::GetIO().WantCaptureMouse) {
            sculptPickArmed=false;
            sculptPickMarkerValid=false;
            status="VIVID sculpt: source mapping unavailable (oversized or invalid model)";
        }
        if(externalMode && sculptPickArmed && morphDraft && human.head &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::GetIO().WantCaptureMouse &&
           sculptMeshIndex>=0 &&
           std::size_t(sculptMeshIndex)<human.head->meshes.size() &&
           !sculptSourceIds.empty()) {
            const auto mouse=ImGui::GetMousePos();
            const float width=float(dev.framebufferWidth());
            const float height=float(dev.framebufferHeight());
            float3 origin{},direction{};
            if(camera.screenRay(mouse.x,mouse.y,width,height,origin,direction)) {
                // New user click invalidates any still-in-flight GPU response.
                // A rejected/CPU-only click must never be overwritten later
                // by an older completed Metal selection.
                gpuSculptToken.cancel();
                gpuSculptToRecord={};
                const auto beforePick=std::chrono::steady_clock::now();
                bool hardwareCandidate=false;
                if(gpuSculptEnabled && !gpuSculptPass.pending() &&
                   dev.frameSerial()) {
                    // Do not preemptively reject authored LOD assets: the
                    // actual Renderer3D view selected in drawModel() below
                    // is the authority. LOD0 without a fade can match the
                    // source precisely; alternate LOD/fade/batches fail
                    // closed in validateSculptGpuRendererView.
                    hardwareCandidate=gpuSculptPass.supported() ||
                                      gpuSculptPass.initialize(dev);
                }
                const auto pick=sculptPicker.pick(
                    *human.head,std::size_t(sculptMeshIndex),facePose,
                    human.facialShapes,human.facialBinding,
                    controller.frame(),origin,direction,morphSourceVertices,
                    sculptSourceIds,
                    hardwareCandidate?dev.frameSerial():0,
                    hardwareCandidate?gpuSculptPublication:0);
                const auto pickMs=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-beforePick).count();
                if(pick.hit) {
                    morphVertexIndex=int(pick.vertex);
                    sculptPickMarkerWorld=pick.worldPosition;
                    sculptPickMarkerValid=true;
                    sculptPickMarkerVertex=morphVertexIndex;
                    sculptPickMarkerMesh=sculptMeshIndex;
                    sculptPickArmed=false;
                    status="Picked deformed face source vertex "+
                        std::to_string(morphVertexIndex)+
                        " / BVH tris "+
                        std::to_string(sculptPicker.stats().testedTriangles)+
                        "/"+std::to_string(sculptPicker.stats().triangles)+
                        " / CPU "+std::to_string(pickMs)+" ms";
                    if(gpuSculptEnabled && !hardwareCandidate)
                        status+=" / GPU ID backend/frame unavailable; CPU BVH retained";
                    if(hardwareCandidate) {
                        const auto pixelX=int(mouse.x),pixelY=int(mouse.y);
                        const auto req=gpuSculptToken.begin(
                            gpuSculptPublication,dev.frameSerial(),
                            std::size_t(sculptMeshIndex),
                            std::uint32_t(dev.framebufferWidth()),
                            std::uint32_t(dev.framebufferHeight()),
                            pixelX>=0?std::uint32_t(pixelX):UINT32_MAX,
                            pixelY>=0?std::uint32_t(pixelY):UINT32_MAX);
                        if(req.valid()) {
                            gpuSculptToRecord=req;
                            gpuSculptRayOrigin=origin;
                            gpuSculptRayDirection=direction;
                            gpuSculptExpectedSourceVertex=morphVertexIndex;
                            status+=" / GPU ID diagnostic pending";
                        }
                    }
                }else {
                    sculptPickMarkerValid=false;
                    status="No valid deformed source triangle under cursor";
                }
            }
        }
        if(externalMode && morphSourceVertices && human.head &&
           morphVertexIndex>=0 &&
           std::size_t(morphVertexIndex)<morphSourceVertices) {
            // Snapshot of the *picked* morphed+skinned vertex, not an
            // undeformed bind-pose marker. Animated snapshots are displayed
            // only in the click frame; paused previews retain the marker.
            float2 screen{};
            const bool found=sculptPickMarkerValid &&
                sculptPickMarkerVertex==morphVertexIndex &&
                sculptPickMarkerMesh==sculptMeshIndex &&
                camera.worldToScreen(sculptPickMarkerWorld,
                    float(dev.framebufferWidth()),
                    float(dev.framebufferHeight()),screen);
            if(found && std::isfinite(screen.x) && std::isfinite(screen.y)) {
                ImGui::GetForegroundDrawList()->AddCircle(
                    ImVec2(screen.x,screen.y),7.0f,IM_COL32(255,190,50,255),20,2.0f);
            }
        }
        if(inspectGroom && human.hair) {
            if(scalpPoseNode>=0 && !scalpBindPositions.empty() &&
               std::size_t(scalpPoseNode)<facePose.size()) {
                const auto frameHead=rigidHeadMatrix(
                    facePose[std::size_t(human.rig.head)]);
                bool validPose=false;
                if(scalpSkinning) {
                    // MotionWeave owns all node matrices; the VIVID adapter
                    // only performs presentation CPU skinning of scalp proxy.
                    validPose=scalpSkinning->evaluate(
                        facePose,frameHead,scalpAnimatedPositions,
                        scalpSkinScratch);
                } else {
                    const auto delta=frameHead.inverse() *
                        facePose[std::size_t(scalpPoseNode)] *
                        scalpBindNodeInverse * scalpBindHeadRigid;
                    validPose=true;
                    for(float value:delta.m)
                        validPose=validPose&&std::isfinite(value);
                    if(validPose) {
                        for(std::size_t i=0;i<scalpBindPositions.size();++i) {
                            auto p=delta.transformPoint(scalpBindPositions[i]);
                            if(!std::isfinite(p.x)||!std::isfinite(p.y)||
                               !std::isfinite(p.z)) {
                                validPose=false;
                                break;
                            }
                            scalpAnimatedPositions[i]=p;
                        }
                    }
                }
                if(!validPose ||
                   !human.groom.refitCollisionMesh(scalpAnimatedPositions))
                    status="VIVID Creator: invalid scalp pose, last-good BVH retained";
            }
            GroomSelfCollisionConfig strandSettings{};
            strandSettings.enabled=groomSelfCollision;
            strandSettings.separation=groomSelfSeparation;
            strandSettings.response=groomSelfResponse;
            strandSettings.segmentEnabled=groomSegmentCollision;
            strandSettings.friction=groomFriction;
            strandSettings.bendPreservation=groomCurlPreservation;
            if(!human.groom.setSelfCollision(strandSettings))
                status="VIVID groom: invalid strand collision settings";
            if(!human.groom.step(step,{wind,0,wind*0.3f}))
                status="VIVID groom: strand simulation rejected step";
            human.groom.updateCardVertices(human.hair->vertices);
            dev.updateBuffer(human.hair->vb,human.hair->vertices.data(),
                             human.hair->vertices.size()*sizeof(ModelVertex));
        }
        renderer.beginScene(camera,dev.framebufferWidth(),dev.framebufferHeight());
        // Opt-in only for this frame's selected human; other game Models
        // do not pay for per-command provenance collection.
        if((smoke || gpuSculptToRecord.valid()) && human.head)
            renderer.trackModelSubmissions(human.head.get());
        // Renderer3D must select the view's actual model meshes, LODs,
        // vertex overrides and material batches BEFORE the separate ID
        // diagnostic is allowed to corroborate a CPU-picked source vertex.
        // drawModel stages colour commands; render() executes them below.
        if(previewLightSweep&&sequenceFrames)studioLighting.preset=humanStudioPresets()[size_t(std::min(sequenceFrame/48,7))].id;
        if(studioLightingEnabled){vivid::applyHumanStudioLighting(renderer,studioLighting,studioFoot,studioHeight);vivid::updateHumanStudioShadows(renderer,shadowResolution,studioHeight,dev.framebufferHeight());const auto bg=studioLighting.rig().background;renderer.drawMesh(floor,{}, {bg.x,bg.y,bg.z,1},kDrawNoCastShadow,0);}
        else renderer.drawMesh(floor,{}, {0.25f,0.26f,0.3f,1});
        ModelDraw draw;
        draw.model=human.head.get();
        draw.nodeWorld=&facePose;
        // Imported semantic clips use the same native morph texture path as Studio.
        // The sparse actor override suppresses Model morphs, so do not attach it here.
        if(previewMotion)draw.pose=&pose.local;
        const bool headSubmitted=previewMotion || (actor&&actor->attach(draw))||
                                 (cpuActor&&cpuActor->attach(draw));
        if(headSubmitted)
            renderer.drawModel(draw);
        const int headIndex=human.rig.head;
        const mat4& poseHead=facePose[std::size_t(headIndex)];
        // Shared render/collision frame: head-local procedural groom,
        // no extra head nonuniform scale applied to its card geometry.
        const mat4 headRigid=rigidHeadMatrix(poseHead);
        if(human.hair) {
            ModelDraw hair;
            hair.model=human.hair.get();
            hair.world=headRigid;
            renderer.drawModel(hair);
        }
        gfx::PassDesc pass;
        pass.clearColor[0]=0.075f;pass.clearColor[1]=0.085f;pass.clearColor[2]=0.12f;
        if(studioLightingEnabled){const auto bg=studioLighting.rig().background;pass.clearColor[0]=bg.x;pass.clearColor[1]=bg.y;pass.clearColor[2]=bg.z;}
        renderer.render(pass,&antialiasing);
        renderer.resolvePostFx();
        // Stage 7AW: only AFTER the renderer has actually encoded its
        // opaque pass may the ID diagnostic corroborate a picked source.
        // The normal pass is resumed for the UI by the optional block below.
        if(gpuSculptToRecord.valid()) {
            const auto actualView=renderer.modelViewProvenance(*human.head);
            const auto viewCheck=validateSculptGpuRendererView(
                actualView,*human.head,gpuSculptToRecord.meshId,
                gpuSculptToRecord.frameSerial);
            const auto acceptedView=renderer.modelSubmissionProvenance(*human.head);
            const auto submittedCheck=validateSculptGpuSubmittedView(
                actualView,acceptedView,*human.head,
                gpuSculptToRecord.meshId,gpuSculptToRecord.frameSerial);
            // Build zero-ID depth geometry for every other imported model
            // mesh referenced by a visible node, for the actual floor, and
            // for the current procedurally simulated groom. All pieces are
            // from the same current morph/skin pose as the selected surface.
            // Standard masked material primitives with live GPU base
            // textures use the same alpha texture/cutoff as Renderer3D.
            // Unsupported ShaderGraph/Blend/hair cases still block by whole
            // polygon with zero ID; no false selectable IDs are created.
            std::vector<SculptGpuIdVertex> occluders;
            std::vector<SculptGpuMaskBatch> maskedOccluders;
            // The render-source decision, not a second duplicated mesh
            // enumerator, determines which other model meshes can block.
            bool complete=viewCheck.canRenderDiagnostic &&
                          submittedCheck.accepted;
            SculptGpuVertexSource actualStream;
            const SculptGpuVertexSource* actualStreamPtr=nullptr;
            if(complete && viewCheck.gpuVertexOverridePresent) {
                // The renderer's actual current-frame ModelDraw source
                // buffer is the only legal GPU source. Do not upload a
                // second CPU approximation under a GPU-override label.
                const render::Renderer3D::ModelViewPrimitive* selected=nullptr;
                for(const auto& primitive:actualView.primitives) {
                    if(primitive.meshIndex!=gpuSculptToRecord.meshId)
                        continue;
                    if(!selected)selected=&primitive;
                    else {
                        if(primitive.vertexOverride!=selected->vertexOverride ||
                           primitive.vertexOverrideCount!=
                               selected->vertexOverrideCount ||
                           primitive.skinning!=selected->skinning)
                            complete=false;
                        // One GPU skin compute dispatch uses one I.model.
                        // Every selected renderer primitive must have the
                        // identical submitted model transform.
                        for(std::size_t c=0;c<16u;++c)
                            if(primitive.world.m[c]!=selected->world.m[c])
                                complete=false;
                    }
                }
                if(!selected || !gpuSculptPass.supportsActualGpuVertexStream() ||
                   !selected->vertexOverride.valid() ||
                   draw.vertexOverride!=selected->vertexOverride ||
                   !prepareSculptGpuVertexSource(
                       *human.head,gpuSculptToRecord.meshId,facePose,
                       selected->world,selected->vertexOverride,
                       selected->vertexOverrideCount,sculptSourceIds,
                       morphSourceVertices,actualStream))
                    complete=false;
                if(complete && selected->skinning!=
                       (actualStream.params.skinned!=0u))
                    complete=false;
                if(complete && actualStream.maskedSelection &&
                   !gpuSculptPass.supportsActualGpuMaskedStream())
                    complete=false;
                if(complete)actualStreamPtr=&actualStream;
            }
            SkinnedWeightSurface otherSurface;
            for(const auto selectedVisibleMesh:viewCheck.otherVisibleMeshes) {
                if(!complete)break;
                const auto meshId=std::size_t(selectedVisibleMesh);
                if(!prepareSkinnedWeightSurface(
                       *human.head,meshId,facePose,otherSurface,
                       sculptPicker.morphedBindPositions()) ||
                   !appendSculptGpuMaterialGeometry(
                        *human.head,otherSurface,0,{},false,
                        occluders,maskedOccluders)) {
                    complete=false;break;
                }
            }
            if(complete)
                complete=appendSculptGpuWorldOccluder(
                    floorCpuVertices,floorCpuIndices,mat4::identity(),
                    occluders);
            if(complete && human.hair)
                complete=appendSculptGpuWorldOccluder(
                    human.hair->vertices,human.hair->indices,
                    rigidHeadMatrix(facePose[std::size_t(human.rig.head)]),
                    occluders);
            if(complete)dev.endPass(); // Renderer3D leaves the UI pass open
            const bool submitted=complete && gpuSculptPass.record(
                *human.head,sculptPicker.worldSurface(),
                morphSourceVertices,sculptSourceIds,
                renderer.viewProj(),gpuSculptToRecord,
                occluders,maskedOccluders,actualStreamPtr);
            if(complete) {
                // Restore the exact screen pass without wiping either depth
                // or colour. Also undo the ID pass's 1-pixel scissor.
                gfx::PassDesc resume=pass;
                resume.clearColorEnabled=false;
                resume.clearDepthEnabled=false;
                dev.beginPass(resume);
                dev.setViewport({0,0,dev.framebufferWidth(),
                                dev.framebufferHeight()});
                dev.setScissor({0,0,dev.framebufferWidth(),
                               dev.framebufferHeight()});
            }
            gpuSculptRendererUsedOverride=submitted &&
                actualStreamPtr!=nullptr;
            gpuSculptToRecord={};
            if(!submitted) {
                gpuSculptToken.cancel();
                const char* reason=!viewCheck.canRenderDiagnostic?
                    sculptGpuRendererViewReasonText(viewCheck.reason):
                    !submittedCheck.accepted?
                        sculptGpuSubmittedViewReasonText(submittedCheck.reason):
                        "actor GPU source, material or scene blocker unsupported";
                status="GPU scene ID unavailable ("+
                    std::string(reason)+"); CPU BVH retained";
            }
        }

        if(smoke) {
            const auto& p=renderer.profile();
            if(!renderer.modelError().empty())
                throw std::runtime_error("VIVID Creator Metal shader error: "+
                                         renderer.modelError());
            if(!headSubmitted)
                throw std::runtime_error("VIVID Creator head was not submitted: "+status);
            const auto headSubmission=renderer.modelSubmissionProvenance(*human.head);

            const bool ready=(captureShadowQuality==ShadowQuality::Off || p.draws[0]>0) && p.draws[1]>0 &&
                             !headSubmission.commands.empty();
            smokeReady=ready?smokeReady+1:0;
            if(smokeReady>=4 && !smokeCaptureRequested) {
            if(previewLightSweep&&sequenceFrame%48==0)std::printf("VIVID lighting frame=%d preset=%s\n",sequenceFrame,studioLighting.preset.c_str());
            if(sequenceFrame==0){const auto& lod=renderer.modelStats();std::printf("VIVID LOD auto=%d levels=%llu,%llu,%llu,%llu chains=%llu blocked=%llu\n",int(automaticLod),(unsigned long long)lod.lodNodes[0],(unsigned long long)lod.lodNodes[1],(unsigned long long)lod.lodNodes[2],(unsigned long long)lod.lodNodes[3],(unsigned long long)lod.cameraLodChains,(unsigned long long)lod.cameraLodBlocked);}
            if(sequenceFrame==0)std::printf("VIVID AA enabled=%d passes=%d\n",int(aaEnabled),antialiasing.stats().passes);
            if(sequenceFrame==0)std::printf("VIVID shadow map=%d adaptive=%d cameraDistance=%.3f\n",renderer.shadowMapSize(),shadowResolution.resolution,camera.distance);
            if(sequenceFrame==0)for(const auto& mat:human.head->materials)if(mat.eyeOptics.z>0)
                std::printf("VIVID optical material %s shading=%d radius=%.4f depth=%.4f ior=%.3f tear=%.2f\n",mat.name.c_str(),int(mat.shading),mat.eyeOptics.z,mat.eyeOptics.w,mat.humanProfile.x,mat.humanProfile.y);
                smokeCaptureRequested=true;
                dev.captureNextFrame([this,headCommands=headSubmission.commands.size(),
                        shadowDraws=p.draws[0],colourDraws=p.draws[1]]
                        (int w,int h,std::vector<uint8_t> pixels) {
                    if(w<128||h<128||pixels.size()!=std::size_t(w*h*4)) {
                        status="VIVID Creator screenshot is empty";
                        platform::requestQuit();
                        return;
                    }
                    char frameName[32];std::snprintf(frameName,sizeof(frameName),"frame-%04d.ppm",sequenceFrame);
                    const auto outputPath=sequenceFrames?(std::filesystem::path(smokePath)/frameName).string():smokePath;
                    std::ofstream out(outputPath,std::ios::binary);
                    out<<"P6\n"<<w<<" "<<h<<"\n255\n";
                    for(std::size_t i=0;i<pixels.size();i+=4)
                        out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                    out.flush();
                    smokeCompleted=bool(out);
                    std::printf("VIVID Creator human skin/eye/groom GPU preview captured: %s headCommands=%zu actor=%s shadow=%u colour=%u pass=%d\n",
                        outputPath.c_str(),headCommands,previewMotion?"MODEL_MORPH":actor?"GPU":"CPU",
                        unsigned(shadowDraws),unsigned(colourDraws),int(smokeCompleted));
                    if(sequenceFrames&&sequenceFrame+1==sequenceFrames)std::printf("VIVID preview footEndpointMaxDrift=%.8f samples=%zu\n",previewFootDrift,previewFootSamples);
                    if(sequenceFrames&&smokeCompleted&&++sequenceFrame<sequenceFrames){
                        smokeCompleted=false;smokeCaptureRequested=false;smokeFrames=0;
                    }else platform::requestQuit();
                });
            }
            if(++smokeFrames>300 && !smokeCompleted)
                throw std::runtime_error("VIVID Creator did not render skin/eyes/groom within 300 frames");
        }
        ui.render();
        dev.endPass();
    }
    void shutdown() {
        if(!device)return;
        gpuSculptToken.cancel();
        if(!gpuSculptPass.shutdownAfterIdle())
            std::fprintf(stderr,
                "VIVID Creator: GPU ID readback resources still in flight\n");
        if(!releaseHuman())std::fprintf(stderr,
            "VIVID Creator: active GPU resource drain failed at shutdown\n");
        if(retiredHumans && !retiredHumans->drain())
            std::fprintf(stderr,
                "VIVID Creator: pending GPU retirement could not confirm completion\n");
        if(floor.vb.valid())device->destroyBuffer(floor.vb);
        if(floor.ib.valid())device->destroyBuffer(floor.ib);
        if(uiReady)ui.shutdown();
        if(antialiasingReady)antialiasing.shutdown();
        if(rendererReady)renderer.shutdown();
        // A failed drain means the device cannot safely release pending
        // GPU resources. Keep the owner alive rather than running a
        // destructor that could free in-flight model data.
        if(retiredHumans && retiredHumans->pending())
            (void)retiredHumans.release();
        else retiredHumans.reset();
        cache.reset();
        device=nullptr;
    }
};
}
int aiverseMain(int argc,char** argv) {
    auto application=std::make_shared<CreatorApp>();
    std::string opticalCase;
    bool presetSupplied=false;
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--smoke")==0) {
            application->smoke=true;
            if(const char* path=std::getenv("AIVERSE_VIVID_CREATOR_CAPTURE"))
                application->smokePath=path;
        }else if(std::strcmp(argv[i],"--appearance")==0 && i+1<argc) {
            try {
                std::ifstream file(argv[++i],std::ios::binary);
                if(!file)throw std::runtime_error("Cannot open appearance recipe");
                std::string bytes((std::istreambuf_iterator<char>(file)),{});
                if(bytes.size()>128*1024)throw std::runtime_error("Appearance recipe too large");
                application->appearanceOverride=HumanAppearance::fromJson(Json::parse(bytes));
            }catch(const std::exception& e){std::fprintf(stderr,"VIVID appearance: %s\n",e.what());return 2;}
        }else if(std::strcmp(argv[i],"--studio-lighting")==0 && i+1<argc) {
            application->studioLighting.preset=argv[++i];(void)application->studioLighting.rig();application->studioLightingEnabled=true;
        }else if(std::strcmp(argv[i],"--preview-pose")==0 && i+1<argc) {
            HumanPreviewAnimation settings;settings.pose=argv[++i];try{application->previewPose=settings.poseIndex();application->previewMotion=true;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 2;}
        }else if(std::strcmp(argv[i],"--preview-head-sweep")==0) {
            application->previewHeadSweep=true;
        }else if(std::strcmp(argv[i],"--preview-walk")==0) {
            application->previewWalk=true;application->previewMotion=true;
        }else if(std::strcmp(argv[i],"--preview-bvh")==0 && i+1<argc) {
            application->previewBvhPath=argv[++i];
            application->previewWalk=true;application->previewMotion=true;
        }else if(std::strcmp(argv[i],"--preview-sequence")==0 && i+1<argc) {
            try{application->sequenceFrames=std::stoi(argv[++i]);if(application->sequenceFrames<1||application->sequenceFrames>720)return 2;}catch(...){return 2;}
        }else if(std::strcmp(argv[i],"--preview-time")==0 && i+1<argc) {
            try{application->previewTime=std::stof(argv[++i]);if(!std::isfinite(application->previewTime)||application->previewTime<0||application->previewTime>6)return 2;}catch(...){return 2;}
        }else if(std::strcmp(argv[i],"--lod")==0 && i+1<argc) {
            const std::string mode=argv[++i];
            if(mode!="auto" && mode!="highest")return 2;
            application->automaticLod=mode=="auto";
        }else if(std::strcmp(argv[i],"--preview-light-sweep")==0) {
            application->previewLightSweep=true;
        }else if(std::strcmp(argv[i],"--preview-bind-pose")==0) {
            application->previewBindPose=true;
        }else if(std::strcmp(argv[i],"--preview-fixed-pose")==0) {
            application->previewFixedPose=true;
        }else if(std::strcmp(argv[i],"--no-aa")==0) {
            application->aaEnabled=false;
        }else if(std::strcmp(argv[i],"--camera-distance")==0 && i+1<argc) {
            try{application->captureCameraDistance=std::stof(argv[++i]);
                if(!std::isfinite(application->captureCameraDistance)||application->captureCameraDistance<.5f||application->captureCameraDistance>120.f)return 2;
            }catch(...){return 2;}
        }else if(std::strcmp(argv[i],"--shadow-quality")==0 && i+1<argc) {
            const std::string quality=argv[++i];
            if(quality=="off")application->captureShadowQuality=ShadowQuality::Off;
            else if(quality=="high")application->captureShadowQuality=ShadowQuality::High;
            else if(quality=="medium")application->captureShadowQuality=ShadowQuality::Medium;
            else return 2;
        }else if(std::strcmp(argv[i],"--eye-detail")==0) {
            application->portraitCapture=true;application->closeupCapture=true;application->eyeDetailCapture=true;
        }else if(std::strcmp(argv[i],"--closeup")==0) {
            application->portraitCapture=true;application->closeupCapture=true;
        }else if(std::strcmp(argv[i],"--portrait")==0) {
            application->portraitCapture=true;
        }else if(std::strcmp(argv[i],"--still")==0) {
            application->animate=false;
            application->wind=0.0f;
        }else if(std::strcmp(argv[i],"--cpu-morph")==0) {
            application->forceCpuMorph=true;
        }else if(std::strcmp(argv[i],"--presentation-config")==0 && i+1<argc) {
            try {
                application->presentation=VividPresentationSettings::loadFile(argv[++i]);
            }catch(const std::exception& e) {
                std::fprintf(stderr,"VIVID presentation config: %s\n",e.what());
                return 2;
            }
        }else if(std::strcmp(argv[i],"--groom-quality")==0 && i+1<argc) {
            if(!VividPresentationSettings::parseGroomQuality(
                   argv[++i],application->presentation.groomQuality)) {
                std::fprintf(stderr,"VIVID: invalid groom quality\n");
                return 2;
            }
        }else if(std::strcmp(argv[i],"--material-quality")==0 && i+1<argc) {
            const std::string tier=argv[++i];
            if(tier=="low")application->presentation.materialQuality=HumanMaterialQuality::Low;
            else if(tier=="medium")application->presentation.materialQuality=HumanMaterialQuality::Medium;
            else if(tier=="high")application->presentation.materialQuality=HumanMaterialQuality::High;
            else {
                std::fprintf(stderr,"VIVID: invalid material quality '%s'\n",tier.c_str());
                return 2;
            }
        }else if(std::strcmp(argv[i],"--optical-case")==0 && i+1<argc) {
            opticalCase=argv[++i];
        }else if(std::strcmp(argv[i],"--preset")==0 && i+1<argc) {
            presetSupplied=true;
            const std::string preset=argv[++i];
            std::string error;
            if(!application->session.preset(preset,&error)) {
                std::fprintf(stderr,"VIVID Creator preset: %s\n",error.c_str());
                return 2;
            }
            application->syncDraft();
        }else if(std::strcmp(argv[i],"--asset")==0 && i+1<argc) {
            const auto& path=argv[++i];
            if(std::strlen(path)>=sizeof(application->manifestFile)) {
                std::fprintf(stderr,"VIVID Creator: package path too long\n");
                return 2;
            }
            std::strncpy(application->manifestFile,path,
                         sizeof(application->manifestFile)-1);
            application->commandLineAsset=true;
        }else{
            std::fprintf(stderr,"VIVID Creator: unsupported argument %s\n",argv[i]);
            return 2;
        }
    }
    if(application->previewLightSweep&&(!application->sequenceFrames||!application->studioLightingEnabled)){std::fprintf(stderr,"preview-light-sweep requires preview-sequence and studio-lighting\n");return 2;}
    if(application->previewHeadSweep&&!application->sequenceFrames){std::fprintf(stderr,"preview-head-sweep requires preview-sequence\n");return 2;}
    if(application->sequenceFrames){
        if(!application->smoke||!application->previewMotion||application->previewTime>=0){
            std::fprintf(stderr,"preview-sequence requires --smoke --preview-pose and no --preview-time\n");return 2;
        }
        std::filesystem::create_directories(application->smokePath);
    }
    // Optical A/B captures use one fixed neutral human, camera and light.
    // They cannot be combined with a preset or external mesh, since either
    // would invalidate one-variable attribution in the resulting image.
    if(!opticalCase.empty()) {
        if(!application->smoke || presetSupplied || application->commandLineAsset) {
            std::fprintf(stderr,
                "VIVID optical capture requires --smoke and forbids --preset/--asset\n");
            return 2;
        }
        HumanDNA variant=HumanDNA::preset("neutral");
        if(!OpticalCaptureCases::apply(variant,opticalCase)) {
            std::fprintf(stderr,"VIVID: unknown optical case %s\n",opticalCase.c_str());
            return 2;
        }
        std::string error;
        if(!application->session.edit([&](HumanDNA& dna){dna=variant;},&error)) {
            std::fprintf(stderr,"VIVID optical capture: %s\n",error.c_str());
            return 2;
        }
        application->syncDraft();
        application->animate=false;
        application->inspectGroom=false;
        application->wind=0.0f;
        std::printf("VIVID optical case=%s staticCamera=1 staticLighting=1 frozenMotion=1\n",
                    opticalCase.c_str());
    }
    platform::AppConfig config;
    config.title="AIVERSE VIVID Creator - Digital Human";
    config.width=1500;
    config.height=920;
    config.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[application](gfx::Device& device) {
        try{application->init(device);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID Creator init: %s\n",e.what());
            platform::requestQuit();
        }
    };
    callbacks.frame=[application](gfx::Device& device,double dt) {
        try{application->frame(device,dt);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID Creator frame: %s\n",e.what());
            platform::requestQuit();
        }
    };
    callbacks.afterFrame=[application](gfx::Device&) {
        // Runs AFTER endFrame. The platform RHI completion marker is only
        // legal after the final draw/compute encoder is submitted.
        if(application->retiredHumans && application->retiredHumans->pending())
            (void)application->retiredHumans->collect();
        if(application->cache)application->cache->pruneExpired();
    };
    callbacks.event=[application](const platform::Event& event) {
        application->ui.handleEvent(event);
    };
    callbacks.shutdown=[application]{application->shutdown();};
    const int code=platform::run(config,std::move(callbacks));
    return code || (application->smoke && !application->smokeCompleted) ? 1 : 0;
}
