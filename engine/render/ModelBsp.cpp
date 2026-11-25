// Quake III IBSP v46 planar surfaces and triangle soups.
#include "ModelInterchange.h"
#include <array>
#include "BspVisibility.h"
#include "BspShaderImages.h"
#include "LightmapUv.h"

namespace aiverse::render {
namespace {
// A Quake III patch is a grid of overlapping quadratic Bezier spans.
// Sample each shared boundary once so adjoining spans have identical vertices.
void appendPatch(Model& model, Primitive& prim, size_t source, size_t width, size_t height,
                 interchange::ByteReader& reader) {
    constexpr size_t steps = 4;
    if (width < 3 || height < 3 || !(width & 1) || !(height & 1) ||
        width > 129 || height > 129 || width * height != prim.vertexCount)
        reader.fail("invalid BSP patch grid");
    const size_t columns = ((width - 1)/2) * steps + 1;
    const size_t rows = ((height - 1)/2) * steps + 1;
    const size_t triangles = (rows - 1) * (columns - 1) * 2;
    if (model.vertices.size() + rows*columns > 0x200000 ||
        model.indices.size() + triangles*3 > 0x600000)
        reader.fail("BSP patch tessellation budget exceeded");
    prim.firstVertex = uint32_t(model.vertices.size());
    prim.vertexCount = uint32_t(rows * columns);
    prim.indexCount = uint32_t(triangles * 3);
    for (size_t y = 0; y < rows; ++y) {
        const size_t spanY = std::min(y/steps, (height-3)/2);
        const float v = float(y-spanY*steps)/steps;
        const float wy[]{(1-v)*(1-v), 2*v*(1-v), v*v};
        for (size_t x = 0; x < columns; ++x) {
            const size_t spanX = std::min(x/steps, (width-3)/2);
            const float u = float(x-spanX*steps)/steps;
            const float wx[]{(1-u)*(1-u), 2*u*(1-u), u*u};
            ModelVertex vertex{};
            float rgba[4]{};
            float2 lm{};
            for (size_t j = 0; j < 3; ++j) {
                for (size_t i = 0; i < 3; ++i) {
                    const float w = wy[j]*wx[i];
                    const auto& control = model.vertices[source+(2*spanY+j)*width+2*spanX+i];
                    vertex.pos += control.pos*w;
                    vertex.nrm += control.nrm*w;
                    vertex.uv.x += control.uv.x*w;
                    vertex.uv.y += control.uv.y*w;
                    const float2 controlLm = lightmapUv(control);
                    lm.x += controlLm.x*w;
                    lm.y += controlLm.y*w;
                    for (unsigned k = 0; k < 4; ++k)
                        rgba[k] += float((control.color >> (8*k)) & 255)*w/255;
                }
            }
            setLightmapUv(vertex, lm);
            vertex.nrm = normalize(vertex.nrm);
            vertex.color = interchange::packColor(rgba[0],rgba[1],rgba[2],rgba[3]);
            model.vertices.push_back(vertex);
        }
    }
    for (size_t y = 0; y+1 < rows; ++y) {
        for (size_t x = 0; x+1 < columns; ++x) {
            const uint32_t a = prim.firstVertex + uint32_t(y*columns+x);
            const uint32_t b = a+1, c = a+uint32_t(columns), d = c+1;
            // Match the orientation of the authored normals on either grid winding.
            const auto& va = model.vertices[a];
            const auto& vb = model.vertices[b];
            const auto& vc = model.vertices[c];
            if (dot(cross(vb.pos-va.pos,vc.pos-va.pos),va.nrm) >= 0)
                model.indices.insert(model.indices.end(), {a,b,c,b,d,c});
            else
                model.indices.insert(model.indices.end(), {a,c,b,b,c,d});
        }
    }
}

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
                               const FileReader& read, std::vector<std::string>* warnings) {
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
    // Shader image names are relative to the game root (maps/level.bsp -> textures/...).
    auto directory=interchange::dirOf(path);
    auto base=directory;
    if (base.size()>=5 && interchange::lowerStr(base.substr(base.size()-5))=="maps/")
        base.resize(base.size()-5);
    std::map<std::string,int> textureIndices;
    auto diffuse=[&](const std::string& name) {
        if (auto it=textureIndices.find(name);it!=textureIndices.end())return it->second;
        int& result=textureIndices[name]; result=-1;
        if (!bspAssetName(name))return result;
        const auto ext=interchange::baseName(name).find_last_of('.');
        const std::string stem=ext==name.npos?name:name.substr(0,name.find_last_of('.'));
        for (const auto* suffix : {"",".tga",".jpg",".png",".jpeg"}) {
            if (!*suffix && ext==name.npos)continue;
            const auto candidate=(*suffix?stem+suffix:name);
            Image image;image.name=candidate;
            if (!readModelImage(base+candidate,read,image))continue;
            result=int(model->images.size()); model->images.push_back(std::move(image)); break;
        }
        return result;
    };
    BspShaderImages scripts(read,base);
    for (size_t i = 0; i < shaders; ++i) {
        const char* name = bytes.data() + lumps[1].offset + i * 72;
        auto& material=model->materials.emplace_back();
        material.name.assign(name,std::find(name,name+64,'\0'));
        material.baseTex=diffuse(scripts.baseImage(material.name));
        if(material.baseTex<0)material.baseTex=diffuse(material.name);
    }
    constexpr size_t pixels=128*128;
    const size_t pageCount=records(14,pixels*3,256);
    const int firstPage=int(model->images.size());
    for (size_t page=0;page<pageCount;++page) {
        auto& image=model->images.emplace_back();
        image.name=path+"#lightmap/"+std::to_string(page);
        image.width=image.height=128; image.repeat=false;
        image.rgba.resize(pixels*4);
        const auto* src=reinterpret_cast<const uint8_t*>(bytes.data()+lumps[14].offset+page*pixels*3);
        for(size_t p=0;p<pixels;++p) {
            std::copy_n(src+3*p,3,image.rgba.data()+4*p);
            image.rgba[4*p+3]=255;
        }
    }
    auto vis=std::make_shared<BspVisibility>();
    reader.seek(lumps[10].offset);
    model->vertices.reserve(vertices);
    for (size_t i = 0; i < vertices; ++i) {
        auto& vertex = model->vertices.emplace_back();
        vertex.pos = interchange::zUpToYUp({finiteFloat(reader), finiteFloat(reader), finiteFloat(reader)});
        vertex.uv = {finiteFloat(reader), finiteFloat(reader)};
        setLightmapUv(vertex, {finiteFloat(reader), finiteFloat(reader)});
        vertex.nrm = interchange::zUpToYUp({finiteFloat(reader), finiteFloat(reader), finiteFloat(reader)});
        vertex.color = reader.u32();
    }
    auto& mesh = model->meshes.emplace_back();
    std::map<uint32_t,int> bakedMaterials;
    std::vector<int32_t> surfacePrimitive(surfaces, -1);
    size_t skipped = 0;
    for (size_t i = 0; i < surfaces; ++i) {
        reader.seek(lumps[13].offset + i * 104);
        const auto shader = reader.u32();
        reader.skip(4);
        const auto kind = reader.u32();
        if (kind != 1 && kind != 2 && kind != 3) { ++skipped; continue; }
        const auto firstVertex = reader.u32(), vertexCount = reader.u32();
        const auto firstIndex = reader.u32(), indexCount = reader.u32();
        requireSpan(reader, firstVertex, vertexCount, vertices);
        requireSpan(reader, firstIndex, indexCount, indices);
        if (shader >= shaders || indexCount % 3) reader.fail("invalid BSP triangle surface");
        if (kind != 2 && !indexCount) continue;
        if (model->indices.size() + indexCount > 0x600000) reader.fail("BSP index budget exceeded");
        surfacePrimitive[i] = int32_t(mesh.primitives.size());
        auto& prim = mesh.primitives.emplace_back();
        prim.material = int(shader);
        prim.firstVertex = firstVertex;
        prim.vertexCount = vertexCount;
        prim.firstIndex = uint32_t(model->indices.size());
        prim.indexCount = indexCount;
        reader.seek(lumps[13].offset + i * 104 + 28);
        const auto lightmapIndex = reader.le<int32_t>();
        if(lightmapIndex>=0 && size_t(lightmapIndex)>=pageCount)reader.fail("invalid BSP lightmap index");
        prim.lightmapUv=lightmapIndex>=0;
        if (prim.lightmapUv) {
            // Lightmapped BSP surfaces use baked irradiance instead of vertex lighting.
            auto [it,inserted]=bakedMaterials.try_emplace(shader,int(model->materials.size()));
            if (inserted) {
                auto material=model->materials[shader];
                material.vertexColors=false;
                model->materials.push_back(std::move(material));
            }
            prim.material=it->second;
        }
        vis->lightmapImages.push_back(prim.lightmapUv?firstPage+lightmapIndex:-1);
        if (kind == 2) {
            reader.seek(lumps[13].offset + i * 104 + 96);
            const auto width = reader.u32(), height = reader.u32();
            appendPatch(*model, prim, firstVertex, width, height, reader);
            continue;
        }
        reader.seek(lumps[11].offset + size_t(firstIndex) * 4);
        for (size_t j = 0; j < indexCount; j += 3) {
            const auto a = reader.u32(), b = reader.u32(), c = reader.u32();
            if (std::max({a,b,c}) >= vertexCount) reader.fail("BSP index outside surface");
            // Quake III stores clockwise triangles.
            model->indices.insert(model->indices.end(), {firstVertex+a, firstVertex+c, firstVertex+b});
        }
    }
    if (skipped && warnings) warnings->push_back(path + ": skipped " + std::to_string(skipped) + " non-triangle surfaces");
    if (warnings) {
        std::set<std::string> missing;
        for (const auto& prim:mesh.primitives) {
            const auto& mat=model->materials[size_t(prim.material)];
            if(mat.baseTex<0)missing.insert(mat.name);
        }
        for (const auto& name:missing)warnings->push_back(path+": BSP diffuse image unavailable: "+name);
    }
    if (mesh.primitives.empty()) reader.fail("no supported BSP surfaces");
    auto& node = model->nodes.emplace_back();
    node.name = interchange::baseName(path);
    node.mesh = 0;
    if (lumps[16].length) {
        const auto planeCount = records(2, 16, 0x20000);
        const auto branchCount = records(3, 36, 0x20000);
        const auto leafCount = records(4, 48, 0x20000);
        const auto referenceCount = records(5, 4, 0x20000);

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
    }
    model->bspVisibility = std::move(vis);
    model->finalize();
    return model;
}
} // namespace aiverse::render
