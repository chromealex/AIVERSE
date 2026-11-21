#include "HumanPreview.h"
#include "VividPresentationSettings.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::vivid {
namespace {
using namespace render;
struct Builder {
    std::vector<std::unique_ptr<MeshBuilder>> geometries;
    std::vector<ModelPart> parts;

    void sphere(const std::string& name,float3 position,float3 scale,
                const Material& material,int rings=16,int slices=24) {
        auto mesh=std::make_unique<MeshBuilder>();
        mesh->sphere({0,0,0},1.0f,rings,slices);
        parts.push_back({name,mesh.get(),material,
            mat4::translate(position)*mat4::scale(scale)});
        geometries.push_back(std::move(mesh));
    }
    void iris(const std::string& name,float3 centre,float radius,
              const Material& material) {
        auto mesh=std::make_unique<MeshBuilder>();
        mesh->addVertex({0,0,0},{0,0,1},0xffffffffu);
        constexpr int sides=64;
        for(int i=0;i<sides;++i) {
            const float a=float(i)*6.283185307179586f/float(sides);
            mesh->addVertex({radius*std::cos(a),radius*std::sin(a),0},
                            {0,0,1},0xffffffffu);
        }
        for(int i=0;i<sides;++i)
            mesh->addTriangle(0,std::uint32_t(i+1),
                              std::uint32_t((i+1)%sides+1));
        parts.push_back({name,mesh.get(),material,mat4::translate(centre)});
        geometries.push_back(std::move(mesh));
    }
};
}
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,GroomQuality quality,
                                       HumanMaterialQuality materialQuality) {
    dna.validate();
    HumanPreviewAsset result;
    const auto mat=humanMaterials(dna,materialQuality);
    const auto& a=dna.anatomy;
    const float hs=a.headScale;
    const float eyeSpacing=0.27f+0.16f*a.eyeSpacing;
    const float eyeRadius=0.145f*a.eyeScale*hs;
    const float eyeY=(0.12f+0.08f*a.browHeight)*hs;
    const float eyeZ=0.75f*hs;
    const float jawY=(-0.55f-0.08f*(a.jawLength-1.0f))*hs;
    Builder b;

    const float bodyHeight=a.heightMeters/1.74f;
    // Reference bust under the head: anatomical controls now influence
    // neck and shoulders without coupling them to jaw/head animation.
    b.sphere("neck",{0,-1.04f*hs,0.0f},
        {0.21f*hs,0.35f*hs,0.20f*hs},mat.skin,16,24);
    b.sphere("shoulder.L",{-0.68f*a.shoulderWidth*hs,-1.42f*hs,0},
        {0.35f*hs,0.27f*hs,0.35f*hs},mat.skin,16,24);
    b.sphere("shoulder.R",{0.68f*a.shoulderWidth*hs,-1.42f*hs,0},
        {0.35f*hs,0.27f*hs,0.35f*hs},mat.skin,16,24);
    b.sphere("torso",{0,-1.84f*hs,0},
        {0.76f*a.shoulderWidth*hs,
         0.75f*bodyHeight*hs,0.42f*(0.70f+0.60f*a.bodyBuild)*hs},
        mat.skin,20,32);
    b.sphere("head",{0,0,0},
        {0.72f*a.headWidth*hs,0.9f*hs,0.75f*hs},mat.skin,32,48);
    b.sphere("jaw",{0,jawY,0.25f*hs},
        {0.50f*a.jawWidth*hs,0.27f*hs,0.49f*hs},mat.skin);
    b.sphere("cheek.L",{-0.43f*a.headWidth*hs,-0.10f*hs,0.50f*hs},
        {0.22f+0.13f*a.cheekFullness,0.27f,0.23f},mat.skin);
    b.sphere("cheek.R",{0.43f*a.headWidth*hs,-0.10f*hs,0.50f*hs},
        {0.22f+0.13f*a.cheekFullness,0.27f,0.23f},mat.skin);
    b.sphere("nose",{0,0.005f*hs,0.78f*hs},
        {(0.1f+0.10f*a.noseWidth)*hs,(0.16f+0.10f*a.noseLength)*hs,
         (0.14f+0.16f*a.noseLength)*hs},mat.skin);
    b.sphere("ear.L",{-0.71f*a.headWidth*hs,-0.05f*hs,-0.05f*hs},
        {0.15f*a.earScale*hs,0.30f*a.earScale*hs,0.10f*hs},mat.skin);
    b.sphere("ear.R",{0.71f*a.headWidth*hs,-0.05f*hs,-0.05f*hs},
        {0.15f*a.earScale*hs,0.30f*a.earScale*hs,0.10f*hs},mat.skin);
    b.sphere("upperLip",{0,-0.37f*hs,0.77f*hs},
        {(0.27f+0.11f*a.mouthWidth)*hs,
         (0.035f+0.02f*a.lipFullness)*hs,0.045f*hs},mat.skin);
    b.sphere("lowerLip",{0,-0.43f*hs,0.76f*hs},
        {(0.27f+0.11f*a.mouthWidth)*hs,
         (0.04f+0.035f*a.lipFullness)*hs,0.045f*hs},mat.skin);
    b.sphere("teeth",{0,-0.41f*hs,0.745f*hs},
        {0.26f*hs,0.055f*hs,0.04f*hs},mat.teeth);

    const float irisRadius=eyeRadius*0.61f;
    for(int side=0;side<2;++side) {
        const bool left=side==0;
        const char* suffix=left?".L":".R";
        const float x=(left?-1.0f:1.0f)*eyeSpacing*hs;
        const float3 centre{x,eyeY,eyeZ};
        b.sphere(std::string("eye")+suffix,centre,
                 {eyeRadius,eyeRadius,eyeRadius},mat.sclera,24,32);
        b.iris(std::string("iris")+suffix,
               // Keep the pupil in front of the opaque sclera, inside
               // the transparent corneal shell; 0.97 buried its centre.
               {x,eyeY,eyeZ+eyeRadius*1.005f},
               irisRadius,mat.iris);
        b.sphere(std::string("cornea")+suffix,
                 {x,eyeY,eyeZ+eyeRadius*0.03f},
                 {eyeRadius*1.018f,eyeRadius*1.018f,eyeRadius*1.018f},
                 mat.cornea,24,32);
        b.sphere(std::string("lid")+suffix,
                 {x,eyeY+eyeRadius*0.86f,eyeZ+eyeRadius*0.13f},
                 {eyeRadius*1.08f,eyeRadius*0.19f,eyeRadius*0.18f},
                 mat.skin,12,20);
        b.sphere(std::string("brow")+suffix,
                 {x,eyeY+eyeRadius*1.47f,eyeZ+eyeRadius*0.11f},
                 {eyeRadius*1.17f,eyeRadius*0.19f,eyeRadius*0.20f},
                 mat.hair,12,20);
    }
    result.head=modelFromParts("vivid-human-"+dna.id,b.parts);
    if(!result.head||result.head->vertices.empty())
        throw std::runtime_error("VIVID procedural human head generation failed");

    // Attach every surface to the head role; bone names and transform
    // hierarchy now match the immutable DNA rig aliases.
    const int headIndex=result.head->findNode("head");
    if(headIndex<0)throw std::runtime_error("VIVID head rig missing");
    // Builder parts initially share the root and already contain their
    // authored anatomy scale/position. Preserve those world transforms when
    // parenting facial parts to the scaled head node (pure T*S here).
    const auto headScale=result.head->nodes[std::size_t(headIndex)].s;
    const auto headTranslation=result.head->nodes[std::size_t(headIndex)].t;
    for(std::size_t i=1;i<result.head->nodes.size();++i) {
        if(int(i)==headIndex)continue;
        const auto& name=result.head->nodes[i].name;
        const bool bodyRoot=name=="neck" || name=="shoulder.L" ||
                            name=="shoulder.R" || name=="torso";
        result.head->nodes[i].parent=bodyRoot ? 0 : headIndex;
        if(!bodyRoot) {
            auto& node=result.head->nodes[i];
            const auto offset=node.t-headTranslation;
            node.t={offset.x/headScale.x,offset.y/headScale.y,offset.z/headScale.z};
            node.s={node.s.x/headScale.x,node.s.y/headScale.y,node.s.z/headScale.z};
        }
    }
    // shader graph for iris uses radial 0..1 UV, not the generic planar UV
    // generated by modelFromParts for ordinary procedural primitives.
    for(const auto& mesh:result.head->meshes) {
        if(mesh.name!="iris.L"&&mesh.name!="iris.R")continue;
        const auto& p=mesh.primitives[0];
        for(std::uint32_t i=0;i<p.vertexCount;++i) {
            auto& v=result.head->vertices[std::size_t(p.firstVertex+i)];
            v.uv={0.5f+v.pos.x/(2.0f*irisRadius),
                  0.5f+v.pos.y/(2.0f*irisRadius)};
        }
    }
    result.head->finalize();
    result.rig=HumanRigBinding::compile(*result.head,dna.rig);
    // Important: the shared model upload policy may generate LOD meshes,
    // tangents and material/shadow batch vertices. Generate correctives
    // only AFTER those immutable model-relative vertex IDs are finalized.
    render::prepareModelUpload(*result.head);

    // Source-independent sparse corrective morph targets, intentionally
    // separate from the renderer's legacy eight-target shader limit.
    std::vector<MorphTargetInput> targets;
    for(auto label:{"jawOpen","eyeBlinkLeft","eyeBlinkRight",
                    "mouthSmileLeft","mouthSmileRight"}) {
        MorphTargetInput m;
        m.name=label;
        for(const auto& mesh:result.head->meshes) {
            if(mesh.primitives.empty())continue;
            const auto& p=mesh.primitives[0];
            for(std::uint32_t k=0;k<p.vertexCount;++k) {
                const auto index=p.firstVertex+k;
                const auto& v=result.head->vertices[std::size_t(index)];
                MorphDelta d;d.vertex=index;
                if(m.name=="jawOpen"&&
                   (mesh.name=="jaw"||mesh.name=="lowerLip"||mesh.name=="teeth"))
                    d.position={0.0f,-0.12f*hs,0.035f*hs};
                else if(m.name=="eyeBlinkLeft"&&mesh.name=="lid.L")
                    d.position={0.0f,-0.12f*hs,0.03f*hs};
                else if(m.name=="eyeBlinkRight"&&mesh.name=="lid.R")
                    d.position={0.0f,-0.12f*hs,0.03f*hs};
                else if(m.name=="mouthSmileLeft"&&mesh.name=="upperLip"&&v.pos.x<0)
                    d.position={-0.05f*hs,0.045f*hs,0};
                else if(m.name=="mouthSmileRight"&&mesh.name=="upperLip"&&v.pos.x>0)
                    d.position={0.05f*hs,0.045f*hs,0};
                else continue;
                m.deltas.push_back(d);
            }
        }
        targets.push_back(std::move(m));
    }
    result.facialShapes=SparseMorphLibrary::compile(
        result.head->vertices.size(),std::move(targets));
    const std::vector<ShapeRoute> routes{
        {Channel::JawOpen,"jawOpen",1.0f},
        {Channel::EyeBlinkLeft,"eyeBlinkLeft",1.0f},
        {Channel::EyeBlinkRight,"eyeBlinkRight",1.0f},
        {Channel::MouthSmileLeft,"mouthSmileLeft",1.0f},
        {Channel::MouthSmileRight,"mouthSmileRight",1.0f}};
    result.facialBinding=FaceShapeBinding::compile(result.facialShapes,routes);
    result.groom.generate(dna,quality);
    result.hair=result.groom.makeCardModel(dna,materialQuality);
    if(!result.hair)throw std::runtime_error("VIVID hair model generation failed");
    return result;
}
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,
                                       GroomQuality quality) {
    return generateHumanPreview(dna,quality,HumanMaterialQuality::High);
}
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,
                                       const VividPresentationSettings& settings) {
    if(!settings.valid())
        throw std::invalid_argument("VIVID invalid project presentation settings");
    return generateHumanPreview(dna,settings.groomQuality,settings.materialQuality);
}
} // namespace aiverse::vivid
