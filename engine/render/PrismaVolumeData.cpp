#include "PrismaVolumeData.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace aiverse::render {
namespace {

float number(const Json& src, const char* key, float fallback, float lo, float hi) {
    if (!src.has(key)) return fallback;
    if (!src[key].isNumber()) throw std::runtime_error(std::string(key) + " must be a number");
    const double v = src[key].toDouble();
    if (!std::isfinite(v) || v < lo || v > hi)
        throw std::runtime_error(std::string(key) + " outside allowed range");
    return static_cast<float>(v);
}
float3 vector3(const Json& src, const char* key) {
    const Json& a = src[key];
    if (!a.isArray() || a.size() != 3)
        throw std::runtime_error(std::string(key) + " must be [x,y,z]");
    float v[3]{};
    for (int i = 0; i < 3; ++i) {
        if (!a[size_t(i)].isNumber())
            throw std::runtime_error(std::string(key) + " contains a non-number");
        const double f = a[size_t(i)].toDouble();
        if (!std::isfinite(f) || std::abs(f) > 1000000.0)
            throw std::runtime_error(std::string(key) + " outside world coordinate range");
        v[i] = static_cast<float>(f);
    }
    return {v[0],v[1],v[2]};
}
std::vector<PrismaVolumeEffectMask> authoredEffects(const Json& profile) {
    std::vector<PrismaVolumeEffectMask> out;
    const Json& effects = profile["effects"];
    if (!effects.isArray()) return out;
    for (const Json& effect : effects.items()) {
        if (!effect.isObject()) continue;
        const auto* desc = prismaType(effect.getStr("type"));
        if (!desc) continue; // parsePrisma reports or throws for unknown IDs
        PrismaVolumeEffectMask mask;
        mask.type = desc->type;
        mask.enabled = effect.has("enabled");
        mask.minQuality = effect.has("minQuality");
        mask.graph = effect.has("graph");
        for (const auto& param : desc->params) {
            if (param.kind == PrismaParamKind::Text) {
                mask.text |= effect.has(param.name);
                continue;
            }
            const size_t count = param.kind == PrismaParamKind::Color ? 3u : 1u;
            for (size_t i = 0; i < count; ++i)
                mask.numeric.push_back(effect.has(param.name) ? uint8_t(1) : uint8_t(0));
        }
        auto previous = std::find_if(out.begin(), out.end(),
            [&](const PrismaVolumeEffectMask& item){ return item.type == mask.type; });
        if (previous == out.end()) out.push_back(std::move(mask));
        else *previous = std::move(mask); // last effect of a type wins, like PrismaData
    }
    return out;
}
PrismaVolume one(const Json& j, const std::string& fallback, std::vector<std::string>* issues) {
    if (!j.isObject()) throw std::runtime_error("volume entry must be an object");
    PrismaVolume v;
    if (j.has("id") && !j["id"].isString())
        throw std::runtime_error("volume id must be a string");
    v.id = j.getStr("id", fallback);
    if (v.id.empty() || v.id.size() > 96)
        throw std::runtime_error("volume id must be 1..96 characters");
    const auto shape = j.getStr("shape", "global");
    if (shape == "global") v.shape = PrismaVolumeShape::Global;
    else if (shape == "sphere") v.shape = PrismaVolumeShape::Sphere;
    else if (shape == "box") v.shape = PrismaVolumeShape::Box;
    else throw std::runtime_error("unknown shape '" + shape + "'");
    v.weight = number(j,"weight",1.f,0.f,1.f);
    v.blendDistance = number(j,"blendDistance",0.f,0.f,100000.f);
    v.enabled = j.getBool("enabled",true);
    if (j.has("priority")) {
        const auto n = j["priority"].integer(std::numeric_limits<int64_t>::min());
        if (!j["priority"].isNumber() || n < -100000 || n > 100000)
            throw std::runtime_error("priority must be integer in [-100000,100000]");
        v.priority = static_cast<int>(n);
    }
    if (j.has("layerMask")) {
        // Json::integer() uses simulation Fixed and can overflow 0xffffffff.
        // Read the authored integer literal instead; this is presentation-only data.
        if (!j["layerMask"].isNumber())
            throw std::runtime_error("layerMask must be unsigned 32-bit integer");
        const std::string literal = j["layerMask"].numberLiteral();
        if (literal.empty()) throw std::runtime_error("layerMask must not be empty");
        uint64_t mask = 0;
        for (char digit : literal) {
            if (digit < '0' || digit > '9')
                throw std::runtime_error("layerMask must be unsigned 32-bit integer");
            mask = mask * 10u + uint64_t(digit - '0');
            if (mask > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("layerMask exceeds 32 bits");
        }
        v.layerMask = static_cast<uint32_t>(mask);
    }
    if (v.shape == PrismaVolumeShape::Sphere) {
        v.center = vector3(j, "center");
        v.radius = number(j,"radius",1.f,0.00001f,100000.f);
    } else if (v.shape == PrismaVolumeShape::Box) {
        v.center = vector3(j, "center");
        v.halfExtents = vector3(j, "halfExtents");
        if (v.halfExtents.x <= 0 || v.halfExtents.y <= 0 || v.halfExtents.z <= 0)
            throw std::runtime_error("box halfExtents must be positive on all axes");
    }
    const Json& profile = j["profile"];
    if (!profile.isObject())
        throw std::runtime_error("profile must be a postfx settings object");
    v.overrideEnabled = profile.has("enabled");
    v.overrideHdr = profile.has("hdr");
    v.profile = parsePrisma(profile, Json(), issues);
    v.effectMasks = authoredEffects(profile);
    return v;
}
} // namespace

Json makePrismaVolumeJson(const std::string& id, PrismaVolumeShape shape, float3 center) {
    if (id.empty() || id.size() > 96)
        throw std::invalid_argument("PRISMA volume id must be 1..96 characters");
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
        throw std::invalid_argument("PRISMA volume center must be finite");
    Json v = Json::object();
    v.set("id", Json::string(id));
    v.set("shape", Json::string(shape == PrismaVolumeShape::Global ? "global" :
                                 shape == PrismaVolumeShape::Sphere ? "sphere" : "box"));
    if (shape != PrismaVolumeShape::Global) {
        Json c = Json::array();
        c.push(Json::number(double(center.x), 3));
        c.push(Json::number(double(center.y), 3));
        c.push(Json::number(double(center.z), 3));
        v.set("center", std::move(c));
    }
    if (shape == PrismaVolumeShape::Sphere)
        v.set("radius", Json::number(8.0));
    if (shape == PrismaVolumeShape::Box) {
        Json e = Json::array();
        for (int i = 0; i < 3; ++i) e.push(Json::number(8.0));
        v.set("halfExtents", std::move(e));
    }
    v.set("blendDistance", Json::number(2.0));
    Json profile = Json::object();
    profile.set("effects", Json::array());
    v.set("profile", std::move(profile));
    return v;
}

PrismaVolumeStack parsePrismaVolumes(const Json& game, const Json& map,
                                    std::vector<std::string>* issues) {
    PrismaVolumeStack stack;
    const auto parseBlock = [&](const Json& block, bool mapScope) {
        if (block.isNull()) return;
        if (!block.isObject()) {
            if (issues) issues->push_back("prismaVolumes: expected {volumes:[...]} object");
            return;
        }
        if (mapScope && block.getBool("replace", false)) stack.volumes.clear();
        const Json& list = block["volumes"];
        if (list.isNull()) return;
        if (!list.isArray()) {
            if (issues) issues->push_back("prismaVolumes.volumes must be an array");
            return;
        }
        std::unordered_set<std::string> idsInBlock;
        for (size_t i = 0; i < list.size(); ++i) {
            const std::string generated = std::string(mapScope ? "map:" : "game:") + std::to_string(i);
            try {
                PrismaVolume v = one(list[i], generated, issues);
                if (!idsInBlock.insert(v.id).second)
                    throw std::runtime_error("duplicate id in same scope: " + v.id);
                auto it = std::find_if(stack.volumes.begin(), stack.volumes.end(),
                                       [&](const PrismaVolume& x){ return x.id == v.id; });
                if (it != stack.volumes.end()) *it = std::move(v);
                else stack.volumes.push_back(std::move(v));
            } catch (const std::exception& e) {
                if (issues) issues->push_back("prismaVolumes[" + std::to_string(i) + "]: " + e.what());
            }
        }
    };
    parseBlock(game,false);
    parseBlock(map,true);
    return stack;
}

} // namespace aiverse::render
