// Spine 2D skeletal animation: setup data (skeleton JSON 4.x + texture atlas), read-only and shared
// by every instance. Presentation only (floats): nothing here reaches the simulation.
//
// Formats: the Spine editor's JSON export (4.0 / 4.1; binary .skel files are converted to the same
// JSON by tools/importers/spine/skel2json.py) and the libGDX texture atlas (.atlas / .atlas.txt, old
// "xy/size/orig/offset" and new "bounds/offsets" keys). Supported: bones with every transform mode,
// slots (color, dark color ignored, blend), skins, region / mesh / linked mesh attachments (weighted
// or not), IK constraints (1 and 2 bones, softness, stretch, compress), transform constraints (world
// or local, absolute or relative), events, and animation timelines for bones, slots, IK, transform
// constraints, deform, draw order and events, with stepped / linear / bezier curves. Not supported
// (loaded, ignored, reported in warnings): path constraints, clipping, point / bounding box
// attachments (no visual), sequences (4.1).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::spine {

enum class TransformMode : uint8_t { Normal, OnlyTranslation, NoRotationOrReflection, NoScale, NoScaleOrReflection };
enum class BlendMode : uint8_t { Normal, Additive, Multiply, Screen };

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    static Color parse(const std::string& hex, Color def);   // "rrggbbaa" / "rrggbb"; def when shorter
    static Color parse(const std::string& hex);
};

struct BoneData {
    std::string name;
    int parent = -1;
    float length = 0, x = 0, y = 0, rotation = 0, scaleX = 1, scaleY = 1, shearX = 0, shearY = 0;
    TransformMode mode = TransformMode::Normal;
    bool skinRequired = false;
};

struct SlotData {
    std::string name;
    int bone = 0;
    Color color;
    std::string attachment;   // setup pose attachment ("" = none)
    BlendMode blend = BlendMode::Normal;
};

struct IkData {
    std::string name;
    int order = 0;
    std::vector<int> bones;   // 1 or 2
    int target = 0;
    float mix = 1, softness = 0;
    int bend = 1;
    bool compress = false, stretch = false, uniform = false, skinRequired = false;
};

struct TransformData {
    std::string name;
    int order = 0;
    std::vector<int> bones;
    int target = 0;
    bool local = false, relative = false, skinRequired = false;
    float offRotation = 0, offX = 0, offY = 0, offScaleX = 0, offScaleY = 0, offShearY = 0;
    float mixRotate = 1, mixX = 1, mixY = 1, mixScaleX = 1, mixScaleY = 1, mixShearY = 1;
};

struct AtlasPage {
    std::string name;            // image file, relative to the atlas file
    int width = 0, height = 0;
    bool pma = false, linear = true;
};

struct AtlasRegion {
    std::string name;
    int page = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // bounds on the page (texture space, v down)
    int x = 0, y = 0, w = 0, h = 0;          // packed size (unrotated: w, h as in the image)
    int offX = 0, offY = 0, origW = 0, origH = 0;
    int degrees = 0;                         // 0 or 90 (rotated clockwise on the page)
};

struct Atlas {
    std::vector<AtlasPage> pages;
    std::vector<AtlasRegion> regions;
    const AtlasRegion* find(const std::string& name) const;
};
// Throws std::runtime_error with a readable message.
Atlas parseAtlas(const std::string& text);

enum class AttachmentType : uint8_t { Region, Mesh, Other };

struct Attachment {
    AttachmentType type = AttachmentType::Other;
    std::string name, path;
    Color color;
    int region = -1;               // index into the atlas, -1 = missing (not drawn)
    // region: the quad corners in bone space (bl, tl, tr, br) and their uvs
    float offset[8] = {};
    float uvs4[8] = {};
    // mesh
    std::vector<float> vertices;   // unweighted: x,y pairs; weighted: as in the JSON (count, [bone, x, y, w]...)
    bool weighted = false;
    int vertexCount = 0;           // world vertices
    std::vector<float> regionUVs;  // 0..1 inside the region, as exported
    std::vector<float> uvs;        // page uvs (resolved from the region)
    std::vector<uint16_t> triangles;
    const Attachment* deformParent = nullptr;  // linked mesh: deform timelines keyed on this one
};

