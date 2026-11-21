// CPU-only renderer import LOD regression: no Metal/window required.
#include "engine/render/AutoLodRuntime.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static Model makeRigidVehicle() {
    Model m;
    m.path = "models/vehicles/rigid_test.glb";
    m.materials.resize(2);
    for (int part = 0; part < 2; ++part) {
        constexpr int cells = 32;
        const uint32_t firstVertex = uint32_t(m.vertices.size());
        const uint32_t firstIndex = uint32_t(m.indices.size());
        for (int z = 0; z <= cells; ++z)
            for (int x = 0; x <= cells; ++x) {
                ModelVertex v;
                v.pos = {float(x) / cells, float(part), float(z) / cells};
                v.nrm = {0, 1, 0};
                v.uv = {float(x) / cells, float(z) / cells};
                m.vertices.push_back(v);
            }
        for (int z = 0; z < cells; ++z)
            for (int x = 0; x < cells; ++x) {
                const uint32_t v = firstVertex + uint32_t(z * (cells + 1) + x);
                m.indices.insert(m.indices.end(), {v, v+1, v+uint32_t(cells+1),
                    v+1, v+uint32_t(cells+2), v+uint32_t(cells+1)});
            }
        Primitive primitive;
        primitive.material = part;
        primitive.firstVertex = firstVertex;
        primitive.vertexCount = uint32_t(m.vertices.size()) - firstVertex;
        primitive.firstIndex = firstIndex;
        primitive.indexCount = uint32_t(m.indices.size()) - firstIndex;
        MeshDef mesh;
        mesh.name = part ? "wheel_mesh" : "body_mesh";
        mesh.primitives.push_back(primitive);
        const int meshId = int(m.meshes.size());
        m.meshes.push_back(std::move(mesh));
        Node node;
        node.name = part ? "wheel_0_left" : "chassis";
        node.mesh = meshId;
        node.parent = part ? 0 : -1;
        node.t = {float(part), 0, 0};
        m.nodes.push_back(std::move(node));
    }
    // A rigid animated node, which the runtime still needs for steering and
    // independent wheel rotation. AutoLOD must not reject the whole model
    // merely because its GLB contains ordinary node animation channels.
    AnimClip spin;
    spin.name = "wheel";
    AnimChannel channel;
    channel.node = 1;
    channel.path = AnimChannel::Rotation;
    channel.times = {0.f, 1.f};
    channel.values = {0,0,0,1, 0,0,1,0};
    spin.channels.push_back(std::move(channel));
    m.clips.push_back(std::move(spin));
    m.finalize();
    return m;
}

static Model makeMorphFace() {
    auto m=makeRigidVehicle();
    MeshDef& face=m.meshes[0];
    Primitive& p=face.primitives[0];
    p.morphTargets=52;
    p.morphDeltas.assign(size_t(p.morphTargets)*p.vertexCount*6u,0.f);
    for(int ch=0;ch<p.morphTargets;++ch) {
        face.morphTargetNames.push_back("expression_"+std::to_string(ch));
        for(size_t v=0;v<p.vertexCount;++v) {
            const auto& pos=m.vertices[size_t(p.firstVertex)+v].pos;
            p.morphDeltas[(size_t(ch)*p.vertexCount+v)*6u+1u]=
                float(ch+1)*1e-6f*(pos.x+pos.z);
        }
    }
    m.finalize();
    return m;
}

