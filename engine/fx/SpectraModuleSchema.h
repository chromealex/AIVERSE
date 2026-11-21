// SpectraFX graph-v1 typed authoring fields.
// Metadata only: the authoritative emitter parser remains FxEffect.cpp.
// No renderer, editor, FPS/RTS, or gameplay dependencies.
#pragma once

#include "engine/data/Json.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::spectra {

enum class FieldKind : unsigned char {
    Number, Integer, Boolean, String, Choice, Vector3, Color
};

struct FieldSpec {
    const char* key;
    const char* label;
    FieldKind kind;
    double minimum = 0.0;
    double maximum = 0.0;
    const char* initial = "null";   // JSON literal; only used on explicit Add
    const char* choices = "";       // | separated (Choice only)
};

// These are intentionally conservative common fields, not a closed set of
// EmitterDef properties. Advanced modules/curves remain editable as JSON.
inline constexpr std::array<FieldSpec, 6> kSpawnFields{{
    {"burst", "Burst count", FieldKind::Integer, 0, 2000, "12"},
    {"rate", "Particles / second", FieldKind::Number, 0, 2000, "20"},
    {"perMeter", "Particles / metre", FieldKind::Number, 0, 200, "1"},
    {"delay", "Delay (s)", FieldKind::Number, 0, 60, "0"},
    {"duration", "Duration (s)", FieldKind::Number, 0, 600, "1"},
    {"loop", "Loop emission", FieldKind::Boolean, 0, 0, "false"}
}};
inline constexpr std::array<FieldSpec, 8> kInitializeFields{{
    {"life", "Lifetime (s)", FieldKind::Number, 0.01, 60, "1"},
    {"speed", "Initial speed", FieldKind::Number, -500, 500, "1"},
    {"size", "Particle size", FieldKind::Number, 0.001, 100, "0.4"},
    {"direction", "Direction", FieldKind::Vector3, 0, 0, "[0,1,0]"},
    {"spread", "Spread (degrees)", FieldKind::Number, 0, 180, "30"},
    {"shape", "Spawn shape", FieldKind::Choice, 0, 0, "\"point\"", "point|sphere|hemisphere|disc|ring|donut|cone|box|line|mesh"},
    {"radius", "Shape radius", FieldKind::Number, 0, 100, "0.2"},
    {"gravity", "Gravity", FieldKind::Vector3, 0, 0, "[0,-9.81,0]"}
}};
inline constexpr std::array<FieldSpec, 7> kUpdateFields{{
    {"drag", "Drag", FieldKind::Number, 0, 50, "0"},
    {"spin", "Spin (deg/s)", FieldKind::Number, -36000, 36000, "0"},
    {"rotation", "Rotation (degrees)", FieldKind::Number, -3600, 3600, "0"},
    {"speedLimit", "Speed limit", FieldKind::Number, 0, 1000, "10"},
    {"stretch", "Stretch", FieldKind::Number, 0, 10, "0"},
    {"velocity", "Added velocity", FieldKind::Vector3, 0, 0, "[0,1,0]"},
    {"hidden", "Hide particles", FieldKind::Boolean, 0, 0, "false"}
}};
inline constexpr std::array<FieldSpec, 4> kSpriteFields{{
    {"texture", "Texture path", FieldKind::String, 0, 0, "\"builtin:soft\""},
    {"blend", "Blend mode", FieldKind::Choice, 0, 0, "\"alpha\"", "alpha|additive|premultiplied|multiply|subtract|opaque"},
    {"align", "Alignment", FieldKind::Choice, 0, 0, "\"camera\"", "camera|velocity|ground|upright|world|local"},
    {"fps", "Flipbook FPS", FieldKind::Number, 0, 120, "0"}
}};
inline constexpr std::array<FieldSpec, 2> kMeshFields{{
    {"blend", "Blend mode", FieldKind::Choice, 0, 0, "\"alpha\"", "alpha|additive|premultiplied|multiply|subtract|opaque"},
    {"align", "Alignment", FieldKind::Choice, 0, 0, "\"camera\"", "camera|velocity|ground|upright|world|local"}
}};
inline constexpr std::array<FieldSpec, 1> kRibbonFields{{
    {"hidden", "Hidden trail source", FieldKind::Boolean, 0, 0, "true"}
}};

