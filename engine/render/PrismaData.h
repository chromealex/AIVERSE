// Post-processing effects as data: the "postfx" block of game.json (and of a map, which overrides
// it), the catalog of effects with their parameters, presets, and a frame-cost estimate per device
// class. GPU-free (links into the editor core, aiversemcp and tests); the renderer is Prisma.h.
//
// Format (game.json, the same block in a map overrides it):
//   "postfx": {
//     "enabled": true,                       // false switches the whole chain off
//     "replace": false,                      // map only: true ignores the game's effects
//     "effects": [
//       {"type": "bloom", "intensity": 0.8, "threshold": 0.7, "minQuality": "medium"},
//       {"type": "vignette", "intensity": 0.3},
//       {"type": "lut", "texture": "postfx/warm.png"}
//     ]
//   }
// One effect per type (a later one with the same type replaces an earlier one; a map effect replaces
// the game's effect of that type, "enabled": false on it switches that effect off for the map).
// Built-in effects execute in PRISMA's fixed shader/render pipeline order
// (independent of the JSON list), including HDR Auto Exposure, MotionBlur,
// DoF, TemporalResolve and bounded ShaderGraph; see docs/prisma.md.
// Every effect has "enabled" and "minQuality"
// (low | medium | high: the effect is skipped on lower post-processing tiers; the default depends on
// how heavy the effect is). The tier is chosen per GPU (old Android: low), AIVERSE_POSTFX=off|low|medium|high.
// The whole post chain costs nothing when no effect is active (the scene renders straight to the screen).
#pragma once
#include <string>
#include <vector>
#include "engine/data/Json.h"

