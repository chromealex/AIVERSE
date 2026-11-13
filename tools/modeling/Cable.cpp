#include "Modeling.h"
#include "engine/cables/Cables.h"
namespace aiverse::modeling {
Mesh cableGeometry(const Json &s, const std::atomic<bool> *cancel) {
    cables::Options o;
    require(s["anchors"].isArray(), "cable anchors must be an array");
    for (const auto &a : s["anchors"].items()) {
        require(a.isArray() && a.size()==3, "cable anchor needs three coordinates");
        o.anchors.push_back(vector3(a, {}, "cable anchor"));
    }
    o.sag=number(s["sag"],o.sag,"cable sag");
    o.radius=number(s["radius"],o.radius,"cable radius");
    o.uvRepeat=number(s["uvRepeat"],o.uvRepeat,"cable uvRepeat");
    o.spacing=number(s["spacing"],o.spacing,"cable spacing");
    o.down=vector3(s["down"],o.down,"cable down");
    o.bundleAxis=vector3(s["bundleAxis"],o.bundleAxis,"cable bundleAxis");
    auto count=[&](const char *key,unsigned fallback) {
        const auto &v=s[key]; if (v.isNull()) return fallback;
        require(v.isNumber() && v.toDouble()==double(v.integer(-1)) && v.integer(-1)>=0 && v.integer(-1)<=256,
                std::string("cable ")+key+": expected bounded integer");
        return unsigned(v.integer());
    };
    o.segments=count("segments",o.segments); o.sides=count("sides",o.sides); o.strands=count("strands",o.strands);
    if (!s["caps"].isNull()) {require(s["caps"].type() == Json::Type::Bool, "cable caps must be boolean"); o.caps=s["caps"].boolean();}
    auto cable=cables::build(o,cancel); Mesh m; m.vertices.reserve(cable.vertices.size());
    for (const auto &v : cable.vertices) {ModelVertex x{};x.pos=v.position;x.nrm=v.normal;x.uv=v.uv;m.vertices.push_back(x);}
    m.indices=std::move(cable.indices); return m;
}
} // namespace aiverse::modeling
