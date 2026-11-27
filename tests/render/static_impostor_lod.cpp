// Engine regression: StaticImpostorLod appends a two-card far LOD with a
// baked silhouette to a single-node static model. Project-independent.
#include "engine/render/StaticImpostorLod.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "impostor check failed: %s at %d\n", #x, __LINE__); std::exit(1); } } while (0)
using namespace aiverse::render;

static Model makeBox(bool withLod) {
    Model m;
    m.path = "box";
    Material mat; mat.baseColor = {0.2f, 0.6f, 0.1f, 1.f};
    m.materials.push_back(mat);
    const float3 c[8] = {{-1,0,-.5f},{1,0,-.5f},{1,4,-.5f},{-1,4,-.5f},{-1,0,.5f},{1,0,.5f},{1,4,.5f},{-1,4,.5f}};
    for (auto p : c) { ModelVertex v{}; v.pos = p; v.nrm = {0,1,0}; m.vertices.push_back(v); }
    const uint32_t q[36] = {0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 3,2,6,3,6,7, 0,3,7,0,7,4, 1,5,6,1,6,2};
    for (uint32_t i : q) m.indices.push_back(i);
    MeshDef mesh; mesh.name = "box";
    Primitive p; p.firstIndex = 0; p.indexCount = 36; p.material = 0;
    mesh.primitives.push_back(p);
    m.meshes.push_back(mesh);
    if (withLod) {
        MeshDef coarse = mesh; coarse.name = "box_lod1"; coarse.lods.clear();
        m.meshes.push_back(coarse);
        m.meshes[0].lods.push_back(ModelLod{1, 64.f});
    }
    Node n; n.name = "box"; n.mesh = 0;
    m.nodes.push_back(n);
    m.finalize();
    return m;
}

int main() {
    {
        Model m = makeBox(true);
        const size_t meshes = m.meshes.size(), mats = m.materials.size(), imgs = m.images.size();
        std::string why;
        CHECK(appendStaticImpostorLod(m, {30.f, 128}, &why));
        CHECK(m.meshes.size() == meshes + 1);
        CHECK(m.materials.size() == mats + 1);
        CHECK(m.images.size() == imgs + 1);
        const auto& lods = m.meshes[0].lods;
        CHECK(lods.size() == 2);
        CHECK(lods.back().mesh == int(meshes));
        CHECK(lods.back().maxPixels == 30.f);   // below previous level (64*0.75=48)
        const MeshDef& cards = m.meshes[size_t(lods.back().mesh)];
        uint32_t tris = 0; for (const auto& p : cards.primitives) tris += p.indexCount / 3;
        CHECK(tris == 4);
        const Material& card = m.materials.back();
        CHECK(card.alpha == AlphaMode::Mask);
        CHECK(card.doubleSided);
        // The silhouette must cover the card area.
        const Image& atlas = m.images.back();
        size_t opaque = 0, total = size_t(atlas.width) * size_t(atlas.height);
        for (size_t i = 0; i < total; ++i) if (atlas.rgba[i * 4 + 3] > 127) ++opaque;
        CHECK(opaque > total / 4);
    }
    {
        Model m = makeBox(false);
        std::string why;
        CHECK(appendStaticImpostorLod(m, {20.f, 128}, &why));
        CHECK(m.meshes[0].lods.size() == 1 && m.meshes[0].lods[0].maxPixels == 20.f);
    }
    {
        Model m = makeBox(true);
        Node extra; extra.name = "second"; extra.mesh = 0; m.nodes.push_back(extra);
        std::string why;
        CHECK(!appendStaticImpostorLod(m, {}, &why));
        CHECK(!why.empty());
        CHECK(m.meshes[0].lods.size() == 1);
    }
    {
        auto makeSparse = [] {
            Model m=makeBox(false);
            auto invisible=m.materials[0];invisible.alpha=AlphaMode::Mask;invisible.baseColor.w=0;
            m.materials.push_back(invisible);
            auto ghost=m.meshes[0].primitives.front();
            ghost.material=1;ghost.firstIndex=uint32_t(m.indices.size());
            ghost.firstVertex=uint32_t(m.vertices.size());ghost.vertexCount=8;
            for (uint32_t v=0;v<8;++v) {
                auto p=m.vertices[v];p.pos=p.pos+float3{8,12,6};m.vertices.push_back(p);
            }
            for (uint32_t i=0;i<36;++i)m.indices.push_back(m.indices[i]+8);
            m.meshes[0].primitives.push_back(ghost);m.finalize();return m;
        };
        Model a=makeSparse(),b=makeSparse();
        CHECK(appendStaticImpostorLod(a,{20,128,ImpostorFit::Auto}));
        CHECK(appendStaticImpostorLod(b,{20,128,ImpostorFit::Full}));
        CHECK(a.images.back().rgba==b.images.back().rgba);
        CHECK(a.meshes.back().boundsMax.y < b.meshes.back().boundsMax.y*0.5f);
        CHECK(a.indices==b.indices);
    }
    std::printf("static impostor lod: ok\n");
    return 0;
}
