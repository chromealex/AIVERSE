#include "GameTerrainRender.h"
#include <algorithm>
#include <cstdio>

namespace aiverse::game {
using render::float3;
namespace {
float3 jsonColor(const Json& j, float3 def) {
    if (!j.isArray() || j.size() < 3) return def;
    return {float(j[0].toDouble()), float(j[1].toDouble()), float(j[2].toDouble())};
}

const char* kKindNames[] = {"grass", "sand", "ramp", "rock", "blocked", "lakebed", "shallows", "dug"};
} // namespace

const char* cellKindName(int k) { return k >= 0 && k < 8 ? kKindNames[k] : "?"; }

std::vector<uint8_t> parseMaterialRows(const Json& rows, int w, int h) {
    if (!rows.isArray() || w <= 0 || h <= 0) return {};
    std::vector<uint8_t> out(size_t(w * h), 255);
    for (size_t y = 0; y < rows.size() && int(y) < h; ++y) {
        std::string r = rows[y].str();
        for (size_t x = 0; x < r.size() && int(x) < w; ++x)
            if (r[x] >= '0' && r[x] <= '3') out[y * size_t(w) + x] = uint8_t(r[x] - '0');
    }
    return out;
}

int terrainCellKind(uint8_t f,bool dug){
    if (!(f & kCellGround) && (f & kCellWater)) return kKindLakebed;
    if (dug) return kKindDug;
    if (!(f & kCellGround)) return kKindBlocked;
    if (f & kCellRock) return kKindRock;
    if (f & kCellRamp) return kKindRamp;
    if (f & kCellWater) return kKindShallows;
    if (f & kCellNoBuild) return kKindSand;
    return kKindGrass;
}

void applyTerrainLook(TerrainLook& look,const Json& t) {
    if (!t.isObject()) return;
    look.render=t.getBool("render",look.render);
    if (t["pbr"].isObject()) {
        const Json& p=t["pbr"];
        look.pbr=p.getBool("enabled",look.pbr);
        look.packedPbr=p.getBool("packedMaps",look.packedPbr);
        look.pbrRoughness=std::clamp(float(p["roughness"].toDouble(look.pbrRoughness)),0.045f,1.0f);
    }
    if (t["layers"].isArray()) {
        look.layers.clear();
        for (auto& l : t["layers"].items()) {
            if (look.layers.size() >= 4) break;
            TerrainLook::Layer L;
            L.id = l.getStr("id", "layer" + std::to_string(look.layers.size()));
            L.texture = l.getStr("texture");
            L.normal = l.getStr("normal");
            L.scale = std::max(0.05f, float(l["scale"].toDouble(4)));
            L.tint = jsonColor(l["tint"], {1, 1, 1});
            look.layers.push_back(L);
        }
    }
    auto layerIndex = [&](const std::string& id) {
        for (size_t i = 0; i < look.layers.size(); ++i) if (look.layers[i].id == id) return int(i);
        return -1;
    };
    if (t["cells"].isObject())
        for (auto& [k, v] : t["cells"].members()) {
            int kind = -1;
            for (int i = 0; i < 8; ++i) if (k == kKindNames[i]) kind = i;
            if (kind < 0) continue;
            const Json& lj = v.isString() ? v : v["layer"];
            if (lj.isString()) { int li = layerIndex(lj.str()); if (li >= 0) look.cellLayer[kind] = li; }
            else if (lj.isNumber()) look.cellLayer[kind] = std::clamp(int(lj.integer()), 0, 3);
            if (v.isObject() && v["color"].isArray()) { look.hasColor[kind] = true; look.color[kind] = jsonColor(v["color"], {1, 1, 1}); }
        }
    if (const Json& c = t["cliff"]; c.isObject()) {
        look.cliffTexture = c.getStr("texture", look.cliffTexture);
        look.cliffNormal = c.getStr("normal", look.cliffNormal);
        look.cliffScale = std::max(0.05f, float(c["scale"].toDouble(look.cliffScale)));
        look.cliffTint = jsonColor(c["tint"], look.cliffTint);
        if (c["slope"].isArray() && c["slope"].size() >= 2) {
            look.slopeStart = float(c["slope"][0].toDouble()); look.slopeEnd = std::max(look.slopeStart + 0.01f, float(c["slope"][1].toDouble()));
        }
    }
    if (t.has("splatStrength")) look.splatStrength = std::clamp(float(t["splatStrength"].toDouble(1)), 0.0f, 1.0f);
    if (const Json& w = t["water"]; w.isObject()) {
        std::string m = w.getStr("mode", "auto");
        look.water = m == "sheet" ? TerrainLook::Water::Sheet : m == "bodies" ? TerrainLook::Water::Bodies : TerrainLook::Water::Auto;
        if (w["color"].isArray() && w["color"].size() >= 3) {
            float3 c = jsonColor(w["color"], {0.22f, 0.45f, 0.62f});
            look.waterColor = {c.x, c.y, c.z, w["color"].size() >= 4 ? float(w["color"][3].toDouble()) : 0.78f};
        }
        look.waterOffset = float(w["offset"].toDouble(look.waterOffset));
        const std::string renderer = w.getStr("renderer", "legacy");
        look.waterRenderer = renderer == "nereid" ? TerrainLook::WaterRenderer::Nereid : TerrainLook::WaterRenderer::Legacy;
        if (renderer != "nereid" && renderer != "legacy")
            std::fprintf(stderr, "[water] unknown terrain.water.renderer \"%s\", using legacy\n", renderer.c_str());
        if (w["nereid"].isObject()) look.nereid = w["nereid"];
    } else if (w.isString()) {
        std::string m = w.str();
        look.water = m == "sheet" ? TerrainLook::Water::Sheet : m == "bodies" ? TerrainLook::Water::Bodies : TerrainLook::Water::Auto;
    }
    if (const Json& l = t["lod"]; l.type() == Json::Type::Bool) look.lod = l.boolean() ? 1 : 0;
    else if (l.isString()) look.lod = l.str() == "on" ? 1 : l.str() == "off" ? 0 : -1;
    if (t.has("lodRange")) look.lodRange = std::max(16.0f, float(t["lodRange"].toDouble(128)));
    // detail surface (docs/terrain.md "Detail surface")
    if (t["style"].isString()) look.smooth = t["style"].str() == "smooth";
    if (const Json& d = t["detailLook"]; d.isObject()) {
        look.detailScale = std::clamp(int(d.getInt("scale", look.detailScale)), 1, 8);
        look.bevel = std::clamp(float(d["bevel"].toDouble(look.bevel)), 0.0f, 3.0f);
        look.wobble = std::clamp(float(d["wobble"].toDouble(look.wobble)), 0.0f, 2.0f);
        look.roughness = std::clamp(float(d["roughness"].toDouble(look.roughness)), 0.0f, 1.0f);
        look.blendSharpness = std::clamp(float(d["blendSharpness"].toDouble(look.blendSharpness)), 0.0f, 1.0f);
        look.macro = std::clamp(float(d["macro"].toDouble(look.macro)), 0.0f, 1.0f);
        look.detailRange = std::clamp(float(d["lodRange"].toDouble(look.detailRange)), 4.0f, 1000.0f);
        if (d["rockColor"].isArray()) { look.rockColor = jsonColor(d["rockColor"], look.rockColor); look.hasRockColor = true; }
    }
}

} // namespace aiverse::game