// V2 module authoring fields. Only common primitives are typed; complex
// FxRange curves, sub-emitter handlers, custom shaders and gradients remain
// editable as full JSON without losing authored data or $node references.
inline constexpr std::array<FieldSpec,9> kNoiseModuleFields{{
    {"strength","Noise strength",FieldKind::Number,0,100,"0.3"},
    {"frequency","Frequency",FieldKind::Number,0.001,100,"1.5"},
    {"scroll","Scroll speed",FieldKind::Number,-100,100,"0"},
    {"axes","Axis weights",FieldKind::Vector3,0,0,"[1,1,1]"},
    {"octaves","Octaves",FieldKind::Integer,1,4,"1"},
    {"damping","Damping",FieldKind::Boolean,0,0,"false"},
    {"position","Position influence",FieldKind::Number,0,10,"1"},
    {"rotation","Rotation influence",FieldKind::Number,-3600,3600,"0"},
    {"size","Size influence",FieldKind::Number,0,10,"0"}
}};
inline constexpr std::array<FieldSpec,8> kCollisionModuleFields{{
    {"height","Ground offset",FieldKind::Number,-100,100,"0"},
    {"bounce","Bounce",FieldKind::Number,0,2,"0.5"},
    {"dampen","Collision damping",FieldKind::Number,0,1,"0"},
    {"lifeLoss","Lifetime loss",FieldKind::Number,0,1,"0"},
    {"minKillSpeed","Minimum kill speed",FieldKind::Number,0,1000,"0"},
    {"maxKillSpeed","Maximum kill speed",FieldKind::Number,0,100000,"0"},
    {"ground","Terrain collision",FieldKind::Boolean,0,0,"true"},
    {"radius","Particle radius multiplier",FieldKind::Number,0,10,"0"}
}};
inline constexpr std::array<FieldSpec,10> kTrailModuleFields{{
    {"time","Trail time",FieldKind::Number,0.01,10,"0.3"},
    {"lifeFraction","Lifetime ratio",FieldKind::Number,0,1,"0"},
    {"texture","Trail texture",FieldKind::String,0,0,"\"builtin:soft\""},
    {"blend","Blend mode",FieldKind::Choice,0,0,"\"additive\"","alpha|additive|premultiplied|multiply|subtract|opaque"},
    {"ratio","Trail emission ratio",FieldKind::Number,0,1,"1"},
    {"minDistance","Minimum distance",FieldKind::Number,0,100,"0"},
    {"ribbons","Ribbon count",FieldKind::Integer,1,16,"1"},
    {"dieWithParticles","Die with particle",FieldKind::Boolean,0,0,"true"},
    {"inheritColor","Inherit colour",FieldKind::Boolean,0,0,"true"},
    {"sizeAffectsWidth","Size affects width",FieldKind::Boolean,0,0,"true"}
}};
inline constexpr std::array<FieldSpec,7> kLightModuleFields{{
    {"color","Light colour",FieldKind::Color,0,0,"\"#ffcc99\""},
    {"radius","Light radius",FieldKind::Number,0.01,100,"3"},
    {"intensity","Intensity",FieldKind::Number,0,20,"1"},
    {"ratio","Emission ratio",FieldKind::Number,0,1,"1"},
    {"max","Maximum lights",FieldKind::Integer,0,64,"8"},
    {"useParticleColor","Use particle colour",FieldKind::Boolean,0,0,"true"},
    {"alphaAffectsIntensity","Alpha affects light",FieldKind::Boolean,0,0,"true"}
}};
inline constexpr std::array<FieldSpec,5> kForceModuleFields{{
    {"x","Force X",FieldKind::Number,-1000,1000,"0"},
    {"y","Force Y",FieldKind::Number,-1000,1000,"0"},
    {"z","Force Z",FieldKind::Number,-1000,1000,"0"},
    {"space","Coordinate space",FieldKind::Choice,0,0,"\"world\"","world|local"},
    {"randomize","Randomize force",FieldKind::Boolean,0,0,"false"}
}};
inline constexpr std::array<FieldSpec,6> kVelocityModuleFields{{
    {"x","Velocity X",FieldKind::Number,-1000,1000,"0"},
    {"y","Velocity Y",FieldKind::Number,-1000,1000,"0"},
    {"z","Velocity Z",FieldKind::Number,-1000,1000,"0"},
    {"radial","Radial speed",FieldKind::Number,-1000,1000,"0"},
    {"space","Coordinate space",FieldKind::Choice,0,0,"\"local\"","world|local"},
    {"orbitalOffset","Orbital offset",FieldKind::Vector3,0,0,"[0,0,0]"}
}};
inline constexpr std::array<FieldSpec,5> kShapeModuleFields{{
    {"shape","Shape",FieldKind::Choice,0,0,"\"sphere\"","point|sphere|hemisphere|disc|ring|donut|cone|box|line|mesh"},
    {"radius","Radius",FieldKind::Number,0,100,"1"},
    {"arc","Arc",FieldKind::Number,0,360,"360"},
    {"thickness","Thickness",FieldKind::Number,0,1,"1"},
    {"box","Box size",FieldKind::Vector3,0,0,"[1,1,1]"}
}};
inline constexpr std::array<FieldSpec,3> kMaterialModuleFields{{
    {"texture","Texture",FieldKind::String,0,0,"\"builtin:soft\""},
    {"blend","Blend",FieldKind::Choice,0,0,"\"additive\"","alpha|additive|premultiplied|multiply|subtract|opaque"},
    {"align","Facing",FieldKind::Choice,0,0,"\"camera\"","camera|velocity|ground|upright|world|local"}
}};
inline constexpr std::array<FieldSpec,3> kSortModuleFields{{
    {"sort","Sort policy",FieldKind::Choice,0,0,"\"auto\"","auto|none|distance|oldest|youngest"},
    {"sortOrder","Sort order",FieldKind::Integer,-100,100,"0"},
    {"minScreenSize","Min screen size",FieldKind::Number,0,1,"0"}
}};
inline constexpr std::array<FieldSpec,4> kTriggerModuleFields{{
    {"inside","Inside action",FieldKind::Choice,0,0,"\"ignore\"","ignore|kill|callback"},
    {"outside","Outside action",FieldKind::Choice,0,0,"\"ignore\"","ignore|kill|callback"},
    {"enter","Enter action",FieldKind::Choice,0,0,"\"ignore\"","ignore|kill|callback"},
    {"exit","Exit action",FieldKind::Choice,0,0,"\"ignore\"","ignore|kill|callback"}
}};
inline constexpr std::array<FieldSpec,2> kFlipbookModuleFields{{
    {"fps","Animation FPS",FieldKind::Number,0,120,"12"},
    {"cycles","Cycles",FieldKind::Number,0.01,1000,"1"}
}};
inline constexpr std::array<FieldSpec,3> kSpeedLimitModuleFields{{
    {"x","Max X speed",FieldKind::Number,0,1000000,"8"},
    {"y","Max Y speed",FieldKind::Number,0,1000000,"8"},
    {"z","Max Z speed",FieldKind::Number,0,1000000,"8"}
}};
inline constexpr std::array<FieldSpec,1> kInheritVelocityFields{{
    {"amount","Inherited velocity",FieldKind::Number,-10,10,"1"}
}};