struct Skin {
    std::string name;
    // (slot, attachment name) -> attachment
    std::map<std::pair<int, std::string>, std::shared_ptr<Attachment>> attachments;
    std::vector<int> bones, ik, transforms;
    const Attachment* get(int slot, const std::string& name) const;
};

struct EventData {
    std::string name, string, audio;
    int intValue = 0;
    float floatValue = 0, volume = 1, balance = 0;
};

// One curve segment from key i to key i+1.
struct Curve {
    enum Type : uint8_t { Linear, Stepped, Bezier } type = Linear;
    std::vector<float> bezier;   // per channel: cx1, cy1, cx2, cy2 (absolute time / value)
};

// A keyed sequence of float channels (1 for rotate / alpha, 2 for translate / scale / shear,
// 3 / 4 for colors, 2 for IK (mix, softness), 6 for transform constraints, 1 for deform 0..1).
struct CurveTimeline {
    int channels = 1;
    std::vector<float> times;
    std::vector<float> values;   // times.size() * channels
    std::vector<Curve> curves;   // per key (curve to the next key)
    // Value of channel c at time t (t inside the timeline; before the first key = first key).
    float value(float t, int c) const;
    int frameAt(float t) const;   // last key with time <= t (-1 before the first)
};

enum class BoneProp : uint8_t { Rotate, Translate, TranslateX, TranslateY, Scale, ScaleX, ScaleY, Shear, ShearX, ShearY };
enum class SlotProp : uint8_t { Rgba, Rgb, Alpha };

struct BoneTimeline { int bone = 0; BoneProp prop = BoneProp::Rotate; CurveTimeline curve; };
struct SlotTimeline { int slot = 0; SlotProp prop = SlotProp::Rgba; CurveTimeline curve; };
struct AttachmentTimeline { int slot = 0; std::vector<float> times; std::vector<std::string> names; };
struct IkTimeline { int ik = 0; CurveTimeline curve; std::vector<int> bend; std::vector<uint8_t> compress, stretch; };
struct TransformTimeline { int constraint = 0; CurveTimeline curve; };
struct DeformTimeline {
    int slot = 0;
    const Attachment* attachment = nullptr;
    CurveTimeline curve;                       // channel 0: 0..1 between keys
    std::vector<std::vector<float>> frames;    // full deform offsets per key (vertex buffer size)
};
struct DrawOrderTimeline { std::vector<float> times; std::vector<std::vector<int>> orders; };  // empty = setup order
struct EventKey { float time = 0; int event = 0; int intValue = 0; float floatValue = 0; std::string string; };

struct Animation {
    std::string name;
    float duration = 0;
    std::vector<BoneTimeline> bones;
    std::vector<SlotTimeline> slots;
    std::vector<AttachmentTimeline> attachments;
    std::vector<IkTimeline> ik;
    std::vector<TransformTimeline> transforms;
    std::vector<DeformTimeline> deforms;
    DrawOrderTimeline drawOrder;
    std::vector<EventKey> events;
};

struct SkeletonData {
    std::string name, version;
    float x = 0, y = 0, width = 0, height = 0;
    std::vector<BoneData> bones;
    std::vector<SlotData> slots;
    std::vector<IkData> ik;
    std::vector<TransformData> transforms;
    std::vector<Skin> skins;           // [0] = default (always present, may be empty)
    std::vector<EventData> events;
    std::vector<Animation> animations;
    std::shared_ptr<Atlas> atlas;
    float scale = 1;
    std::vector<std::string> warnings; // unsupported features, missing regions

    int findBone(const std::string& n) const;
    int findSlot(const std::string& n) const;
    int findSkin(const std::string& n) const;
    int findAnimation(const std::string& n) const;
    int findEvent(const std::string& n) const;
};

// Parses a skeleton JSON text against an atlas (regions are looked up by attachment path).
// `scale` multiplies every length (as the runtimes' SkeletonJson.scale). Throws std::runtime_error.
std::shared_ptr<SkeletonData> parseSkeleton(const std::string& json, std::shared_ptr<Atlas> atlas, float scale = 1);

} // namespace aiverse::spine
