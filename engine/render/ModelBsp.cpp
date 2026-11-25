// Quake III IBSP v46 planar surfaces and triangle soups.
#include "ModelInterchange.h"
#include <array>
#include "BspVisibility.h"

namespace aiverse::render {
namespace {
struct Lump {
    size_t offset, length;
};
void requireSpan(interchange::ByteReader& reader, size_t first, size_t count, size_t total) {
    if (first > total || count > total - first) reader.fail("invalid BSP range");
}
float finiteFloat(interchange::ByteReader& reader) {
    const float value = reader.f32();
    if (!std::isfinite(value)) reader.fail("non-finite BSP vertex");
    return value;
}
}

std::unique_ptr<Model> loadBsp(const std::string& path, const std::string& bytes,
                               const FileReader&, std::vector<std::string>* warnings) {
    interchange::ByteReader reader(path, bytes);
    if (bytes.compare(0, 4, "IBSP") != 0) reader.fail("expected Quake III IBSP");
    reader.skip(4);
    if (reader.u32() != 46) reader.fail("unsupported BSP version");
    std::array<Lump, 17> lumps{};
    for (auto& lump : lumps) {
        lump = {reader.u32(), reader.u32()};
        requireSpan(reader, lump.offset, lump.length, bytes.size());
        if (lump.length && lump.offset < 144) reader.fail("BSP lump overlaps header");
    }
    auto records = [&](size_t lump, size_t stride, size_t limit) {
        if (lumps[lump].length % stride || lumps[lump].length / stride > limit)
            reader.fail("invalid BSP record count");
        return lumps[lump].length / stride;
    };
    const size_t vertices = records(10, 44, 0x80000);
    const size_t indices = records(11, 4, 0x80000);
    const size_t surfaces = records(13, 104, 0x20000);
    const size_t shaders = records(1, 72, 0x400);
    auto model = std::make_unique<Model>();
    for (size_t i = 0; i < shaders; ++i) {
        const char* name = bytes.data() + lumps[1].offset + i * 72;
        model->materials.emplace_back().name.assign(name, std::find(name, name + 64, '\0'));
    }
    reader.seek(lumps[10].offset);
    model->vertices.reserve(vertices);
    for (size_t i = 0; i < vertices; ++i) {
        auto& vertex = model->vertices.emplace_back();
        vertex.pos = interchange::zUpToYUp({finiteFloat(reader), finiteFloat(reader), finiteFloat(reader)});
        vertex.uv = {finiteFloat(reader), finiteFloat(reader)};
        reader.skip(8); // secondary UVs for baked lightmaps
        vertex.nrm = interchange::zUpToYUp({finiteFloat(reader), finiteFloat(reader), finiteFloat(reader)});
        vertex.color = reader.u32();
    }
    auto& mesh = model->meshes.emplace_back();
    std::vector<int32_t> surfacePrimitive(surfaces, -1);
    size_t skipped = 0;
    for (size_t i = 0; i < surfaces; ++i) {
        reader.seek(lumps[13].offset + i * 104);
        const auto shader = reader.u32();
        reader.skip(4);
        const auto kind = reader.u32();
        if (kind != 1 && kind != 3) { ++skipped; continue; }
        const auto firstVertex = reader.u32(), vertexCount = reader.u32();
        const auto firstIndex = reader.u32(), indexCount = reader.u32();
        requireSpan(reader, firstVertex, vertexCount, vertices);
        requireSpan(reader, firstIndex, indexCount, indices);
        if (shader >= shaders || indexCount % 3) reader.fail("invalid BSP triangle surface");
        if (!indexCount) continue;
        if (model->indices.size() + indexCount > 0x80000) reader.fail("BSP index budget exceeded");
        surfacePrimitive[i] = int32_t(mesh.primitives.size());
        auto& prim = mesh.primitives.emplace_back();
        prim.material = int(shader);
        prim.firstVertex = firstVertex;
        prim.vertexCount = vertexCount;
        prim.firstIndex = uint32_t(model->indices.size());
        prim.indexCount = indexCount;
        reader.seek(lumps[11].offset + size_t(firstIndex) * 4);
        for (size_t j = 0; j < indexCount; j += 3) {
            const auto a = reader.u32(), b = reader.u32(), c = reader.u32();
            if (std::max({a,b,c}) >= vertexCount) reader.fail("BSP index outside surface");
            // Quake III stores clockwise triangles.
            model->indices.insert(model->indices.end(), {firstVertex+a, firstVertex+c, firstVertex+b});
        }
    }
    if (skipped && warnings) warnings->push_back(path + ": skipped " + std::to_string(skipped) + " non-triangle surfaces");
    if (mesh.primitives.empty()) reader.fail("no supported BSP surfaces");
    auto& node = model->nodes.emplace_back();
    node.name = interchange::baseName(path);
    node.mesh = 0;
    if (lumps[16].length) {
        const auto planeCount = records(2, 16, 0x20000);
        const auto branchCount = records(3, 36, 0x20000);
        const auto leafCount = records(4, 48, 0x20000);
        const auto referenceCount = records(5, 4, 0x20000);
        auto vis = std::make_shared<BspVisibility>();
        reader.seek(lumps[16].offset);
        if (lumps[16].length < 8 || lumps[16].length > 0x200008)
            reader.fail("invalid BSP visibility size");
        const uint32_t clusters = reader.u32();
        vis->rowBytes = reader.u32();
        if (clusters > 0x20000 || vis->rowBytes < (clusters + 7)/8 ||
            uint64_t(clusters) * vis->rowBytes != lumps[16].length - 8)
            reader.fail("invalid BSP visibility rows");
        const auto* start = reinterpret_cast<const uint8_t*>(reader.data());
        vis->rows.assign(start, start + (lumps[16].length - 8));
        reader.seek(lumps[2].offset);
        for (size_t i = 0; i < planeCount; ++i) {
            float3 normal{finiteFloat(reader), finiteFloat(reader), finiteFloat(reader)};
            vis->planes.push_back({interchange::zUpToYUp(normal), finiteFloat(reader)});
        }
        reader.seek(lumps[3].offset);
        for (size_t i = 0; i < branchCount; ++i) {
            BspVisibility::Branch branch{reader.u32(), {reader.le<int32_t>(), reader.le<int32_t>()}};
            if (branch.plane >= planeCount) reader.fail("invalid BSP split plane");
            for (auto child : branch.child)
                if (child >= 0 ? size_t(child) >= branchCount : size_t(~child) >= leafCount)
                    reader.fail("invalid BSP child");
            vis->branches.push_back(branch);
            reader.skip(24);
        }
        vis->primitiveClusters.resize(mesh.primitives.size());
        // Bound total leaf expansion even when many leaves alias the same list.
        size_t memberships = 0;
        for (size_t i = 0; i < leafCount; ++i) {
            reader.seek(lumps[4].offset + i * 48);
            const int32_t cluster = reader.le<int32_t>();
            if (cluster < -1 || (cluster >= 0 && uint32_t(cluster) >= clusters))
                reader.fail("invalid BSP leaf cluster");
            vis->leafClusters.push_back(cluster);
            reader.skip(28);
            const auto first = reader.u32(), count = reader.u32();
            requireSpan(reader, first, count, referenceCount);
            memberships += count;
            if (memberships > 0x80000) reader.fail("too many BSP leaf memberships");
            reader.seek(lumps[5].offset + size_t(first) * 4);
            for (size_t j = 0; j < count; ++j) {
                const auto surface = reader.u32();
                if (surface >= surfaces) reader.fail("invalid BSP leaf surface");
                const int32_t primitive = surfacePrimitive[surface];
                if (primitive >= 0 && cluster >= 0)
                    vis->primitiveClusters[size_t(primitive)].push_back(uint32_t(cluster));
            }
        }
        model->bspVisibility = std::move(vis);
    }
    model->finalize();
    return model;
}
} // namespace aiverse::render
