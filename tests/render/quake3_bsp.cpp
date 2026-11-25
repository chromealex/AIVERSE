#include "engine/render/Model.h"
#include "engine/render/BspVisibility.h"
#include <cstring>
#include <stdexcept>

namespace {
template<class T> void put(std::string& out, size_t offset, T v) {
    std::memcpy(out.data() + offset, &v, sizeof(v));
}
void lump(std::string& out, int id, int offset, int size) {
    put(out, 8 + id * 8, offset);
    put(out, 12 + id * 8, size);
}
std::string triangle() {
    std::string bytes(464, '\0');
    bytes.replace(0, 4, "IBSP");
    put(bytes, 4, 46);
    lump(bytes, 1, 144, 72);
    lump(bytes, 10, 216, 132);
    lump(bytes, 11, 348, 12);
    lump(bytes, 13, 360, 104);
    bytes.replace(144, 5, "stone");
    put(bytes, 260, 1.f);
    put(bytes, 312, 1.f);
    put(bytes, 352, 1);
    put(bytes, 356, 2);
    put(bytes, 368, 1);
    put(bytes, 376, 3);
    put(bytes, 384, 3);
    put(bytes, 388, -1);
    return bytes;
}
// Both leaves initially reference the single triangle through distinct PVS rows.
std::string partitionedTriangle() {
    auto bytes = triangle();
    bytes.resize(630);
    lump(bytes, 2, 464, 16);
    lump(bytes, 3, 480, 36);
    lump(bytes, 4, 516, 96);
    lump(bytes, 5, 612, 8);
    lump(bytes, 16, 620, 10);
    put(bytes, 464, 1.f);
    put(bytes, 484, -1);
    put(bytes, 488, -2);
    put(bytes, 552, 1);
    put(bytes, 564, 1);
    put(bytes, 596, 1);
    put(bytes, 600, 1);
    put(bytes, 620, 2);
    put(bytes, 624, 1);
    bytes[628] = 1;
    bytes[629] = 2;
    return bytes;
}
std::string curvedPatch() {
    auto bytes = triangle();
    bytes.resize(464 + 9*44);
    lump(bytes, 10, 464, 9*44);
    put(bytes, 368, 2);
    put(bytes, 376, 9);
    put(bytes, 384, 0);
    put(bytes, 456, 3);
    put(bytes, 460, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            size_t offset = 464 + size_t(3*y+x)*44;
            put(bytes, offset, float(x));
            put(bytes, offset+4, float(y));
            put(bytes, offset+8, x == 1 && y == 1 ? 4.f : 0.f);
            put(bytes, offset+36, 1.f);
        }
    }
    return bytes;
}
auto load(const std::string& bytes) {
    return aiverse::render::loadModel("maps/test.bsp", [&](const std::string& path, std::string& data) {
        if (path != "maps/test.bsp") return false;
        data = bytes;
        return true;
    });
}
}

int main() {
    auto bytes = triangle();
    auto model = load(bytes);
    if (model->materials.at(0).name != "stone" || model->vertices.at(2).pos.y != 1.f ||
        model->indices != std::vector<uint32_t>{0, 2, 1}) return 1;
    auto lit=bytes;
    const size_t start=lit.size(), pageSize=128*128*3;
    lit.resize(start+pageSize, char(91));
    lump(lit,14,int(start),int(pageSize));
    put(lit,388,0);
    lit.replace(144,20,std::string("textures/test/stone")+char(0));
    const auto loaded=aiverse::render::loadModel("pack/maps/test.bsp",[&](const std::string& file,std::string& out){
        if(file=="pack/maps/test.bsp"){out=lit;return true;}
        if(file=="pack/scripts/shaderlist.txt"){out="// enabled scripts\ncustom";return true;}
        if(file=="pack/scripts/custom.shader"){out="textures/test/stone { { map $lightmap } { map textures/tiles/red.tga } }";return true;}
        if(file!="pack/textures/tiles/red.tga")return false;
        out.assign(21,char(0));out[2]=2;out[12]=out[14]=1;out[16]=24;out[20]=char(252);return true;
    });
    const auto& p=loaded->meshes.at(0).primitives.at(0);
    const auto& mat=loaded->materials.at(p.material);
    if (!p.lightmapUv || mat.vertexColors || mat.baseTex<0 ||
        loaded->images.at(mat.baseTex).rgba.at(0)!=252)return 9;
    const auto& page=loaded->images.at(loaded->bspVisibility->lightmapImages.at(0));
    if (page.width!=128 || page.height!=128 || page.repeat ||
        page.rgba.at(0)!=91 || page.rgba.at(3)!=255)return 10;
    put(lit,388,1);
    try{load(lit);return 11;}catch(const std::runtime_error&){}
    auto patch = curvedPatch();
    auto curved = load(patch);
    const auto& surface = curved->meshes.at(0).primitives.at(0);
    if (surface.vertexCount != 25 || surface.indexCount != 96 ||
        curved->vertices.at(surface.firstVertex+12).pos.y != 1.f) return 7;
    put(patch, 456, 4);
    try { load(patch); return 8; } catch (const std::runtime_error&) {}
    auto partitioned = partitionedTriangle();
    auto level = load(partitioned);
    const auto& both = *level->bspVisibility;
    if (both.cameraCluster({1,0,0}) != 0 || both.cameraCluster({-1,0,0}) != 1 ||
        !both.visible(0,0) || !both.visible(1,0)) return 3;
    put(partitioned, 600, 0);
    level = load(partitioned);
    if (level->bspVisibility->visible(1,0) || !level->bspVisibility->visible(-1,0)) return 4;
    partitioned[629] = 3;
    if (!load(partitioned)->bspVisibility->visible(1,0)) return 5;
    for (const auto& [offset,value] : {std::pair{484, -3}, {612, 1}, {624, 0}, {516, 2}}) {
        auto bad = partitioned;
        put(bad, offset, value);
        try { load(bad); return 6; } catch (const std::runtime_error&) {}
    }
    for (const auto& [offset,value] : {std::pair{4, 47}, {356, 3}, {384, -1}, {116, 105}}) {
        auto bad = bytes;
        put(bad, offset, value);
        try { load(bad); return 2; } catch (const std::runtime_error&) {}
    }
}
