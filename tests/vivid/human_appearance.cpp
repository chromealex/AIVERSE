#include "engine/character/vivid/HumanPreview.h"
#include "engine/character/vivid/HumanAppearance.h"
#include "engine/character/vivid/VividPresentationSettings.h"
#include "engine/character/vivid/VividRendererQuality.h"
#include "tools/vivid-creator/OpticalCaptureCases.h"
#include "engine/character/vivid/ScalpSkinning.h"
#include "engine/character/vivid/GroomSelfCollision.h"
#include "engine/character/vivid/GroomSegmentCollision.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/render/ModelShaders.h"
#include "engine/render/Animation.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void expect(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
bool finite(render::float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
void test() {
    HumanAppearance look;
    expect(HumanAppearance::fromJson(look.toJson()).toJson()==look.toJson(),"appearance roundtrip");
    auto invalid=look.toJson();auto badValues=invalid["values"];badValues.set("skin.roughness",Json::number(-1.0));invalid.set("values",badValues);
    bool rejected=false;try{(void)HumanAppearance::fromJson(invalid);}catch(...){rejected=true;}
    expect(rejected,"negative roughness accepted");
    render::Model source;
    for(const auto* role:{"skin","hair","lashes","eyes","cloth"}) {
        render::Material m;m.name=role;m.alpha=render::AlphaMode::Blend;m.baseTex=3;
        source.materials.push_back(m);look.materials[role]=role;
    }
    applyHumanAppearance(source,look);
    for(const auto& m:source.materials) {
        expect(bool(m.shaderGraph)&&m.baseTex==3&&m.alpha==render::AlphaMode::Blend,"appearance lost textures/alpha");
        expect(m.shaderGraph->nodes<=128,"appearance graph exceeds budget");
    }
    expect(source.materials[4].roughness>.8f,"cloth default still glossy");
    const auto before=source.materials[0].shaderGraph;
    look.values["skin.roughness"]=.3f;applyHumanAppearance(source,look);
    expect(source.materials[0].roughness<.35f && source.materials[0].shaderGraph!=before,"roughness not applied");
    {auto made=look;for(const char* k:{"makeup.lips","makeup.blush","makeup.shadow","makeup.stubble"})made.values[k]=.8f;
     render::Model copy=source;applyHumanAppearance(copy,made);
     expect(bool(copy.materials[0].shaderGraph)&&copy.materials[0].shaderGraph->nodes>source.materials[0].shaderGraph->nodes,"makeup layers not compiled into the skin graph");
     expect(copy.materials[0].shaderGraph->nodes<=256,"makeup graph unbounded");}
    source.boundsMin={-1,0,-1};source.boundsMax={1,2,1};source.nodes.resize(2);
    render::Skin skin;skin.joints={1};source.skins.push_back(skin);
    std::vector<render::mat4> world(2,render::mat4::translate({0,1,0}));
    const auto original=world;
    applyHumanBody(source,look,world);
    expect(world[1].m[13]==original[1].m[13],"neutral body changes pose");
    look.values["body.height"]=1.2f;applyHumanBody(source,look,world);
    expect(std::abs(world[1].m[13]-1.2f)<.001f && world[0].m[13]==1.f,"body deformation double applied or absent");
    expect(source.nodes[1].t.y==0,"body mutated source skeleton");
    source.meshes.resize(1);source.meshes[0].boundsMin={-.2f,.8f,-.2f};source.meshes[0].boundsMax={.2f,1.2f,.2f};
    source.nodes[0].mesh=0;world=original;applyHumanBody(source,look,world);
    expect(std::abs(world[0].m[13]+world[0].m[5]-2.4f)<.003f,"rigid groom did not follow body height");

    for(auto name:{"neutral","warm","cool","stylized"}) {
        auto dna=HumanDNA::preset(name);
        auto mat=humanMaterials(dna);
        expect(mat.skin.shaderGraph && mat.iris.shaderGraph &&
               mat.hair.shaderGraph,"PBR human shader graphs missing");
        expect(mat.skin.shading==render::ShadingModel::Skin &&
               mat.iris.shading==render::ShadingModel::Eye &&
               mat.cornea.shading==render::ShadingModel::Eye &&
               mat.hair.shading==render::ShadingModel::Hair,
               "human materials did not select dedicated PBR modes");
        expect(std::string(render::shaders::kModelFS).find("human skin")!=std::string::npos &&
               std::string(render::shaders::kModelMSL).find("human BRDF lobes")!=std::string::npos,
               "GLSL/Metal human BRDF source missing");
        render::Model modelA,modelB;
        const auto original=mat.skin;
        auto adjusted=original;
        adjusted.humanProfile.x*=0.2f;
        expect(!render::equivalentGpuMaterial(modelA,original,modelB,adjusted) &&
               render::gpuMaterialStateHash(modelA,original)!=
               render::gpuMaterialStateHash(modelB,adjusted),
               "GPU batching merged human materials with different subsurface profiles");
        for(const auto& material:{mat.skin,mat.iris,mat.hair}) {
            expect(material.shaderGraph->domain=="material" &&
                   !material.shaderGraph->glsl.empty() &&
                   !material.shaderGraph->msl.empty() &&
                   material.shaderGraph->outputs.count("roughness") &&
                   material.shaderGraph->outputs.count("color"),
                   "GLSL/Metal material surface graph invalid");
        }
        render::Material scanned;
        scanned.name="scan.skin";
        scanned.baseTex=4;
        scanned.normalTex=5;
        scanned.mrTex=6;
        scanned.occlusionTex=7;
        scanned.uvOffset={0.2f,0.1f};
        scanned.uvScale={1.2f,0.9f};
        scanned.normalScale=0.7f;
        const auto specialized=humanMaterialFromSource(HumanSurface::Skin,dna,scanned);
        expect(specialized.shading==render::ShadingModel::Skin &&
               specialized.name=="scan.skin" &&
               specialized.baseTex==4 && specialized.normalTex==5 &&
               specialized.mrTex==6 && specialized.occlusionTex==7 &&
               specialized.uvOffset.x==0.2f && specialized.normalScale==0.7f,
               "VIVID skin shader discarded authored albedo/normal/MR texture maps");
        expect(mat.cornea.alpha==render::AlphaMode::Blend &&
               !mat.cornea.castShadow,"cornea incorrectly opaque / shadow casting");
        auto human=generateHumanPreview(dna,GroomQuality::Medium);
        expect(human.head&&human.hair&&human.rig.valid(),
               "reference human head or rig missing");
        expect(human.head->findNode("head")>=0 &&
               human.head->findNode("jaw")>=0 &&
               human.head->findNode("eye.L")>=0 &&
               human.head->findNode("eye.R")>=0,
               "facial joint hierarchy incomplete");
        expect(human.rig.hasFacialJoints(),"reference head has no jaw/eye joints");
        render::Pose bind;
        render::bindPose(*human.head,bind);
        std::vector<render::mat4> world;
        render::worldMatrices(*human.head,bind,world);
        const auto& eye=world[std::size_t(human.head->findNode("eye.L"))];
        const auto& nose=world[std::size_t(human.head->findNode("nose"))];
        expect(std::fabs(eye.at(2,3)-0.75f*dna.anatomy.headScale)<1.e-5f &&
               std::fabs(nose.at(2,3)-0.78f*dna.anatomy.headScale)<1.e-5f,
               "head parenting applied anatomy scale twice and buried facial features");
        const auto& iris=world[std::size_t(human.head->findNode("iris.L"))];
        const auto& cornea=world[std::size_t(human.head->findNode("cornea.L"))];
        expect(iris.at(2,3)>eye.at(2,3)+eye.at(2,2) &&
               iris.at(2,3)<cornea.at(2,3)+cornea.at(2,2),
               "iris pupil must lie between opaque sclera and corneal shell");
        expect(human.facialShapes.targets()==5 &&
               human.facialBinding.routes()==5 &&
               human.facialShapes.vertices()==human.head->vertices.size(),
               "facial morph routes incomplete");
        expect(human.head->materials.size()>10 &&
               human.head->vertices.size()>2500,
               "procedural skin/eye/teeth surfaces missing");
        std::size_t irisVertices=0;
        for(const auto& mesh:human.head->meshes) {
            if(mesh.name!="iris.L"&&mesh.name!="iris.R")continue;
            for(const auto& p:mesh.primitives)
                for(std::uint32_t k=0;k<p.vertexCount;++k) {
                    const auto& uv=human.head->vertices[std::size_t(p.firstVertex+k)].uv;
                    expect(uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1,
                           "iris radial shader UV outside bounds");
                    ++irisVertices;
                }
        }
        expect(irisVertices>=128,"radial iris surfaces missing");
        expect(human.groom.guides().size()>8 &&
               human.groom.vertexCount()==human.hair->vertices.size(),
               "hair geometry and guides mismatch");
        expect(human.hair->meshes[0].deformable,
               "dynamic groom incorrectly eligible for geometry pooling");
        expect(human.groom.step(0.016f,{0.5f,0.1f,0.2f}),
               "groom timestep failed");
        expect(!human.groom.step(-1.0f) &&
               !human.groom.step(1.5f),"invalid groom dt accepted");
        auto changed=human.hair->vertices;
        expect(human.groom.updateCardVertices(changed),
               "groom render stream did not update");
        for(const auto& v:changed)expect(finite(v.pos)&&finite(v.nrm),
                                         "groom generated nonfinite render vertex");
        const auto stats=human.groom.stats();
        expect(stats.triangles==human.hair->indices.size()/3,
               "groom card triangle stats mismatch");
    }
    // Stage 7AH: existing renderer is the sole global material-quality
    // owner. VIVID can choose a cheaper appearance, but never a more
    // expensive one than the actual renderer tier selected by its host.
    {
        const auto original=HumanDNA::preset("neutral").toJson();
        VividPresentationSettings requested;
        requested.materialQuality=HumanMaterialQuality::High;
        requested.groomQuality=GroomQuality::High;
        for(const auto rendererTier:{render::MaterialQuality::Low,
                                     render::MaterialQuality::Medium,
                                     render::MaterialQuality::High}) {
            const auto expected=vividMaterialQuality(rendererTier);
            expect(rendererMaterialQuality(expected)==rendererTier,
                   "VIVID->Renderer3D quality mapping is not reversible");
            const auto resolved=resolveVividPresentation(requested,rendererTier);
            expect(resolved.materialQuality==expected &&
                   resolved.groomQuality==GroomQuality::High &&
                   requested.materialQuality==HumanMaterialQuality::High,
                   "project quality clamp changed caller or groom geometry");
            const auto material=humanMaterial(
                HumanSurface::Skin,HumanDNA::preset("neutral"),
                resolved.materialQuality);
            expect(material.shaderGraph && material.shaderGraph->nodes<=
                   humanMaterialBudget(expected).maxNodes,
                   "renderer-capped VIVID skin exceeded material budget");
        }
        requested.materialQuality=HumanMaterialQuality::Low;
        for(const auto rendererTier:{render::MaterialQuality::Medium,
                                     render::MaterialQuality::High})
            expect(resolveVividPresentation(requested,rendererTier)
                       .materialQuality==HumanMaterialQuality::Low,
                   "host capability resolution must not increase quality");
        auto invalid=requested;
        invalid.materialQuality=static_cast<HumanMaterialQuality>(127);
        bool rejected=false;
        try { (void)resolveVividPresentation(invalid,render::MaterialQuality::High); }
        catch(const std::invalid_argument&) {rejected=true;}
        expect(rejected,"invalid VIVID appearance tier must fail closed");
        expect(HumanDNA::preset("neutral").toJson()==original,
               "renderer quality resolution must not modify human DNA");
    }
    // Stage 7AG project-level profile is independent of the human recipe.
    {
        VividPresentationSettings settings;
        expect(settings.valid() &&
               settings.materialQuality==HumanMaterialQuality::High &&
               settings.groomQuality==GroomQuality::High,
               "VIVID default presentation profile changed");
        const auto before=HumanDNA::preset("neutral").toJson();
        for(const auto quality:{HumanMaterialQuality::Low,
                                HumanMaterialQuality::Medium,
                                HumanMaterialQuality::High}) {
            for(const auto groom:{GroomQuality::Low,GroomQuality::Medium,
                                  GroomQuality::High}) {
                settings.materialQuality=quality;
                settings.groomQuality=groom;
                const auto document=settings.toJson();
                const auto restored=VividPresentationSettings::fromJson(document);
                expect(restored.valid() && restored.materialQuality==quality &&
                       restored.groomQuality==groom,
                       "VIVID presentation profile JSON roundtrip failed");
                const auto parsed=VividPresentationSettings::fromJson(
                    Json::parse(document.dump()));
                expect(parsed.materialQuality==quality &&
                       parsed.groomQuality==groom,
                       "VIVID presentation profile textual JSON drift");
            }
        }
        const auto reject=[](Json document) {
            try { (void)VividPresentationSettings::fromJson(document); }
            catch(const std::invalid_argument&) {return true;}
            return false;
        };
        auto invalid=settings.toJson();
        invalid.set("version",Json::number(std::int64_t(2)));
        expect(reject(invalid),"VIVID presentation accepted unsupported version");
        invalid=settings.toJson();
        invalid.set("groomQuality",Json::string("ultra"));
        expect(reject(invalid),"VIVID presentation accepted unknown groom tier");
        invalid=settings.toJson();
        invalid.set("materialQuality",Json::number(std::int64_t(1)));
        expect(reject(invalid),"VIVID presentation accepted non-string material tier");
        invalid=settings.toJson();
        invalid.set("textureStreaming",Json::fromBool(true));
        expect(reject(invalid),"VIVID presentation silently accepted unsupported texture toggle");
        settings.materialQuality=HumanMaterialQuality::Low;
        settings.groomQuality=GroomQuality::Low;
        auto reference=generateHumanPreview(
            HumanDNA::preset("neutral"),settings);
        expect(reference.head && reference.hair &&
               reference.hair->materials.front().shading==render::ShadingModel::Pbr &&
               reference.groom.stats().guides>0,
               "project presentation settings did not propagate to the renderer");
        expect(HumanDNA::preset("neutral").toJson()==before,
               "VIVID presentation quality must not mutate HumanDNA");
    }
    // Stage 7AF: quality is a presentation policy, independent of DNA and
    // of geometry/morph source IDs. Old APIs default to High quality.
    {
        auto dna=HumanDNA::preset("neutral");
        const auto baseline=dna.toJson();
        const auto skinDefault=humanMaterial(HumanSurface::Skin,dna);
        const auto skinHigh=humanMaterial(HumanSurface::Skin,dna,
                                         HumanMaterialQuality::High);
        expect(skinDefault.shaderGraph->glsl==skinHigh.shaderGraph->glsl &&
               skinDefault.shaderGraph->msl==skinHigh.shaderGraph->msl,
               "VIVID default shader graph must remain High-compatible");
        for(const auto tier:{HumanMaterialQuality::Low,
                             HumanMaterialQuality::Medium,
                             HumanMaterialQuality::High}) {
            const auto budget=humanMaterialBudget(tier);
            const auto collection=humanMaterials(dna,tier);
            for(const auto& material:{collection.skin,collection.iris,
                                     collection.hair}) {
                expect(material.shaderGraph &&
                       material.shaderGraph->nodes<=budget.maxNodes &&
                       material.shaderGraph->samples<=budget.maxSamples &&
                       !material.shaderGraph->glsl.empty() &&
                       !material.shaderGraph->msl.empty(),
                       "VIVID material tier exceeded node/sample budget");
            }
            if(tier==HumanMaterialQuality::Low) {
                expect(collection.skin.shading==render::ShadingModel::Pbr &&
                       collection.iris.shading==render::ShadingModel::Pbr &&
                       collection.cornea.shading==render::ShadingModel::Pbr &&
                       collection.hair.shading==render::ShadingModel::Pbr,
                       "mobile tier must avoid dedicated human BRDF branches");
            } else {
                expect(collection.skin.shading==render::ShadingModel::Skin &&
                       collection.iris.shading==render::ShadingModel::Eye &&
                       collection.hair.shading==render::ShadingModel::Hair,
                       "medium/high tiers lost dedicated human BRDFs");
            }
        }
        for(const auto surface:{HumanSurface::Skin,HumanSurface::Iris,
                                HumanSurface::Hair}) {
            const auto low=humanMaterial(surface,dna,HumanMaterialQuality::Low);
            const auto medium=humanMaterial(surface,dna,HumanMaterialQuality::Medium);
            const auto high=humanMaterial(surface,dna,HumanMaterialQuality::High);
            expect(low.shaderGraph->nodes<medium.shaderGraph->nodes &&
                   medium.shaderGraph->nodes<high.shaderGraph->nodes,
                   "VIVID shader graph complexity must scale with quality");
        }
        render::Material imported;
        imported.baseTex=7;imported.mrTex=8;
        imported.normalTex=9;imported.occlusionTex=10;
        imported.uvScale={1.25f,0.95f};
        const auto mobile=humanMaterialFromSource(HumanSurface::Skin,dna,
                                  imported,HumanMaterialQuality::Low);
        expect(mobile.baseTex==7 && mobile.mrTex==8 &&
               mobile.normalTex==9 && mobile.occlusionTex==10 &&
               mobile.uvScale.x==1.25f,
               "material quality discarded imported texture references");
        auto preview=generateHumanPreview(dna,GroomQuality::Low,
                                            HumanMaterialQuality::Low);
        expect(preview.head && preview.hair &&
               preview.hair->materials.front().shading==render::ShadingModel::Pbr &&
               preview.hair->materials.front().shaderGraph->nodes<=
                   humanMaterialBudget(HumanMaterialQuality::Low).maxNodes,
               "low-quality human preview groom must use mobile material");
        expect(dna.toJson()==baseline,
               "selecting VIVID material quality modified versioned DNA");
    }
    // Regression: custom head proportions must use an ellipsoid collision
    // matching generated roots, not a hard-coded sphere in world units.
    for(float width:{0.65f,1.0f,1.35f}) {
        auto dna=HumanDNA::preset("neutral");
        dna.anatomy.headWidth=width;
        dna.anatomy.headScale=1.2f;
        HumanGroomRuntime groom;
        groom.generate(dna,GroomQuality::Low);
        const float rx=0.72f*dna.anatomy.headWidth*dna.anatomy.headScale;
        const float ry=0.90f*dna.anatomy.headScale;
        const float rz=0.75f*dna.anatomy.headScale;
        for(int frame=0;frame<24;++frame)
            expect(groom.step(0.016f,{2.0f,-0.5f,1.0f}),
                   "DNA-scaled scalp collision step failed");
        for(const auto& guide:groom.guides()) {
            expect(finite(guide.position.front()),"groom root became nonfinite");
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto p=guide.position[j];
                const float q=std::sqrt((p.x/rx)*(p.x/rx)+
                                        (p.y/ry)*(p.y/ry)+
                                        (p.z/rz)*(p.z/rz));
                expect(finite(p)&&q>=0.999f,
                       "groom strand penetrated DNA-scaled head ellipsoid");
            }
        }
    }

    {
        std::array<GroomGuide,2> guides;
        for(auto& guide:guides) {
            guide.root={0,1,0};
            guide.rest={{0,1,0},{0,1.1f,0}};
            guide.position=guide.rest;
            guide.previous=guide.rest;
        }
        guides[1].root={0.001f,1,0};
        guides[1].rest[0]=guides[1].position[0]=
            guides[1].previous[0]=guides[1].root;
        guides[1].rest[1]=guides[1].position[1]=
            guides[1].previous[1]={0.001f,1.1f,0};
        GroomSelfCollisionConfig config;
        config.enabled=true;
        config.separation=0.02f;
        config.response=1.0f;
        GroomSelfCollisionStats stats;
        const float before=render::length(
            guides[0].position[1]-guides[1].position[1]);
        expect(GroomSelfCollision::resolve(guides,config,stats) &&
               stats.contacts==1 && stats.pairChecks==1,
               "groom self collision did not discover overlapping strands");
        const float after=render::length(
            guides[0].position[1]-guides[1].position[1]);
        expect(after>before && guides[0].position[0].y==1.f &&
               guides[1].position[0].x==0.001f,
               "groom contact correction moved roots or failed separation");
    }
    // Particle broadphase must reuse owner scratch and preserve the exact
    // standalone pair-order/contact result on repeated identical input.
    {
        std::array<GroomGuide,3> fixture{};
        for(std::size_t i=0;i<fixture.size();++i) {
            const float x=0.008f*float(i);
            fixture[i].root={x,1.f,0.f};
            fixture[i].rest=fixture[i].position={{x,1.f,0.f},
                                                   {x,1.12f,0.f}};
            fixture[i].previous=fixture[i].position;
        }
        GroomSelfCollisionConfig cfg;
        cfg.enabled=true;
        cfg.separation=0.02f;
        cfg.response=1.f;
        auto reference=fixture;
        GroomSelfCollisionStats referenceStats;
        expect(GroomSelfCollision::resolve(reference,cfg,referenceStats),
               "reference particle broadphase failed");
        GroomParticleWorkspace scratch;
        auto first=fixture;
        GroomSelfCollisionStats stats;
        expect(GroomSelfCollision::resolve(first,cfg,stats,scratch),
               "persistent particle broadphase failed");
        expect(stats.contacts==referenceStats.contacts &&
               stats.pairChecks==referenceStats.pairChecks &&
               stats.budgetHit==referenceStats.budgetHit,
               "persistent particle broadphase changed collision counts");
        for(std::size_t i=0;i<fixture.size();++i) {
            expect(render::length(first[i].position[1]-
                                  reference[i].position[1])<1.e-7f &&
                   render::length(first[i].position[0]-
                                  fixture[i].position[0])<1.e-7f,
                   "particle broadphase changed corrections or moved roots");
        }
        const auto particleCap=scratch.particles.capacity();
        const auto entryCap=scratch.entries.capacity();
        const auto correctionCap=scratch.corrections.capacity();
        auto second=fixture;
        GroomSelfCollisionStats again;
        expect(GroomSelfCollision::resolve(second,cfg,again,scratch) &&
               scratch.particles.capacity()==particleCap &&
               scratch.entries.capacity()==entryCap &&
               scratch.corrections.capacity()==correctionCap &&
               again.contacts==stats.contacts &&
               again.pairChecks==stats.pairChecks,
               "particle broadphase did not reuse capacity deterministically");
        scratch.reset();
        expect(scratch.entries.empty() && scratch.particles.empty() &&
               scratch.entries.capacity()==entryCap,
               "particle workspace reset released capacity");
    }
    // The owner-held scratch must retain its capacity while frames reuse
    // the same model. Equal snapshots must yield identical ordered contacts.
    {
        std::array<GroomGuide,2> originals{};
        originals[0].root={-0.04f,0,0};
        originals[1].root={0,-0.04f,0.004f};
        originals[0].rest=originals[0].position={
            {-0.04f,0,0},{0.04f,0,0}};
        originals[1].rest=originals[1].position={
            {0,-0.04f,0.004f},{0,0.04f,0.004f}};
        for(auto& guide:originals)guide.previous=guide.position;
        GroomSelfCollisionConfig settings;
        settings.enabled=true;
        settings.segmentEnabled=true;
        settings.separation=0.02f;
        settings.response=1.f;
        GroomSegmentWorkspace scratch;
        auto first=originals;
        GroomSelfCollisionStats firstStats;
        expect(GroomSegmentCollision::resolve(first,settings,
            firstStats,scratch) && firstStats.contacts==1,
            "reusable strand workspace failed initial contact");
        const auto originalCapacity=scratch.cells.capacity();
        const auto pairCapacity=scratch.pairs.capacity();
        const auto segmentCapacity=scratch.segments.capacity();
        expect(scratch.segments.size()==2 && segmentCapacity>=2,
               "groom workspace did not retain segment snapshot");
        auto second=originals;
        GroomSelfCollisionStats secondStats;
        expect(GroomSegmentCollision::resolve(second,settings,
            secondStats,scratch) &&
            scratch.cells.capacity()==originalCapacity &&
            scratch.pairs.capacity()==pairCapacity &&
            scratch.segments.capacity()==segmentCapacity &&
            scratch.segments.size()==2 &&
            firstStats.contacts==secondStats.contacts &&
            firstStats.pairChecks==secondStats.pairChecks &&
            render::length(first[0].position[1]-second[0].position[1])<1.e-6f,
            "scratch capacity changed or reusable collision lost determinism");
        // Reset scratch and keep capacity; roots remain immutable.
        scratch.reset();
        expect(scratch.cells.empty() && scratch.pairs.empty() &&
            scratch.cells.capacity()==originalCapacity &&
            scratch.segments.empty() &&
            scratch.segments.capacity()==segmentCapacity,
            "workspace reset unexpectedly freed reusable broadphase storage");
    }
    // Crossed line segments must repel even when their endpoint particles
    // are farther apart than the contact radius. This tests the continuous
    // segment narrowphase rather than the particle-particle approximation.
    {
        std::array<GroomGuide,2> hair{};
        hair[0].root={-0.04f,0,0};
        hair[1].root={0,-0.04f,0.004f};
        hair[0].rest=hair[0].position={{-0.04f,0,0},{0.04f,0,0}};
        hair[1].rest=hair[1].position={{0,-0.04f,0.004f},{0,0.04f,0.004f}};
        for(auto& guide:hair)guide.previous=guide.position;
        hair[0].previous[1].y=-0.01f;
        GroomSelfCollisionConfig config;
        config.enabled=true;
        config.segmentEnabled=true;
        config.separation=0.02f;
        config.response=1.f;
        config.friction=0.5f;
        GroomSelfCollisionStats report;
        const auto start0=hair[0].position[0],start1=hair[1].position[0];
        expect(GroomSegmentCollision::resolve(hair,config,report) &&
               report.contacts>0 && report.pairChecks==1,
               "segment collision missed crossing strand capsules");
        expect(render::length(hair[0].position[1]-hair[0].previous[1])<0.011f &&
               render::length(hair[0].position[0]-start0)<1.e-7f &&
               render::length(hair[1].position[0]-start1)<1.e-7f,
               "segment friction changed pinned root or added velocity");
        const auto first=hair[0].position[1];
        auto invalid=config;
        invalid.friction=std::numeric_limits<float>::quiet_NaN();
        expect(!GroomSegmentCollision::resolve(hair,invalid,report) &&
               render::length(hair[0].position[1]-first)<1.e-7f,
               "invalid strand friction partially changed live groom");
    }
    {
        std::vector<GroomGuide> packed(64);
        for(std::size_t i=0;i<packed.size();++i) {
            const float off=float(i)*0.00001f;
            auto& g=packed[i];
            g.root={off,0,0};
            g.rest=g.position={{off,0,0},{off,0.02f,0}};
            g.previous=g.position;
        }
        GroomSelfCollisionConfig config;
        config.enabled=true;
        config.segmentEnabled=true;
        config.separation=0.04f;
        config.maxPairChecks=1024;
        GroomSelfCollisionStats report;
        expect(GroomSegmentCollision::resolve(packed,config,report) &&
               report.budgetHit && report.pairChecks<=config.maxPairChecks,
               "bounded segment contact search ignored pair budget");
        for(const auto& g:packed)
            expect(render::length(g.position[0]-g.root)<1.e-7f &&
                   finite(g.position[1]),
                   "budget-limited contacts damaged fixed root or guide");
    }
    // Excessively dense candidate segment sets must not cancel the entire
    // groom simulation or modify the existing positions.
    {
        std::vector<GroomGuide> oversized(16385);
        for(std::size_t i=0;i<oversized.size();++i) {
            auto& g=oversized[i];
            g.root={float(i)*0.01f,0,0};
            g.rest=g.position={g.root,g.root+render::float3{0,0.02f,0}};
            g.previous=g.position;
        }
        GroomSelfCollisionConfig config;
        config.enabled=true;
        config.segmentEnabled=true;
        GroomSelfCollisionStats report;
        const auto prior=oversized[0].position[1];
        expect(GroomSegmentCollision::resolve(oversized,config,report) &&
               report.budgetHit && report.contacts==0 &&
               render::length(oversized[0].position[1]-prior)<1.e-7f,
               "dense groom segment budget did not fail open without mutation");
    }
    // Inter-guide self collision is opt-in, deterministic and does not
    // move fixed roots. The spatial neighbor search has a strict pair cap.
    {
        auto dna=HumanDNA::preset("neutral");
        HumanGroomRuntime baseline,colliding,replica;
        baseline.generate(dna,GroomQuality::Low);
        colliding.generate(dna,GroomQuality::Low);
        replica.generate(dna,GroomQuality::Low);
        GroomSelfCollisionConfig config;
        config.enabled=true;
        config.separation=0.04f;
        config.response=0.75f;
        config.maxPairChecks=2048;
        expect(colliding.setSelfCollision(config) &&
               replica.setSelfCollision(config),
               "groom self collision settings rejected");
        const auto originalRoot=colliding.guides()[0].root;
        const auto root2=colliding.guides()[1].root;
        expect(!colliding.setSelfCollision(
                    {true,std::numeric_limits<float>::quiet_NaN(),0.6f,2048}),
               "invalid groom self collision parameters were accepted");
        for(int frame=0;frame<5;++frame) {
            expect(baseline.step(1.f/120.f,{0.8f,0,0.2f}) &&
                   colliding.step(1.f/120.f,{0.8f,0,0.2f}) &&
                   replica.step(1.f/120.f,{0.8f,0,0.2f}),
                   "self-collision groom integration failed");
        }
        expect(render::length(colliding.guides()[0].root-originalRoot)<1e-6f &&
               render::length(colliding.guides()[1].root-root2)<1e-6f &&
               render::length(colliding.guides()[0].position[0]-originalRoot)<1e-6f,
               "self collision moved an anchored guide root");
        for(std::size_t i=0;i<colliding.guides().size();++i)
            for(std::size_t j=0;j<colliding.guides()[i].position.size();++j) {
                const auto a=colliding.guides()[i].position[j];
                const auto b=replica.guides()[i].position[j];
                expect(finite(a) && render::length(a-b)<1e-6f,
                       "groom inter-guide collision not deterministic");
            }
        const auto diag=colliding.selfCollisionStats();
        expect(diag.pairChecks<=5*config.maxPairChecks &&
               (!diag.budgetHit || diag.pairChecks>0),
               "groom spatial grid exceeded the bounded contact budget");
        expect(colliding.stats().updatedMs>=0,
               "groom collision timing instrumentation invalid");
    }

    // Stage 7AA: 8192-guide stress fixture (one segment per guide).
    // Sparse and overloaded distributions exercise both successful
    // broadphase reuse and bounded, non-destructive overload handling.
    {
        constexpr std::size_t guideCount=8192;
        std::vector<GroomGuide> sparse(guideCount);
        for(std::size_t i=0;i<guideCount;++i) {
            const float x=float(i)*0.2f;
            auto& guide=sparse[i];
            guide.root={x,0.f,0.f};
            guide.rest=guide.position={guide.root,
                                       {x,0.005f,0.f}};
            guide.previous=guide.position;
        }
        GroomSelfCollisionConfig cfg;
        cfg.enabled=true;
        cfg.segmentEnabled=true;
        cfg.separation=0.02f;
        cfg.maxPairChecks=1024;
        GroomParticleWorkspace particleScratch;
        GroomSegmentWorkspace segmentScratch;
        const auto start=std::chrono::steady_clock::now();
        auto first=sparse;
        GroomSelfCollisionStats p1,s1;
        expect(GroomSelfCollision::resolve(first,cfg,p1,particleScratch) &&
               GroomSegmentCollision::resolve(first,cfg,s1,segmentScratch),
               "8192-guide sparse collision solve failed");
        expect(!p1.budgetHit && !s1.budgetHit &&
               p1.contacts==0 && s1.contacts==0 &&
               p1.pairChecks<=cfg.maxPairChecks &&
               s1.pairChecks<=cfg.maxPairChecks &&
               segmentScratch.segments.size()==guideCount,
               "8192-guide sparse workload unexpectedly exceeded collision budgets");
        const auto pCap=particleScratch.entries.capacity();
        const auto sCap=segmentScratch.cells.capacity();
        const auto segmentCap=segmentScratch.segments.capacity();
        auto second=sparse;
        GroomSelfCollisionStats p2,s2;
        expect(GroomSelfCollision::resolve(second,cfg,p2,particleScratch) &&
               GroomSegmentCollision::resolve(second,cfg,s2,segmentScratch),
               "8192-guide repeated collision solve failed");
        expect(p1.pairChecks==p2.pairChecks &&
               s1.pairChecks==s2.pairChecks &&
               p1.contacts==p2.contacts && s1.contacts==s2.contacts &&
               particleScratch.entries.capacity()==pCap &&
               segmentScratch.cells.capacity()==sCap &&
               segmentScratch.segments.capacity()==segmentCap,
               "8192-guide collision changed result or reallocated scratch");
        for(std::size_t i=0;i<guideCount;++i)
            expect(render::length(second[i].position[0]-sparse[i].root)<1.e-7f &&
                   render::length(first[i].position[1]-second[i].position[1])<1.e-7f &&
                   finite(first[i].position[1]),
                   "8192-guide stress moved roots or lost deterministic positions");
        const auto elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        std::cout<<"VIVID groom 8192 sparse x2 CPU ms="<<elapsed
                 <<" particleEntries="<<particleScratch.entries.size()
                 <<" segmentCells="<<segmentScratch.cells.size()
                 <<" segmentPairs="<<segmentScratch.pairs.size()<<"\n";

        // Every segment occupies the same small spatial region; the raw
        // candidate budget must skip this optional pass transactionally.
        std::vector<GroomGuide> dense(guideCount);
        for(std::size_t i=0;i<guideCount;++i) {
            auto& guide=dense[i];
            guide.root={0.f,0.f,0.f};
            guide.rest=guide.position={{0.f,0.f,0.f},
                                       {0.f,0.005f,0.f}};
            guide.previous=guide.position;
        }
        const auto before=dense.front().position[1];
        GroomSelfCollisionStats overloaded;
        expect(GroomSegmentCollision::resolve(dense,cfg,overloaded,
                                               segmentScratch) &&
               overloaded.budgetHit && overloaded.contacts==0 &&
               overloaded.pairChecks==0,
               "8192-guide dense segment grid did not skip safely");
        for(const auto& guide:dense)
            expect(render::length(guide.position[0]-guide.root)<1.e-7f &&
                   render::length(guide.position[1]-before)<1.e-7f,
                   "8192-guide dense budget skip mutated live hair");
        GroomSelfCollisionStats bounded;
        expect(GroomSelfCollision::resolve(dense,cfg,bounded,
                                           particleScratch) &&
               bounded.budgetHit && bounded.pairChecks==cfg.maxPairChecks,
               "8192-guide dense particle search did not respect pair cap");
        for(const auto& guide:dense)
            expect(render::length(guide.position[0]-guide.root)<1.e-7f &&
                   finite(guide.position[1]),
                   "8192-guide bounded particle solve corrupted roots");
    }

    // Variable presentation frame rates must use small bounded substeps.
    {
        auto dna=HumanDNA::preset("neutral");
        HumanGroomRuntime batched,individual;
        batched.generate(dna,GroomQuality::Low);
        individual.generate(dna,GroomQuality::Low);
        expect(batched.step(1.0f/30.0f,{1.5f,0,0.4f}),
               "substepped groom simulation rejected a valid frame");
        for(int i=0;i<4;++i)
            expect(individual.step(1.0f/120.0f,{1.5f,0,0.4f}),
                   "fixed groom substep rejected");
        for(std::size_t i=0;i<batched.guides().size();++i)
            for(std::size_t j=0;j<batched.guides()[i].position.size();++j) {
                const auto a=batched.guides()[i].position[j];
                const auto b=individual.guides()[i].position[j];
                expect(finite(a)&&finite(b)&&render::length(a-b)<0.0001f,
                       "groom substeps disagree with equivalent fixed frames");
            }
    }
    // Closed imported-head proxy: CPU groom can collide against actual
    // authored triangles rather than assuming a procedural ellipsoid.
    {
        HumanGroomRuntime groom;
        groom.generate(HumanDNA::preset("neutral"),GroomQuality::Low);
        const std::array<render::float3,8> verts{{
            {-1.4f,-1.4f,-1.4f},{1.4f,-1.4f,-1.4f},
            {1.4f,1.4f,-1.4f},{-1.4f,1.4f,-1.4f},
            {-1.4f,-1.4f,1.4f},{1.4f,-1.4f,1.4f},
            {1.4f,1.4f,1.4f},{-1.4f,1.4f,1.4f}
        }};
        const std::array<std::uint32_t,36> indices{{
            0,2,1,0,3,2, 4,5,6,4,6,7,
            0,1,5,0,5,4, 3,7,6,3,6,2,
            0,4,7,0,7,3, 1,2,6,1,6,5
        }};
        expect(groom.setCollisionMesh(verts,indices),
               "closed imported groom collider rejected");
        expect(groom.hasCollisionMesh(),"groom collider was not retained");
        expect(groom.attachRootsToCollisionMesh(),
               "closed scalp proxy failed to bind groom roots");
        for(const auto& guide:groom.guides())
            expect(std::max({std::fabs(guide.root.x),
                             std::fabs(guide.root.y),
                             std::fabs(guide.root.z)})>=1.399f,
                   "groom root remained inside imported scalp proxy");
        const std::array<std::uint32_t,3> open{0,2,1};
        expect(!groom.setCollisionMesh(verts,open)&&groom.hasCollisionMesh(),
               "open scalp surface wrongly accepted as parity collider");
        const std::array<std::uint32_t,3> bad{0,0,1};
        expect(!groom.setCollisionMesh(verts,bad) && groom.hasCollisionMesh(),
               "invalid collider replaced last-good imported geometry");
        for(int frame=0;frame<4;++frame)
            expect(groom.step(0.016f,{0.1f,0.0f,0.1f}),
                   "imported groom collision simulation failed");
        for(const auto& guide:groom.guides())
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto p=guide.position[j];
                const float extent=std::max({std::fabs(p.x),
                                              std::fabs(p.y),
                                              std::fabs(p.z)});
                expect(finite(p)&&extent>=1.399f,
                       "hair segment remained inside closed import collider");
            }
        auto translated=verts;
        const auto beforeRoot=groom.guides().front().root;
        for(auto& v:translated)v.x+=0.25f;
        expect(groom.refitCollisionMesh(translated),
               "valid animated scalp proxy BVH refit rejected");
        const auto afterRoot=groom.guides().front().root;
        expect(std::fabs((afterRoot.x-beforeRoot.x)-0.25f)<0.0005f &&
               std::fabs(afterRoot.y-beforeRoot.y)<0.0005f &&
               std::fabs(afterRoot.z-beforeRoot.z)<0.0005f,
               "barycentric groom attachment did not follow animated proxy");
        auto nonfinite=translated;
        nonfinite[0].x=std::numeric_limits<float>::quiet_NaN();
        expect(!groom.refitCollisionMesh(nonfinite)&&groom.hasCollisionMesh(),
               "nonfinite scalp refit destroyed last-good BVH");
        expect(render::length(groom.guides().front().root-afterRoot)<0.0001f,
               "failed scalp refit altered last-good root attachment");
        expect(groom.refitCollisionMesh(verts),
               "scalp proxy failed to restore original bind pose");
        groom.clearCollisionMesh();
        expect(!groom.hasCollisionMesh(),"groom collision reset failed");
    }
    // Multi-joint closed scalp proxy CPU skinning matches normalized
    // palette weights and MotionWeave world pose, without touching ECS.
    {
        render::Model model;
        model.nodes.resize(3);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.meshes.resize(1);
        render::Primitive prim;
        prim.firstIndex=0;
        prim.indexCount=3;
        prim.jointPalette={0,1};
        model.meshes[0].primitives.push_back(prim);
        model.skins.resize(1);
        model.skins[0].joints={1,2};
        model.skins[0].inverseBind={render::mat4{},render::mat4{}};
        model.vertices.resize(3);
        model.indices={0,1,2};
        model.vertices[0].pos={1,0,0};
        model.vertices[1].pos={0,1,0};
        model.vertices[2].pos={0,0,1};
        for(auto& v:model.vertices) {
            v.joints[0]=0;v.weights[0]=128;
            v.joints[1]=1;v.weights[1]=127;
        }
        const std::array<std::uint32_t,3> ids{{0,1,2}};
        const auto skin=ScalpSkinning::compile(model,0,0,ids);
        std::array<render::mat4,3> pose{};
        pose[1]=render::mat4::translate({2,0,0});
        pose[2]=render::mat4::translate({0,3,0});
        std::array<render::float3,3> output{};
        expect(skin.evaluate(pose,render::mat4{},output),
               "multi-joint scalp skinning evaluation failed");
        expect(std::fabs(output[0].x-(1.0f+2.0f*128.0f/255.0f))<0.0001f &&
               std::fabs(output[0].y-3.0f*127.0f/255.0f)<0.0001f,
               "scalp skinning did not normalize indexed joint weights");
        const auto first=output[0];
        model.vertices[0].joints[0]=250;
        bool rejectedBadPalette=false;
        try { (void)ScalpSkinning::compile(model,0,0,ids); }
        catch(const std::invalid_argument&) { rejectedBadPalette=true; }
        expect(rejectedBadPalette,"invalid scalp joint palette slot accepted");
        model.vertices[0].joints[0]=0;
        pose[1].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        expect(!skin.evaluate(pose,render::mat4{},output) &&
               render::length(output[0]-first)<0.0001f,
               "bad MotionWeave pose modified last-good scalp vertices");
    }
    // 7AE isolated Creator capture variants: one DNA scalar per case.
    {
        const auto neutral=HumanDNA::preset("neutral");
        struct Case {const char* id;float scatter,ior,rough;};
        for(const auto& scenario:std::array<Case,7>{{
            {"reference",neutral.skin.scatteringMm,
                         neutral.eyes.corneaIor,neutral.groom.roughness},
            {"skin-scatter-low",0.10f,neutral.eyes.corneaIor,neutral.groom.roughness},
            {"skin-scatter-high",4.0f,neutral.eyes.corneaIor,neutral.groom.roughness},
            {"eye-ior-low",neutral.skin.scatteringMm,1.25f,neutral.groom.roughness},
            {"eye-ior-high",neutral.skin.scatteringMm,1.45f,neutral.groom.roughness},
            {"hair-rough-low",neutral.skin.scatteringMm,neutral.eyes.corneaIor,0.08f},
            {"hair-rough-high",neutral.skin.scatteringMm,neutral.eyes.corneaIor,0.90f}
        }}) {
            auto candidate=neutral;
            expect(OpticalCaptureCases::apply(candidate,scenario.id),
                   "Creator optical capture case is not recognized");
            candidate.validate();
            expect(candidate.skin.scatteringMm==scenario.scatter &&
                   candidate.eyes.corneaIor==scenario.ior &&
                   candidate.groom.roughness==scenario.rough,
                   "optical case did not set the specified scalar");
            expect(candidate.skin.melanin==neutral.skin.melanin &&
                   candidate.eyes.pupil==neutral.eyes.pupil &&
                   candidate.groom.guideCount==neutral.groom.guideCount &&
                   candidate.groom.color==neutral.groom.color &&
                   candidate.anatomy.headScale==neutral.anatomy.headScale &&
                   candidate.expressionRest==neutral.expressionRest,
                   "optical case unintentionally changed unrelated DNA");
        }
        auto rejected=neutral;
        expect(!OpticalCaptureCases::apply(rejected,"unknown") &&
               rejected.skin.scatteringMm==neutral.skin.scatteringMm &&
               rejected.eyes.corneaIor==neutral.eyes.corneaIor &&
               rejected.groom.roughness==neutral.groom.roughness,
               "unknown optical case must fail without changing DNA");
    }
    // 7AC contract: shader-model optical lobes must exist in both
    // backends. Source checks complement (do not replace) GPU image tests.
    {
        const std::string glsl=render::shaders::kModelFS;
        const std::string msl=render::shaders::kModelMSL;
        for(const auto& shader:{glsl,msl}) {
            expect(shader.find("coatFresnel")!=std::string::npos &&
                   shader.find("primaryLobe")!=std::string::npos &&
                   shader.find("secondaryLobe")!=std::string::npos,
                   "VIVID corneal Fresnel or secondary hair BRDF missing");
            expect(shader.find("primaryLobe * 0.72 + secondaryLobe * 0.18")
                       !=std::string::npos,
                   "VIVID hair lobe weights differ from shared contract");
        }
        expect(glsl.find("m_uv1.z")!=std::string::npos &&
               msl.find("m.uv1.z")!=std::string::npos,
               "VIVID packed material profile not consumed consistently");
    }
    // Stage 7AB material-graph variation must reach both shader backends.
    {
        auto neutral=HumanDNA::preset("neutral");
        auto skinLow=neutral,skinHigh=neutral;
        skinLow.skin.scatteringMm=0.2f;
        skinHigh.skin.scatteringMm=3.0f;
        const auto shallowA=humanMaterial(HumanSurface::Skin,skinLow);
        const auto shallowB=humanMaterial(HumanSurface::Skin,skinHigh);
        expect(shallowA.shaderGraph && shallowB.shaderGraph &&
               shallowA.shaderGraph->glsl!=shallowB.shaderGraph->glsl &&
               shallowA.shaderGraph->msl!=shallowB.shaderGraph->msl,
               "skin scatter depth must change GLSL and MSL optical graphs");
        auto irisWide=neutral,irisNarrow=neutral;
        irisWide.eyes.irisRadius=0.75f; // upper bound of the public DNA schema
        irisNarrow.eyes.irisRadius=0.3f;
        const auto irisA=humanMaterial(HumanSurface::Iris,irisWide);
        const auto irisB=humanMaterial(HumanSurface::Iris,irisNarrow);
        expect(irisA.shaderGraph && irisB.shaderGraph &&
               irisA.shaderGraph->glsl!=irisB.shaderGraph->glsl &&
               irisA.shaderGraph->msl!=irisB.shaderGraph->msl,
               "iris pupil/limbal size did not reach paired optical shaders");
        auto hairDark=neutral,hairLight=neutral;
        hairDark.groom.melanin=0.95f;
        hairLight.groom.melanin=0.05f;
        const auto hairA=humanMaterial(HumanSurface::Hair,hairDark);
        const auto hairB=humanMaterial(HumanSurface::Hair,hairLight);
        expect(hairA.shaderGraph && hairB.shaderGraph &&
               hairA.shaderGraph->glsl!=hairB.shaderGraph->glsl &&
               hairA.shaderGraph->msl!=hairB.shaderGraph->msl,
               "hair melanin did not change GLSL/MSL optical graph");
    }
    auto eyeHealthy=HumanDNA::preset("neutral");
    auto eyeRed=eyeHealthy;
    eyeHealthy.eyes.scleraRedness=0.0f;
    eyeRed.eyes.scleraRedness=1.0f;
    const auto scleraA=humanMaterial(HumanSurface::Sclera,eyeHealthy);
    const auto scleraB=humanMaterial(HumanSurface::Sclera,eyeRed);
    expect(scleraB.baseColor.y<scleraA.baseColor.y &&
           scleraB.baseColor.z<scleraA.baseColor.z,
           "DNA sclera redness did not change real PBR surface color");
    auto originalDNA=HumanDNA{};
    auto sculptedDNA=originalDNA;
    sculptedDNA.skin.wrinkleStrength=0.95f;
    sculptedDNA.eyes.corneaIor=1.43f;
    sculptedDNA.groom.melanin=0.04f;
    const auto skinA=humanMaterial(HumanSurface::Skin,originalDNA);
    const auto skinB=humanMaterial(HumanSurface::Skin,sculptedDNA);
    const auto hairA=humanMaterial(HumanSurface::Hair,originalDNA);
    const auto hairB=humanMaterial(HumanSurface::Hair,sculptedDNA);
    const auto eye=humanMaterial(HumanSurface::Cornea,sculptedDNA);
    expect(skinA.shaderGraph->glsl!=skinB.shaderGraph->glsl &&
           hairA.shaderGraph->msl!=hairB.shaderGraph->msl &&
           eye.humanProfile.x==1.43f &&
           std::string(render::shaders::kModelMSL).find("cornealIor")!=std::string::npos,
           "DNA wrinkles/melanin/cornea-IOR did not reach real shader programs");
    auto sparseHair=originalDNA;
    sparseHair.groom.density=0.15f;
    auto denseHair=originalDNA;
    denseHair.groom.density=1.0f;
    HumanGroomRuntime lowDensity,highDensity;
    lowDensity.generate(sparseHair);
    highDensity.generate(denseHair);
    expect(lowDensity.guides().size()<highDensity.guides().size(),
           "DNA hair density slider does not alter generated strands");
    auto sculptedBust=originalDNA;
    sculptedBust.anatomy.shoulderWidth=0.85f;
    sculptedBust.anatomy.heightMeters=2.1f;
    auto bust=generateHumanPreview(sculptedBust,GroomQuality::Low);
    expect(bust.head->findNode("torso")>=0 &&
           bust.head->findNode("neck")>=0 &&
           bust.head->findNode("shoulder.L")>=0 &&
           bust.head->nodes[std::size_t(bust.head->findNode("torso"))].parent==0,
           "DNA body reference did not generate independent upper torso/neck");

    auto high=generateHumanPreview(HumanDNA{},GroomQuality::High);
    auto low=generateHumanPreview(HumanDNA{},GroomQuality::Low);
    expect(low.groom.triangleCount()<high.groom.triangleCount(),
           "hair LOD did not reduce geometry");
    auto weights=std::vector<float>(high.facialShapes.targets(),0.0f);
    FaceController controller(0);
    expect(controller.set(Layer::Emotion,Channel::MouthSmileLeft,0.8f),
           "facial controller setup failed");
    controller.snap();
    expect(high.facialBinding.evaluate(controller.frame(),weights),
           "human expression mapping failed");
    expect(weights[3]>0.7f,"human smile not mapped to corrective shape");
}
}
int main(){try{test();std::cout<<"VIVID human appearance/groom PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID appearance FAIL: "<<e.what()<<"\n";return 1;}return 0;}
