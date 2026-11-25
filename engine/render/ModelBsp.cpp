// Quake III IBSP v46 planar surfaces and triangle soups.
#include "ModelInterchange.h"
#include <array>

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
    model->finalize();
    return model;
}
} // namespace aiverse::render
