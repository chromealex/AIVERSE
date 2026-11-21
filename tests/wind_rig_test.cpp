#include <algorithm>
#include "engine/render/ModelLodTransition.h"
// Hierarchical wind masks: library reuse, rig packing, reference evaluation,
// model binding, node-tree rigs, sidecars and TerraWeave Arbor trees.
#include "engine/render/TerraWeaveArborWind.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <cstdio>
#include <set>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::wind;
namespace {
int failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("WIND RIG FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failed; } } while (0)

struct Packed {
    std::vector<uint8_t> rig, atlas; int rw = 0, rh = 0, aw = 0, ah = 0;
    Packed(const WindRig& r, const WindMaskLibrary& lib) { packWindRig(r, rig, rw, rh); lib.packAtlas(atlas, aw, ah); }
    V3 eval(V3 p, uint16_t node, WindDrawParams w, float flutter = 0, V3 n = {0, 0, 1}) const {
        return PackedWindRig(rig, rw, atlas, aw).offset(p, n, encodeWindVertex(node, flutter, 0), w);
    }
};

std::vector<WindNodeDesc> tree() {
    std::vector<WindNodeDesc> d(4);
    d[0].pivot = {0, 0, 0}; d[0].tip = {0, 10, 0};                     // trunk
    d[1].parent = 0; d[1].pivot = {0, 6, 0}; d[1].tip = {3, 8, 0};      // branch
    d[2].parent = 1; d[2].pivot = {3, 8, 0}; d[2].tip = {4, 9, 0};      // twig
    d[3].parent = 2; d[3].pivot = {4, 9, 0}; d[3].tip = {4.3f, 9.2f, 0}; // leaf
    return d;
}

void masks() {
    WindMaskLibrary lib;
    CHECK(lib.find("rigid") == uint8_t(0));
    CHECK(lib.find("trunk") && lib.find("branch") && lib.find("leaf"));
    const size_t before = lib.size();
    // Same curve under a new name reuses the row (max reuse).
    CHECK(lib.addCurve("pine_trunk", {{0, 0}, {0.25f, 0.03f}, {0.6f, 0.3f}, {1, 1}}) == lib.require("trunk"));
    CHECK(lib.size() == before);
    const auto flag = lib.addCurve("flag", {{0, 0}, {0.2f, 0.6f}, {1, 1}});
    CHECK(lib.size() == before + 1 && flag == before);
    bool threw = false;
    try { lib.addCurve("flag", {{0, 1}, {1, 1}}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    lib.loadJson(Json::parse(R"({"masks":{"stiff_base":[[0,0],[0.5,0],[1,0.8]],"also_linear":[[0,0],[1,1]]}})"));
    CHECK(lib.require("also_linear") == lib.require("linear"));
    CHECK(sampleMask(lib.mask(lib.require("trunk")), 0) == 0.f);
    CHECK(std::abs(sampleMask(lib.mask(lib.require("trunk")), 1) - 1.f) < 1e-6f);
    CHECK(sampleMask(lib.mask(lib.require("stiff_base")), 0.4f) == 0.f);
    std::vector<uint8_t> atlas; int w = 0, h = 0;
    lib.packAtlas(atlas, w, h);
    CHECK(w == kTextureWidth && atlas.size() == size_t(w * h * 4));
    CHECK(size_t(w * h) >= lib.size() * kMaskTexels);
    const auto& tr = lib.mask(lib.require("trunk"));
    for (int s = 0; s < kMaskSamples; ++s)
        CHECK(atlas[size_t(lib.require("trunk")) * kMaskTexels * 4 + size_t(s)] == tr[size_t(s)]);
}

void hierarchy() {
    WindMaskLibrary lib;
    WindProfile prof;
    const auto rig = buildWindRig(tree(), prof, lib, 7);
    CHECK(rig.nodes.size() == 4);
    CHECK(rig.nodes[3].depth == 3 && rig.nodes[1].parent == 0);
    CHECK(rig.nodes[0].mask == lib.require("trunk") && rig.nodes[3].mask == lib.require("leaf"));
    Packed pk(rig, lib);
    // 16 bytes per node + 4 header, rounded up to whole 64-texel rows.
    CHECK(pk.rw == 64 && pk.rh == 1);
    const WindDrawParams wind{0.6f, 0, 1.0f, 0.3f}, calm{};
    // Calm air: nothing moves.
    const auto z = pk.eval({4, 9, 0}, 3, calm);
    CHECK(length(z) < 1e-6f);
    // Trunk base is anchored; trunk top sways downwind (+X).
    CHECK(length(pk.eval({0, 0, 0}, 0, wind)) < 1e-3f);
    const auto top = pk.eval({0, 10, 0}, 0, wind);
    CHECK(top.x > 0.02f);                                  // always leans downwind
    float swing = 0;
    for (int i = 0; i < 64; ++i) swing = std::max(swing, pk.eval({0, 10, 0}, 0, {0.6f, 0, float(i) * 0.2f, 0.3f}).x);
    CHECK(swing > 0.15f);                                  // and sways over time
    // A branch vertex exactly at its attachment moves rigidly with the trunk.
    const auto trunkAt = pk.eval({0, 6, 0}, 0, wind);
    const auto branchAt = pk.eval({0, 6, 0}, 1, wind);
    CHECK(length(trunkAt - branchAt) < 1e-5f);
    // Branch tip adds its own bend on top of the trunk at the attachment.
    const auto branchTip = pk.eval({3, 8, 0}, 1, wind);
    CHECK(length(branchTip - trunkAt) > 1e-3f);
    // Deeper parts keep inheriting: twig at its pivot == branch at that point.
    CHECK(length(pk.eval({3, 8, 0}, 2, wind) - branchTip) < 1e-5f);
    // Length is (approximately) preserved: an arc, not a shear.
    {
        std::vector<WindNodeDesc> one(1);
        one[0].tip = {0, 4, 0}; one[0].mask = "linear"; one[0].flex = 2;
        const auto r1 = buildWindRig(one, prof, lib);
        Packed p1(r1, lib);
        const V3 tip{0, 4, 0};
        const auto off = p1.eval(tip, 0, {1.5f, 0, 0.4f, 0});
        CHECK(length(off) > 0.5f);
        CHECK(std::abs(length(tip + off) - 4.f) < 0.08f * length(off) + 0.02f);
    }
    // Rigid mask: the part only follows its parent.
    {
        auto d = tree(); d[1].mask = "rigid";
        Packed pr(buildWindRig(d, prof, lib, 7), lib);
        CHECK(length(pr.eval({3, 8, 0}, 1, wind) - pr.eval({0, 6, 0}, 0, wind)) < 1e-5f);
    }
    // Leaf flutter moves along the normal only when weighted.
    const auto still = pk.eval({4.3f, 9.2f, 0}, 3, wind, 0);
    const auto flutter = pk.eval({4.3f, 9.2f, 0}, 3, wind, 1, {0, 0, 1});
    CHECK(std::abs(flutter.x - still.x) < 1e-6f && std::abs(flutter.y - still.y) < 1e-6f);
    // Unbound vertex (kNoNode) stays still.
    CHECK(length(pk.eval({1, 1, 1}, kNoNode, wind)) < 1e-6f);
    // Packing round trip: quantization below one unit.
    PackedWindRig view(pk.rig, pk.rw, pk.atlas, pk.aw);
    (void)view;
    CHECK(rig.unitMetres() <= 10.f / 32000.f * 2.f);
    // Reuse: identical prototypes produce identical rig textures.
    Packed again(buildWindRig(tree(), prof, lib, 7), lib);
    CHECK(windTextureKey(again.rig) == windTextureKey(pk.rig));
    Packed reseeded(buildWindRig(tree(), prof, lib, 8), lib);
    CHECK(windTextureKey(reseeded.rig) != windTextureKey(pk.rig));   // only phases differ
    CHECK(windTextureKey(reseeded.atlas) == windTextureKey(pk.atlas)); // atlas shared
    // Validation.
    bool threw = false;
    try { auto d = tree(); d[1].parent = 2; (void)buildWindRig(d, prof, lib); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { auto d = tree(); d[0].mask = "nope"; (void)buildWindRig(d, prof, lib); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // Deep chains are bounded (4 levels walked), never crash.
    std::vector<WindNodeDesc> chain(9);
    for (int i = 0; i < 9; ++i) { chain[size_t(i)].parent = i - 1; chain[size_t(i)].pivot = {0, float(i), 0}; chain[size_t(i)].tip = {0, float(i + 1), 0}; }
    Packed pc(buildWindRig(chain, prof, lib), lib);
    CHECK(std::isfinite(pc.eval({0, 9, 0}, 8, wind).x));
    // Profiles from JSON.
    const auto jp = WindProfile::fromJson(Json::parse(R"({"levels":[{"mask":"uniform","flex":0.5},{"mask":"tip"}],"leafFlutter":2})"));
    CHECK(jp.levels.size() == 2 && jp.level(5).mask == "tip" && jp.leafFlutter == 2.f);
}

std::unique_ptr<Model> twoNodeModel() {
    auto m = std::make_unique<Model>();
    m->materials.resize(2);
    m->materials[0].name = "Bark"; m->materials[1].name = "Leaves";
    auto quad = [&](float3 a, float3 b, float3 c, float3 d, int material) {
        MeshDef mesh; Primitive p;
        p.material = material;
        p.firstVertex = uint32_t(m->vertices.size()); p.vertexCount = 4;
        p.firstIndex = uint32_t(m->indices.size()); p.indexCount = 6;
        for (auto v : {a, b, c, d}) { ModelVertex mv; mv.pos = v; mv.nrm = {0, 0, 1}; m->vertices.push_back(mv); }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m->indices.push_back(p.firstVertex + i);
        mesh.primitives.push_back(p);
        m->meshes.push_back(mesh);
    };
    quad({-0.2f, 0, 0}, {0.2f, 0, 0}, {0.2f, 8, 0}, {-0.2f, 8, 0}, 0);   // trunk, local
    quad({0, -0.1f, 0}, {3, -0.1f, 0}, {3, 0.1f, 0}, {0, 0.1f, 0}, 1);   // branch along +X, local
    Node trunk; trunk.name = "Trunk"; trunk.mesh = 0; trunk.children = {1};
    Node branch; branch.name = "Leaf_Branch"; branch.mesh = 1; branch.parent = 0; branch.t = {0, 5, 0};
    m->nodes = {trunk, branch};
    m->finalize();
    return m;
}

void modelBinding() {
    WindMaskLibrary lib;
    auto atlas = WindMaskAtlas::fromLibrary(lib);
    auto m = twoNodeModel();
    WindModelSpec spec;
    spec.rules.push_back({"leaf_*", "twig", std::nullopt, 1.5f, 1.f});
    WindBinding binding;
    const auto rig = rigFromModelNodes(*m, spec, lib, binding);
    CHECK(rig.nodes.size() == 2 && rig.nodes[1].parent == 0);
    CHECK(std::abs(rig.nodes[1].pivot.y - 5) < 1e-4f && rig.nodes[1].mask == lib.require("twig"));
    CHECK(std::abs(rig.nodes[1].flex - 1.5f) < 1e-6f);
    CHECK(std::abs(m->vertices[5].pos.y - 4.9f) < 1e-4f);           // baked into model space
    CHECK(m->nodes[1].t.y == 0 && std::abs(m->bindWorld[1].at(1, 3)) < 1e-6f);
    CHECK(binding.node[0] == 0 && binding.node[6] == 1);
    CHECK(binding.flutter[6] == 1.f && binding.flutter[0] == 0.f);
    const auto rep = bindWindRig(*m, rig, *atlas, binding, {}, &lib);
    CHECK(rep.materials == 2 && rep.vertices == 8 && rep.vertexColorMaterials == 2);
    CHECK(m->images.size() == 2 && m->images[0].nearest && !m->images[0].repeat);
    CHECK(m->images[1].sharedRgba == atlas->rgba());                // atlas bytes shared, not copied
    for (const auto& mat : m->materials) {
        CHECK(mat.shaderGraph == windRigGraph() && !mat.vertexColors);
        CHECK(mat.graphTextures[0] == 0 && mat.graphTextures[1] == 1);
    }
    CHECK((m->vertices[6].color & 0xffffu) == 1u && ((m->vertices[6].color >> 16) & 255u) == 255u);
    // Custom material graphs are never silently replaced.
    auto other = twoNodeModel();
    auto g = std::make_shared<graph::Program>(); g->outputs["color"] = "my_color";
    other->materials[0].shaderGraph = g;
    WindBinding b2; const auto r2 = rigFromModelNodes(*other, spec, lib, b2);
    bool threw = false;
    try { bindWindRig(*other, r2, *atlas, b2); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // A mask outside the atlas is rejected.
    WindMaskLibrary big; big.addCurve("x", {{0, 0.3f}, {1, 0.4f}});
    auto smallAtlas = WindMaskAtlas::fromLibrary(WindMaskLibrary{});
    std::vector<WindNodeDesc> d(1); d[0].tip = {0, 1, 0}; d[0].mask = "x";
    const auto r3 = buildWindRig(d, WindProfile{}, big);
    auto m3 = twoNodeModel(); WindBinding b3; b3.node.assign(m3->vertices.size(), 0);
    threw = false;
    try { bindWindRig(*m3, r3, *smallAtlas, b3); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void sidecar() {
    std::map<std::string, std::string> files{
        {"wind/masks.json", R"({"masks":{"palm_frond":[[0,0],[0.3,0.4],[1,1]]}})"},
        {"wind/profiles.json", R"({"profiles":{"palm":{"levels":[{"mask":"trunk","flex":0.3},{"mask":"palm_frond","flex":1.2}]}}})"},
        {"palm.glb.wind.json", R"({"profile":"palm","rules":[{"node":"Leaf*","flutter":1}],"flutterMaterials":["Leaves"]})"},
    };
    const FileReader read = [&](const std::string& p, std::string& out) {
        auto it = files.find(p); if (it == files.end()) return false; out = it->second; return true;
    };
    auto m = twoNodeModel();
    std::vector<std::string> warnings;
    CHECK(applyWindSidecar(*m, "palm.glb", read, &warnings));
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK(warnings.empty());
    CHECK(m->materials[1].shaderGraph == windRigGraph());
    const auto project = windProjectData(read);
    CHECK(project == windProjectData(read));                        // cached: one atlas per project
    CHECK(project->masks.find("palm_frond").has_value());
    // No sidecar: untouched.
    auto plain = twoNodeModel();
    CHECK(!applyWindSidecar(*plain, "rock.glb", read, nullptr));
    CHECK(!plain->materials[0].shaderGraph);
    // Explicit skeleton for a single-mesh model, nearest-segment binding.
    files["mast.glb.wind.json"] = R"({"skeleton":[{"name":"pole","pivot":[0,0,0],"tip":[0,8,0]},
        {"name":"arm","parent":"pole","pivot":[0,5,0],"tip":[3,5,0],"mask":"twig"}]})";
    auto mast = twoNodeModel();
    for (auto& n : mast->nodes) n.t = {};
    mast->nodes[1].t = {0, 5, 0}; mast->finalize();
    warnings.clear();
    CHECK(applyWindSidecar(*mast, "mast.glb", read, &warnings));
    CHECK(warnings.empty());
    CHECK((mast->vertices[0].color & 0xffffu) == 0u);
    // Bad sidecar: warning, model unchanged.
    files["bad.glb.wind.json"] = R"({"profile":"missing"})";
    auto bad = twoNodeModel(); warnings.clear();
    CHECK(!applyWindSidecar(*bad, "bad.glb", read, &warnings) && warnings.size() == 1);
    CHECK(!bad->materials[0].shaderGraph);
}

void arbor() {
    WindMaskLibrary lib;
    auto atlas = WindMaskAtlas::fromLibrary(lib);
    terraweave::TreeProfile profile;
    const uint64_t seed = 42;
    std::vector<int32_t> owners;
    auto model = terraweave::makeArborBranchLodModel(profile, seed, {}, &owners);
    CHECK(owners.size() == model->vertices.size());
    const auto skeleton = terraweave::generateTree(profile, seed, terraweave::TreeLOD::High);
    const auto rep = terraweave::applyArborWind(*model, skeleton, owners, WindProfile{}, lib, *atlas);
    CHECK(rep.nodes == skeleton.branches.size() && rep.vertices == model->vertices.size());
    // Leaves flutter, bark does not.
    bool leafFlutter = false, barkStill = true;
    for (const auto& mesh : model->meshes)
        for (const auto& p : mesh.primitives)
            for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount; ++i) {
                const uint32_t f = (model->vertices[i].color >> 16) & 255u;
                if (p.material == 1 && f == 255u) leafFlutter = true;
                if (p.material == 0 && f != 0u) barkStill = false;
            }
    CHECK(barkStill);
    CHECK(leafFlutter || skeleton.leaves.empty());
    // Single-mesh model: coarse tiers from another skeleton bind by distance.
    std::vector<int32_t> owners2;
    auto single = terraweave::makeArborModel(profile, seed, {}, &owners2);
    CHECK(owners2.size() == single->vertices.size());
    size_t known = 0; for (auto o : owners2) known += o >= 0;
    CHECK(known > 0);
    const auto rep2 = terraweave::applyArborWind(*single, skeleton, owners2, WindProfile{}, lib, *atlas);
    CHECK(rep2.vertices == single->vertices.size());
    // Trunk foot stays put, crown moves.
    std::vector<uint8_t> rigBytes; int rw = 0, rh = 0;
    packWindRig(terraweave::arborWindRig(skeleton, WindProfile{}, lib), rigBytes, rw, rh);
    std::vector<uint8_t> atlasBytes(atlas->rgba()->begin(), atlas->rgba()->end());
    PackedWindRig eval(rigBytes, rw, atlasBytes, atlas->width());
    const WindDrawParams w{0.5f, 0.2f, 2.f, 0.4f};
    float foot = 1e9f, crown = 0;
    for (const auto& v : model->vertices) {
        const auto off = eval.offset({v.pos.x, v.pos.y, v.pos.z}, {v.nrm.x, v.nrm.y, v.nrm.z}, v.color, w);
        if (v.pos.y < 0.3f) foot = std::min(foot, length(off));
        crown = std::max(crown, length(off));
    }
    CHECK(foot < 0.02f);
    CHECK(crown > 0.1f);
}

void shaderText() {
    const auto& g = windRigGraph();
    CHECK(g->textures.size() == 2 && g->outputs.at("vertexOffset") == "wr_offset");
    CHECK(g->vertexGlsl.find("%VEC") == std::string::npos && g->vertexMsl.find("%MAT") == std::string::npos);
    CHECK(g->vertexMsl.find("float3") != std::string::npos && g->vertexGlsl.find("vec3") != std::string::npos);
    CHECK(g->vertexGlsl.find("SG_TEXEL0") != std::string::npos && g->vertexGlsl.find("SG_TEXEL1") != std::string::npos);
    CHECK(windRigMaxOffsetMetres({0.5f, 0, 0, 0}) > 0.5f);
}

void decodeAndParams() {
    WindMaskLibrary lib;
    const auto rig = buildWindRig(tree(), WindProfile{}, lib, 3);
    std::vector<uint8_t> bytes; int w = 0, h = 0;
    packWindRig(rig, bytes, w, h);
    const auto back = decodeWindRig(bytes, w);
    CHECK(back.nodes.size() == rig.nodes.size());
    for (size_t i = 0; i < rig.nodes.size(); ++i) {
        const auto& a = rig.nodes[i]; const auto& b = back.nodes[i];
        CHECK(length(a.pivot - b.pivot) <= rig.unitMetres());
        CHECK(std::abs(a.length - b.length) <= rig.unitMetres());
        CHECK(dot(a.axis, b.axis) > 0.999f);
        CHECK(a.parent == b.parent && a.mask == b.mask && a.depth == b.depth);
        CHECK(std::abs(a.flex - b.flex) < 0.01f && std::abs(a.stiffness - b.stiffness) < 0.005f);
    }
    bool threw = false;
    try { auto bad = bytes; bad[3] = 7; (void)decodeWindRig(bad, w); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // Scene wind -> object-local parameters: yaw and uniform scale undone.
    SceneWind sw; sw.x = 10; sw.z = 0; sw.metresPerMps = 0.05f; sw.seconds = 2;
    const auto plain = windRigParamsForDraw(sw, mat4{});
    CHECK(std::abs(plain.windX - 0.5f) < 1e-5f && std::abs(plain.windZ) < 1e-5f);
    const auto yawed = windRigParamsForDraw(sw, mat4::rotateY(1.5707963f));
    // rotateY(90) maps local +Z to world +X, so world +X wind is local +Z (or -Z per handedness).
    CHECK(std::abs(std::abs(yawed.windZ) - 0.5f) < 1e-3f && std::abs(yawed.windX) < 1e-3f);
    const auto scaled = windRigParamsForDraw(sw, mat4::scale({2, 2, 2}));
    CHECK(std::abs(scaled.windX - 0.25f) < 1e-5f);
    CHECK(!SceneWind{}.enabled() && windRigParamsForDraw(SceneWind{}, mat4{}).windX == 0);
    // Debug programs share the vertex motion and textures.
    for (auto v : {WindDebugView::Parts, WindDebugView::Depth, WindDebugView::Weight}) {
        const auto g = windRigDebugGraph(v);
        CHECK(g && g->vertexGlsl == windRigGraph()->vertexGlsl && g->textures.size() == 2);
        CHECK(g->outputs.at("color") == "wd_color" && g->glsl.find("%VEC") == std::string::npos &&
              g->msl.find("%VEC") == std::string::npos && g->glsl.find("%MODE%") == std::string::npos);
        CHECK(g == windRigDebugGraph(v));
    }
    CHECK(!windRigDebugGraph(WindDebugView::Off) && !windRigDebugGraph(WindDebugView::Skeleton));
}

void inspection() {
    WindMaskLibrary lib;
    auto atlas = WindMaskAtlas::fromLibrary(lib);
    auto m = twoNodeModel();
    WindBinding b; WindModelSpec spec;
    const auto rig = rigFromModelNodes(*m, spec, lib, b);
    bindWindRig(*m, rig, *atlas, b);
    // Simulate an upload that released CPU pixels: the registry still serves.
    for (auto& img : m->images) img.sharedRgba.reset();
    const auto view = findWindRig(*m);
    CHECK(view && view->decoded.nodes.size() == 2);
    std::vector<WindDebugLine> calm, windy;
    windRigDebugLines(*view, mat4::translate({10, 0, 0}), {}, calm, 4);
    CHECK(calm.size() == 8);
    CHECK(std::abs(calm.front().a.x - 10.f) < 1e-3f);                 // world transform applied
    CHECK((calm.front().rgba >> 24) == 0xffu);
    windRigDebugLines(*view, mat4::translate({10, 0, 0}), {0.8f, 0, 1, 0}, windy, 4);
    CHECK(windy.size() == calm.size() && windy.back().b.x > calm.back().b.x);
    auto plain = twoNodeModel();
    CHECK(!findWindRig(*plain));
}

void forest() {
    using namespace terraweave;
    ArborForestAdapter forest;
    forest.enableHierarchicalWind();
    WindProfile soft; soft.levels[0].flex = 0.6f;
    TreeProfile profile; profile.speciesId = 7;
    forest.setSpeciesWindProfile(7, soft);
    forest.addPrototype(profile, 11);
    bool threw = false;
    try { forest.enableHierarchicalWind(); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    const auto* proto = forest.prototype(7);
    CHECK(proto && findWindRig(*proto));
    ForestWindState wind; wind.xCmPerSec = 900; wind.zCmPerSec = 300;
    forest.setWind(wind);
    forest.setPresentationTick(37);
    FloraBatch near{7, TreeLOD::High, {}}, far{7, TreeLOD::Impostor, {}};
    FoliageInstance tree; tree.key = 5; tree.speciesId = 7; tree.yawMilliDegrees = 90000;
    near.instances.push_back(tree); far.instances.push_back(tree);
    std::vector<ModelDraw> draws;
    forest.forEachDraw({near, far}, {}, [&](const ModelDraw& d) { draws.push_back(d); });
    CHECK(draws.size() == 2);
    const auto& n = draws[0];
    CHECK(n.useVertexParameters && n.nodeWorld == nullptr && n.maxVertexOffsetMetres > 0);
    CHECK(std::abs(n.vertexParameters.x) + std::abs(n.vertexParameters.y) > 0.01f);
    const auto& mats = n.materials ? *n.materials : n.model->materials;
    CHECK(mats[0].shaderGraph == windRigGraph() && mats[1].shaderGraph == windRigGraph());
    // Near tree is upright: whole-tree lean is replaced by the trunk mask.
    CHECK(std::abs(n.world.at(1, 1) - 1.f) < 1e-5f);
    // Without hierarchical wind the legacy path is unchanged.
    ArborForestAdapter legacy;
    legacy.addPrototype(profile, 11);
    legacy.setWind(wind);
    std::vector<ModelDraw> old;
    legacy.forEachDraw({near}, {}, [&](const ModelDraw& d) { old.push_back(d); });
    CHECK(old.size() == 1 && !findWindRig(*legacy.prototype(7)));
    CHECK(legacy.treeGeometry() == ArborTreeGeometry::PerBranch);
    CHECK(legacy.prototype(7)->nodes.size() > 1);
    // GPU wind -> merged whole-tree tiers: ONE node, three LOD meshes with at
    // most two primitives each, so all trees of a species share one draw per
    // material and tier.
    CHECK(forest.treeGeometry() == ArborTreeGeometry::Merged);
    // FloraView tier -> host-chosen whole-tree LOD level.
    CHECK(n.lodLevel == 0);
    {
        FloraBatch mid{7, TreeLOD::Medium, {tree}}, low{7, TreeLOD::Low, {tree}};
        std::vector<ModelDraw> tiers;
        forest.forEachDraw({mid, low}, {}, [&](const ModelDraw& d) { tiers.push_back(d); });
        CHECK(tiers.size() == 2 && tiers[0].lodLevel == 1 && tiers[1].lodLevel == 2);
        CHECK(draws[1].lodLevel == -1);   // impostor: no chain
        CHECK(old[0].lodLevel == -1);     // PerBranch: renderer picks per branch
    }
    CHECK(forcedModelLod(3, proto->meshes[0].lods, 1).mesh == 1 &&
          forcedModelLod(3, proto->meshes[0].lods, 9).mesh == 2 &&
          forcedModelLod(3, proto->meshes[0].lods, 0).mesh == 3);
    // Calm rigged trees still declare a bounded offset (keeps culling + LOD).
    ArborForestAdapter calmForest;
    calmForest.enableHierarchicalWind();
    calmForest.addPrototype(profile, 11);
    std::vector<ModelDraw> calmDraws;
    calmForest.forEachDraw({near}, {}, [&](const ModelDraw& d) { calmDraws.push_back(d); });
    CHECK(calmDraws.size() == 1 && calmDraws[0].maxVertexOffsetMetres > 0 && !calmDraws[0].useVertexParameters);
    CHECK(proto->nodes.size() == 1 && proto->meshes.size() == 3);
    CHECK(proto->meshes[0].lods.size() == 2 && proto->meshes[0].lods[0].mesh == 1 &&
          proto->meshes[0].lods[1].mesh == 2);
    for (const auto& mesh : proto->meshes) {
        CHECK(!mesh.primitives.empty() && mesh.primitives.size() <= 2);
        for (size_t i = 0; i < mesh.primitives.size(); ++i) CHECK(mesh.primitives[i].material == int(i));
    }
    // Coarser tiers really are cheaper.
    auto tris = [&](int m) { uint32_t n = 0; for (const auto& p : proto->meshes[size_t(m)].primitives) n += p.indexCount; return n; };
    CHECK(tris(0) > tris(1) && tris(1) > tris(2));
    // Builder: owners cover every vertex; leaf vertices flutter.
    std::vector<int32_t> owners;
    auto merged = makeArborTreeLodModel(profile, 11, {}, &owners);
    CHECK(owners.size() == merged->vertices.size());
    CHECK(*std::max_element(owners.begin(), owners.end()) > 0);
    // Explicit PerBranch with GPU wind keeps the per-branch nodes.
    ArborForestAdapter branches;
    branches.enableHierarchicalWind();
    branches.setTreeGeometry(ArborTreeGeometry::PerBranch);
    branches.addPrototype(profile, 11);
    CHECK(branches.prototype(7)->nodes.size() > 1 && findWindRig(*branches.prototype(7)));
    bool late = false;
    try { branches.setTreeGeometry(ArborTreeGeometry::Merged); } catch (const std::logic_error&) { late = true; }
    CHECK(late);
}
}

int main() {
    masks();
    hierarchy();
    modelBinding();
    sidecar();
    arbor();
    shaderText();
    decodeAndParams();
    inspection();
    forest();
    std::printf("wind rig: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