inline std::span<const FieldSpec> fieldsForNode(std::string_view type) {
    if (type == "Spawn") return kSpawnFields;
    if (type == "Initialize") return kInitializeFields;
    if (type == "Update") return kUpdateFields;
    if (type == "SpriteOutput") return kSpriteFields;
    if (type == "MeshOutput") return kMeshFields;
    if (type == "RibbonOutput") return kRibbonFields;
    if (type == "NoiseModule") return kNoiseModuleFields;
    if (type == "CollisionModule") return kCollisionModuleFields;
    if (type == "TrailModule") return kTrailModuleFields;
    if (type == "LightModule") return kLightModuleFields;
    if (type == "ForceModule") return kForceModuleFields;
    if (type == "VelocityModule") return kVelocityModuleFields;
    if (type == "ShapeModule") return kShapeModuleFields;
    if (type == "MaterialModule") return kMaterialModuleFields;
    if (type == "SortModule") return kSortModuleFields;
    if (type == "TriggerModule") return kTriggerModuleFields;
    if (type == "FlipbookModule") return kFlipbookModuleFields;
    if (type == "SpeedLimitModule") return kSpeedLimitModuleFields;
    if (type == "InheritVelocityModule") return kInheritVelocityFields;
    return {};
}
inline const FieldSpec* fieldForNode(std::string_view type, std::string_view key) {
    for (const auto& field : fieldsForNode(type))
        if (key == field.key) return &field;
    return nullptr;
}
inline bool isChoice(const char* choices, const std::string& value) {
    const std::string_view all(choices);
    for (size_t from = 0; from <= all.size();) {
        const size_t to = all.find('|', from);
        if (all.substr(from, to == std::string_view::npos ? to : to - from) == value) return true;
        if (to == std::string_view::npos) break;
        from = to + 1;
    }
    return false;
}
inline bool validateTypedField(const FieldSpec& field, const Json& value, std::string& error) {
    error.clear();
    const auto fail = [&](const char* message) {error = std::string(field.key) + ": " + message; return false;};
    switch (field.kind) {
    case FieldKind::Boolean:
        if (value.type() != Json::Type::Bool) return fail("expected boolean");
        return true;
    case FieldKind::Color: {
        fx::float4 rgba;
        if(!fx::parseColor(value,rgba))return fail("expected hex colour or [r,g,b,a]");
        return true;
    }
    case FieldKind::String:
        if (!value.isString() || value.str().empty()) return fail("expected nonempty string");
        if (value.str().size() > 512) return fail("string is too long");
        return true;
    case FieldKind::Choice:
        if (!value.isString() || !isChoice(field.choices, value.str())) return fail("unknown enum choice");
        return true;
    case FieldKind::Number:
    case FieldKind::Integer: {
        if (!value.isNumber()) return fail("expected number");
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < field.minimum || number > field.maximum)
            return fail("value outside allowed range");
        if (field.kind == FieldKind::Integer && value.numberLiteral().find_first_of(".eE") != std::string::npos)
            return fail("expected integer literal");
        return true;
    }
    case FieldKind::Vector3:
        if (!value.isArray() || value.size() != 3) return fail("expected [x,y,z]");
        for (size_t i=0;i<3;++i)
            if (!value[i].isNumber() || !std::isfinite(value[i].toDouble()) ||
                std::abs(value[i].toDouble()) > 100000)
                return fail("invalid vector component");
        return true;
    }
    return fail("unsupported field");
}

// The typed editor must not discard unrecognized or complex modules. Changes
// are localized to a single property and never mutate the source on failure.
inline bool applyTypedField(Json& node, std::string_view key, const Json& value, std::string& error) {
    if (!node.isObject()) { error="node is not an object"; return false; }
    const FieldSpec* field=fieldForNode(node.getStr("type"),key);
    if (!field) {error="field not exposed in the typed inspector"; return false;}
    if (!validateTypedField(*field,value,error)) return false;
    Json props=node["properties"];
    if (!props.isObject()) props=Json::object();
    props.set(std::string(key),value);
    node.set("properties",std::move(props));
    return true;
}
inline bool removeTypedField(Json& node, std::string_view key, std::string& error) {
    if (!node.isObject()) {error="node is not an object";return false;}
    if (!fieldForNode(node.getStr("type"),key)) {error="unknown typed field";return false;}
    Json props=node["properties"];
    if (!props.isObject() || !props.erase(std::string(key))) {error="field is absent";return false;}
    node.set("properties",std::move(props));
    error.clear();
    return true;
}
} // namespace aiverse::spectra