static void check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "auto LOD regression: %s\n", why);
        std::abort();
    }
}
int main() {
    auto m = makeRigidVehicle();
    const auto indices = m.indices;
    const auto nodeNames = std::vector<std::string>{m.nodes[0].name, m.nodes[1].name};
    const size_t meshCount = m.meshes.size();
    const size_t vertexCount = m.vertices.size();
    const auto wheelNode = m.nodes[1].t;
    const auto bodyPrimitive = m.meshes[0].primitives[0];
    check(generateMissingModelLods(m) >= 2, "expected importer-generated body and wheel LOD");
    check(m.autoLod.generatedMeshes == 2 && m.autoLod.generatedLevels >= 2,
          "incorrect generated base mesh/LOD accounting");
    check(m.nodes.size() == 2 && m.nodes[0].name == nodeNames[0] &&
          m.nodes[1].name == nodeNames[1] && m.nodes[1].mesh == 1 &&
          m.nodes[1].t.x == wheelNode.x,
          "changing rigid hierarchy breaks wheel/door animation");
    check(m.clips.size() == 1 && m.clips[0].channels.size() == 1 &&
          m.clips[0].channels[0].node == 1, "rigid animation channel changed");
    check(m.vertices.size() == vertexCount &&
          std::equal(indices.begin(), indices.end(), m.indices.begin()),
          "authored LOD0 geometry was changed");
    check(m.meshes[0].primitives[0].indexCount == bodyPrimitive.indexCount,
          "original body index count changed");
    check(m.meshes.size() > meshCount, "no new coarse mesh");
    for (size_t base = 0; base < meshCount; ++base) {
        const auto& mesh = m.meshes[base];
        check(!mesh.lods.empty(), "missing generated chain");
        float lastPixels = 1e30f;
        size_t lastIndices = mesh.primitives[0].indexCount;
        for (const auto& lod : mesh.lods) {
            check(lod.mesh >= int(meshCount) && size_t(lod.mesh) < m.meshes.size(),
                  "generated chain points outside mesh buffer");
            check(lod.maxPixels > 0 && lod.maxPixels < lastPixels,
                  "LOD screen-size thresholds must be strictly descending");
            const auto& coarse = m.meshes[size_t(lod.mesh)];
            check(coarse.primitives.size() == mesh.primitives.size() &&
                  coarse.primitives[0].material == mesh.primitives[0].material,
                  "coarse geometry changed material slots");
            const size_t count = coarse.primitives[0].indexCount;
            check(count < lastIndices, "coarse LOD did not reduce triangles");
            lastPixels = lod.maxPixels;
            lastIndices = count;
        }
    }
    const size_t after = m.meshes.size(), indicesAfter = m.indices.size();
    generateMissingModelLods(m);
    check(m.meshes.size() == after && m.indices.size() == indicesAfter,
          "preparation not idempotent");

    auto authored = makeRigidVehicle();
    authored.meshes.push_back(authored.meshes[0]);
    authored.meshes[0].lods.push_back({int(authored.meshes.size()) - 1, 128});
    authored.finalize();
    generateMissingModelLods(authored);
    check(authored.meshes[0].lods.size() == 1 && authored.meshes[0].lods[0].maxPixels == 128,
          "authored LOD chains must have priority");

    // The actual shared import/upload entry point must invoke the fallback
    // without callers needing to know about AutoLodRuntime.
    auto automatic = makeRigidVehicle();
    prepareModelUpload(automatic);
    check(automatic.uploadPrepared && automatic.autoLod.generatedLevels >= 2,
          "prepareModelUpload did not generate LODs automatically");
    const size_t preparedMeshCount = automatic.meshes.size();
    prepareModelUpload(automatic);
    check(automatic.meshes.size() == preparedMeshCount,
          "repeated prepareModelUpload changed LODs");

    auto skinned = makeRigidVehicle();
    skinned.skins.emplace_back();
    check(generateMissingModelLods(skinned) == 0,
          "skinned assets must not use rigid-only simplification");
    auto procedural = makeRigidVehicle();
    procedural.path = "roads";
    check(generateMissingModelLods(procedural) == 0,
          "procedural road geometry must keep its own chunk/culling policy");
    auto optOut = makeRigidVehicle();
    optOut.batching.autoLod = false;
    check(generateMissingModelLods(optOut) == 0,
          "per-asset autoLod=false must disable generation");

    auto morph=makeMorphFace();
    const auto sourceDeltas=morph.meshes[0].primitives[0].morphDeltas;
    const size_t sourceVertices=morph.vertices.size();
    const size_t sourceIndices=morph.indices.size();
    check(generateMissingModelLods(morph)>0,"morph-aware index LOD generation");
    check(!morph.meshes[0].lods.empty(),"missing morph chain");
    check(morph.vertices.size()==sourceVertices &&
          morph.meshes[0].primitives[0].morphDeltas==sourceDeltas,
          "source morph geometry must be immutable");
    check(morph.meshes[0].primitives[0].morphTargets==52,
          "52 expression source lost");
    for(const auto& lod:morph.meshes[0].lods) {
        const auto& coarse=morph.meshes[size_t(lod.mesh)];
        const auto& p=coarse.primitives[0];
        check(coarse.morphTargetNames==morph.meshes[0].morphTargetNames,
              "coarse semantic channel identity lost");
        check(lod.morphSourceForTarget.size()==52 &&
              lod.morphSourceForTarget[0]==0 &&
              lod.morphSourceForTarget[51]==51,
              "morph source-to-target mapping must be exact");
        check(p.morphTargets==52 && p.morphAliasMesh==0 &&
              p.morphAliasPrimitive==0 && p.morphDeltas.empty() &&
              p.firstVertex==morph.meshes[0].primitives[0].firstVertex &&
              p.vertexCount==morph.meshes[0].primitives[0].vertexCount,
              "LOD may not duplicate or reindex 52 GPU morph channels");
        check(p.boundsMax.y>=morph.meshes[0].primitives[0].boundsMax.y,
              "coarse bounds must cover original morph envelope");
        for(uint32_t k=0;k<p.indexCount;++k)
            check(morph.indices[size_t(p.firstIndex)+k]<sourceVertices,
                  "coarse index outside original vertex buffer");
    }
    check(morph.indices.size()>sourceIndices,"coarse indices not appended");
    const size_t morphMeshes=morph.meshes.size();
    generateMissingModelLods(morph);
    check(morph.meshes.size()==morphMeshes,"morph generation not idempotent");

    auto missingNames=makeMorphFace();
    missingNames.meshes[0].morphTargetNames.clear();
    generateMissingModelLods(missingNames);
    check(missingNames.meshes[0].lods.empty(),
          "anonymous morph channels must not get guessed LOD mapping");
    auto badLayout=makeMorphFace();
    badLayout.meshes[0].primitives[0].morphDeltas.pop_back();
    // Other (valid) meshes of the fixture still get LODs; the closing
    // Model::finalize() then rejects the malformed face layout. Either
    // outcome is fine as long as the face never reaches the simplifier.
    try {generateMissingModelLods(badLayout);}
    catch(const std::runtime_error&) {}
    check(badLayout.meshes[0].lods.empty(),
          "malformed morph data must not enter simplifier");
    std::puts("auto LOD regression: rigid/morph channels, aliases, envelopes, indices and opt-outs passed");
}
