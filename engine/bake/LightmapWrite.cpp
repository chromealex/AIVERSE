// Bake folder writer (agent-lightbake): lightmap.json + PNG pages + probes.bin. Host only.
#include <cstring>
#include "../data/Json.h"
#include "BakeScene.h"
#include "Half.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // stb_image_write uses sprintf (deprecated in Apple's SDK)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb_image_write/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace aiverse::bake {
namespace {
std::string png(const std::vector<uint8_t>& rgba, int w, int h) {
    std::string out;
    stbi_write_png_compression_level = 9;
    stbi_write_png_to_func([](void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size_t(size)); },
                           &out, w, h, 4, rgba.data(), w * 4);
    return out;
}
Json num3(float3 v) { Json a = Json::array(); a.push(Json::number(double(v.x), 5)); a.push(Json::number(double(v.y), 5)); a.push(Json::number(double(v.z), 5)); return a; }
} // namespace

bool saveLightmapData(const std::string& folder, const LightmapData& d, const WriteFn& write, std::string& error) {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(1)));
    j.set("hash", Json::string(hex64(d.hash)));
    j.set("mode", Json::string(bakedModeName(d.mode)));
    j.set("rgbmRange", Json::number(double(d.rgbmRange), 4));
    j.set("intensity", Json::number(double(d.intensity), 4));
    j.set("shadowColor", num3(d.shadowColor));
    Json pages = Json::array();
    for (size_t i = 0; i < d.pages.size(); ++i) {
        const auto& p = d.pages[i];
        Json pj = Json::object();
        pj.set("width", Json::number(int64_t(p.width)));
        pj.set("height", Json::number(int64_t(p.height)));
        const std::string lm = "lm" + std::to_string(i) + ".png";
        if (!write(folder + "/" + lm, png(p.rgbm, p.width, p.height))) { error = folder + "/" + lm + ": write failed"; return false; }
        pj.set("lightmap", Json::string(lm));
        if (!p.mask.empty()) {
            const std::string mk = "mask" + std::to_string(i) + ".png";
            if (!write(folder + "/" + mk, png(p.mask, p.width, p.height))) { error = folder + "/" + mk + ": write failed"; return false; }
            pj.set("mask", Json::string(mk));
        }
        pages.push(std::move(pj));
    }
    j.set("pages", std::move(pages));
    Json regions = Json::object();
    for (const auto& [k, r] : d.regions) {   // std::map: sorted keys, reproducible file
        Json a = Json::array();
        a.push(Json::number(int64_t(r.page)));
        for (float v : r.st) a.push(Json::number(double(v), 7));
        regions.set(hex64(k), std::move(a));
    }
    j.set("regions", std::move(regions));
    if (d.terrain.page >= 0) {
        Json t = Json::object();
        t.set("page", Json::number(int64_t(d.terrain.page)));
        Json st = Json::array(); for (float v : d.terrain.st) st.push(Json::number(double(v), 7));
        t.set("st", std::move(st));
        Json o = Json::array(); o.push(Json::number(double(d.terrain.origin[0]), 4)); o.push(Json::number(double(d.terrain.origin[1]), 4));
        Json e = Json::array(); e.push(Json::number(double(d.terrain.extent[0]), 4)); e.push(Json::number(double(d.terrain.extent[1]), 4));
        t.set("origin", std::move(o)); t.set("extent", std::move(e));
        j.set("terrain", std::move(t));
    }
    if (!d.probes.empty()) {
        const auto& g = d.probes;
        Json p = Json::object();
        p.set("origin", num3(g.origin)); p.set("cell", num3(g.cell));
        Json dims = Json::array(); dims.push(Json::number(int64_t(g.nx))); dims.push(Json::number(int64_t(g.ny))); dims.push(Json::number(int64_t(g.nz)));
        p.set("dims", std::move(dims));
        p.set("file", Json::string("probes.bin"));
        std::string bin(g.coeffs.size() * 2, '\0');
        for (size_t i = 0; i < g.coeffs.size(); ++i) { const uint16_t h = floatToHalf(g.coeffs[i]); std::memcpy(&bin[i * 2], &h, 2); }
        if (!write(folder + "/probes.bin", bin)) { error = folder + "/probes.bin: write failed"; return false; }
        j.set("probes", std::move(p));
    }
    try { j.set("settings", Json::parse(d.settingsJson)); j.set("stats", Json::parse(d.statsJson)); } catch (...) {}
    if (!write(folder + "/lightmap.json", j.dump(2) + "\n")) { error = folder + "/lightmap.json: write failed"; return false; }
    return true;
}

} // namespace aiverse::bake