namespace aiverse::render {

enum class PrismaQuality { Off = 0, Low = 1, Medium = 2, High = 3 };
enum class HdrMode { Auto, Off, On };
inline bool hdrAtQuality(HdrMode mode, PrismaQuality quality, bool supported) {
    return supported && quality > PrismaQuality::Low &&
        (mode == HdrMode::On || (mode == HdrMode::Auto && quality == PrismaQuality::High));
}
enum class PrismaCostClass { Cheap, Medium, Heavy };

enum class PrismaType {
    Fxaa, Pixelate, ChromaticAberration, Sharpen, TiltShift, Bloom, SunShafts,
    ColorGrade, Lut, Posterize, Vignette, Grain, Fade, Letterbox, ShaderGraph,
    DepthOfField, AutoExposure, MotionBlur, TemporalResolve, LensWater, Count
};

enum class PrismaParamKind { Number, Color, Bool, Text };

struct PrismaParamDef {
    const char* name;
    PrismaParamKind kind;
    float def[3];          // Number/Bool: def[0]; Color: rgb
    float lo, hi;          // range (Number and every Color channel)
    const char* help;      // English, shown in MCP schemas
    const char* label;     // Russian, editor window
};

struct PrismaTypeDef {
    PrismaType type;
    const char* id;                 // "bloom"
    const char* label;              // Russian title for the editor
    const char* help;               // English one-liner (MCP)
    PrismaCostClass cost;
    PrismaQuality defaultMinQuality;
    std::vector<PrismaParamDef> params;
};

const std::vector<PrismaTypeDef>& prismaCatalog();
const PrismaTypeDef* prismaType(const std::string& id);
const PrismaTypeDef& prismaType(PrismaType t);
const char* prismaQualityName(PrismaQuality q);
bool parsePrismaQuality(const std::string& s, PrismaQuality& out);
const char* prismaCostName(PrismaCostClass c);

// One resolved effect. Values are indexed like the type's params; a Color takes 3 consecutive floats.
struct PrismaEffect {
    Json shaderGraph;
    PrismaType type = PrismaType::ColorGrade;
    bool enabled = true;
    PrismaQuality minQuality = PrismaQuality::Low;
    std::vector<float> values;      // flattened params
    std::string text;               // Text param (lut texture path)
    float get(const char* name) const;
    const float* color(const char* name) const;   // 3 floats
};

struct PrismaSettings {
    bool enabled = true;
    HdrMode hdr = HdrMode::Auto;
    std::vector<PrismaEffect> effects;   // at most one per type, catalog order
    const PrismaEffect* find(PrismaType t) const;
    // Effects that run on the given tier.
    std::vector<const PrismaEffect*> activeAt(PrismaQuality q) const;
    bool anyActive(PrismaQuality q) const { return !activeAt(q).empty(); }
};

// Sanitize native/ECS-authored settings before using them for GPU uniforms.
// Normalizes missing/extra flattened values, non-finite numbers, bounds and
// bool thresholds using the SAME per-effect catalog as JSON parsing.
// Does not compile shaders, allocate GPU resources or modify ECS state.
// Safe to call on the final camera-volume-resolved copy each frame.
void sanitizePrismaSettings(PrismaSettings& settings);

// game.json block, then the map's block (either may be null). Problems (unknown type / parameter,
// out-of-range values are clamped) are appended to `issues` when given; the result is always usable.
PrismaSettings parsePrisma(const Json& game, const Json& map, std::vector<std::string>* issues = nullptr);
// One effect object -> effect. Throws std::runtime_error with a readable message in strict mode
// (MCP validation), otherwise reports to `issues`.
PrismaEffect parsePrismaEffect(const Json& e, bool strict, std::vector<std::string>* issues = nullptr);
// Effect -> JSON with only the values that differ from the defaults (plus "type").
Json prismaEffectJson(const PrismaEffect& e);
PrismaEffect defaultPrismaEffect(PrismaType t);

// ---- presets ("cinematic", "vivid", ...): a ready list of effects
struct PrismaPreset { const char* id; const char* label; const char* help; const char* effectsJson; };
const std::vector<PrismaPreset>& prismaPresets();
const PrismaPreset* prismaPreset(const std::string& id);

// ---- frame cost
// Work is counted in "units": one full-resolution full-screen texture read per pixel. Extra passes
// add a fixed cost each (render target switch: tile store/load on mobile GPUs). The same plans are
// used by the renderer (Prisma.cpp), so the estimate follows what is really drawn.
struct PrismaDeviceProfile {
    const char* id;          // weakPhone | midPhone | desktop
    const char* label;       // "weak phone (Mali-T720 / Adreno 306, 1280x720)"
    int width, height;       // typical render size
    float msPerMpixUnit;     // ms per megapixel per unit
    float msPerPass;
    PrismaQuality tier;      // the post-processing tier such a device gets by default
};
const std::vector<PrismaDeviceProfile>& prismaProfiles();

struct PrismaWork { float units = 0; int passes = 0; };
// Work of one effect at a tier (zero when the effect doesn't run there). `forceTier` ignores minQuality.
PrismaWork prismaEffectWork(const PrismaEffect& e, PrismaQuality tier, bool ignoreMinQuality = false);
PrismaWork prismaBaseWork();              // scene to texture + final composite (paid once when any effect runs)
float prismaMs(const PrismaWork& w, const PrismaDeviceProfile& p);

// Plans shared with the renderer.
struct BloomPlan { int startDiv = 2; int levels = 4; };          // first level = size / startDiv
BloomPlan bloomPlan(PrismaQuality tier);
struct BlurPlan { int div = 2; int passes = 2; };               // tilt-shift blur
BlurPlan tiltBlurPlan(PrismaQuality tier);
struct ShaftsPlan { int div = 4; int samples = 16; };
ShaftsPlan shaftsPlan(PrismaQuality tier);

// Cost report for MCP / the editor: per effect and in total for each device profile, plus warnings
// (heavy effects, effects forced onto weak devices, a chain over budget).
Json prismaCostReport(const PrismaSettings& s);
// Warnings for one effect only (MCP add / set): heavy, or heavy forced onto low tiers.
std::vector<std::string> prismaEffectWarnings(const PrismaEffect& e);

} // namespace aiverse::render
