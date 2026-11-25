#include "engine/render/Model.h"
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
    for (const auto [offset,value] : {std::pair{4, 47}, {356, 3}, {384, -1}, {116, 105}}) {
        auto bad = bytes;
        put(bad, offset, value);
        try { load(bad); return 2; } catch (const std::runtime_error&) {}
    }
}
