#include "apps/studio-native/StudioModelPicking.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::studio;

static Model oneTriangle() {
    Model model;
    model.nodes.resize(1);
    model.nodes[0].mesh=0;
    model.bindWorld.push_back(mat4::identity());
    model.meshes.resize(1);
    Primitive primitive;
    primitive.firstIndex=0;
    primitive.indexCount=3;
    model.meshes[0].primitives.push_back(primitive);
    model.vertices.resize(3);
    model.vertices[0].pos={0,0,0};
    model.vertices[1].pos={2,0,0};
    model.vertices[2].pos={0,2,0};
    model.indices={0,1,2};
    return model;
}
int main() {
    const float3 origin{.25f,.25f,5}, direction{0,0,-1};
    auto model=oneTriangle();
    const auto first=studioPickModelSurface(model,mat4::identity(),origin,direction);
    AIVERSE_TEST_CHECK(first);
    AIVERSE_TEST_CHECK(std::abs(first.distance-5)<1e-4f);
    AIVERSE_TEST_CHECK(first.node==0 && first.mesh==0 && first.primitive==0 && first.triangle==0);
    // Within a triangle's AABB but outside actual face.
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),{1.8f,1.8f,5},direction));
    // Double sided triangle winding still hits.
    AIVERSE_TEST_CHECK(studioPickModelSurface(model,mat4::identity(),{.25f,.25f,-5},{0,0,1}));
    // Both world and node transforms must be included.
    model.bindWorld[0]=mat4::translate({1,0,0});
    const auto shifted=studioPickModelSurface(model,mat4::translate({0,0,-3}),
                                             {1.25f,.25f,5},direction);
    AIVERSE_TEST_CHECK(shifted && std::abs(shifted.distance-8)<1e-4f);
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    // Missing CPU geometry must NOT fall back to a false-positive AABB.
    model.vertices.clear();
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),{1.25f,.25f,5},direction));
    // Non-rigid/skinned or morphed meshes are not safe for bind-pose picking.
    model=oneTriangle();
    model.nodes[0].skin=0;
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    model.nodes[0].skin=-1;
    model.meshes[0].primitives[0].morphTargets=1;
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    // Corrupt indices must be ignored without out-of-bounds access.
    model=oneTriangle();
    model.indices[2]=UINT32_MAX;
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    model=oneTriangle();
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,{0,0,0}));
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),origin,direction,0));
    const float inf=std::numeric_limits<float>::infinity();
    AIVERSE_TEST_CHECK(!studioPickModelSurface(model,mat4::identity(),{inf,0,0},direction));
    // The closer model wins, even when instance ordering is reversed.
    model=oneTriangle();
    auto front=oneTriangle(), back=oneTriangle();
    const auto nearest=studioPickModelInstances({
        {"back",&back,mat4::translate({0,0,-5})},
        {"front",&front,mat4::translate({0,0,-1})}
    },origin,direction);
    AIVERSE_TEST_CHECK(nearest.id=="front");
    AIVERSE_TEST_CHECK(std::abs(nearest.surface.distance-6)<1e-4f);
    AIVERSE_TEST_CHECK(studioPickModelInstances({
        {"hidden",nullptr,mat4::identity()}
    },origin,direction).id.empty());
    StudioDocumentProxy fallback;
    fallback.id="cube";
    fallback.center={.25f,.25f,2};
    fallback.half={.5f,.5f,.5f};
    fallback.calculateBounds();
    StudioDocumentProxy hiddenProxy=fallback;
    hiddenProxy.id="front";
    hiddenProxy.center.z=4;
    hiddenProxy.calculateBounds();
    const std::vector<StudioModelPickInstance> instances{
        {"front",&front,mat4::translate({0,0,-1})}
    };
    // The frontmost visible fallback cube wins over a farther model triangle.
    AIVERSE_TEST_CHECK(studioPickSceneSurfaces(instances,{fallback,hiddenProxy},origin,direction)=="cube");
    // A proxy replaced by a real rendered model must NOT steal the selection.
    AIVERSE_TEST_CHECK(studioPickSceneSurfaces(instances,{hiddenProxy},origin,direction)=="front");
    // Transparent card pixels must not intercept a visual mesh click.
    auto masked=oneTriangle();masked.materials.resize(1);auto& material=masked.materials[0];
    masked.meshes[0].primitives[0].material=0;
    material.alpha=AlphaMode::Mask;material.alphaCutoff=.5f;material.baseTex=0;
    masked.vertices[0].uv={0,0};masked.vertices[1].uv={1,0};masked.vertices[2].uv={0,1};
    Image image;image.width=image.height=2;image.repeat=false;image.nearest=true;
    image.rgba={255,255,255,0,255,255,255,255,255,255,255,0,255,255,255,255};masked.images.push_back(image);
    AIVERSE_TEST_CHECK(!studioPickModelSurface(masked,mat4::identity(),{.25f,.25f,5},direction));
    AIVERSE_TEST_CHECK(studioPickModelSurface(masked,mat4::identity(),{1.5f,.25f,5},direction));
    AIVERSE_TEST_CHECK(!studioPickModelSurface(masked,mat4::identity(),{1.5f,.25f,-5},{0,0,1}));
    material.doubleSided=true;
    AIVERSE_TEST_CHECK(studioPickModelSurface(masked,mat4::identity(),{1.5f,.25f,-5},{0,0,1}));
    masked.images[0].nearest=false;
    AIVERSE_TEST_CHECK(!studioPickModelSurface(masked,mat4::identity(),{.9f,.25f,5},direction));
    AIVERSE_TEST_CHECK(studioPickModelSurface(masked,mat4::identity(),{1.1f,.25f,5},direction));
    material.uvOffset.x=.5f;
    AIVERSE_TEST_CHECK(studioPickModelSurface(masked,mat4::identity(),{.25f,.25f,5},direction));
    masked.images[0].rgba.clear();
    AIVERSE_TEST_CHECK(!studioPickModelSurface(masked,mat4::identity(),{.25f,.25f,5},direction));
    std::cout<<"PASS studio CPU model triangle picking\n";
}
