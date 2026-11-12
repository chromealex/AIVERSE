#include "SpineData.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include "../data/Json.h"

namespace aiverse::spine {

namespace {

float num(const Json& j, const char* key, float def) {
    const Json& v = j[key];
    return v.isNumber() ? float(v.toDouble(def)) : def;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<int> ints(const std::string& csv) {
    std::vector<int> out;
    std::stringstream ss(csv);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::atoi(trim(part).c_str()));
    return out;
}

TransformMode parseMode(const std::string& s) {
    if (s == "onlyTranslation") return TransformMode::OnlyTranslation;
    if (s == "noRotationOrReflection") return TransformMode::NoRotationOrReflection;
    if (s == "noScale") return TransformMode::NoScale;
    if (s == "noScaleOrReflection") return TransformMode::NoScaleOrReflection;
    return TransformMode::Normal;
}

BlendMode parseBlend(const std::string& s) {
    if (s == "additive") return BlendMode::Additive;
    if (s == "multiply") return BlendMode::Multiply;
    if (s == "screen") return BlendMode::Screen;
    return BlendMode::Normal;
}

// Page uv of a point of a region given in trimmed-image pixels (lx right, ly down from the top-left
// of the packed rect, in the image's own orientation). libGDX atlases rotate regions 90 degrees
// counter-clockwise ("rotate: true" / "rotate: 90").
void pageUV(const Atlas& atlas, const AtlasRegion& r, float lx, float ly, float& u, float& v) {
    const AtlasPage& p = atlas.pages[size_t(r.page)];
    float pw = float(std::max(1, p.width)), ph = float(std::max(1, p.height));
    float px, py;
    if (r.degrees == 90) { px = float(r.x) + ly; py = float(r.y) + float(r.w) - lx; }
    else { px = float(r.x) + lx; py = float(r.y) + ly; }
    u = px / pw;
    v = py / ph;
}

void readCurve(const Json& key, int channels, Curve& out) {
    const Json& c = key["curve"];
    if (c.isString() && c.str() == "stepped") { out.type = Curve::Stepped; return; }
    if (c.isArray() && c.size() >= 4) {
        out.type = Curve::Bezier;
        out.bezier.assign(size_t(channels) * 4, 0.f);
        for (size_t i = 0; i < out.bezier.size() && i < c.size(); i++) out.bezier[i] = float(c[i].toDouble());
        // fewer channels given than used (rgba2 -> rgba): repeat the last channel
        for (size_t i = c.size(); i < out.bezier.size(); i++) out.bezier[i] = out.bezier[i - 4];
    }
}

// keys -> curve timeline; `read` fills the channel values of one key.
template <class F>
void readTimeline(const Json& keys, int channels, CurveTimeline& tl, F read) {
    tl.channels = channels;
    for (const Json& k : keys.items()) {
        tl.times.push_back(num(k, "time", 0));
        size_t at = tl.values.size();
        tl.values.resize(at + size_t(channels));
        read(k, &tl.values[at]);
        Curve c;
        readCurve(k, channels, c);
        tl.curves.push_back(std::move(c));
    }
}

float lastTime(const std::vector<float>& t) { return t.empty() ? 0.f : t.back(); }

std::vector<int> drawOrderFromOffsets(const Json& offsets, const SkeletonData& d) {
    int n = int(d.slots.size());
    std::vector<int> order(size_t(n), -1), unchanged(size_t(n), 0);
    int original = 0, un = 0;
    for (const Json& o : offsets.items()) {
        int slot = d.findSlot(o.getStr("slot"));
        if (slot < 0) throw std::runtime_error("spine: draw order: unknown slot '" + o.getStr("slot") + "'");
        while (original != slot && original < n) unchanged[size_t(un++)] = original++;
        int to = original + int(o.getInt("offset", 0));
        if (to < 0 || to >= n) throw std::runtime_error("spine: draw order offset out of range");
        order[size_t(to)] = original++;
    }
    while (original < n) unchanged[size_t(un++)] = original++;
    for (int i = n - 1; i >= 0; i--)
        if (order[size_t(i)] == -1) order[size_t(i)] = unchanged[size_t(--un)];
    return order;
}

} // namespace

Color Color::parse(const std::string& hex) { return parse(hex, Color{}); }

Color Color::parse(const std::string& hex, Color def) {
    if (hex.size() < 6) return def;
    auto byte = [&](size_t i) { return float(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)) / 255.f; };
    Color c;
    c.r = byte(0); c.g = byte(2); c.b = byte(4);
    c.a = hex.size() >= 8 ? byte(6) : 1.f;
    return c;
}

const AtlasRegion* Atlas::find(const std::string& name) const {
    for (const AtlasRegion& r : regions)
        if (r.name == name) return &r;
    return nullptr;
}

Atlas parseAtlas(const std::string& text) {
    Atlas atlas;
    std::stringstream ss(text);
    std::vector<std::string> lines;
    for (std::string line; std::getline(ss, line);) lines.push_back(trim(line));
    AtlasPage* page = nullptr;
    AtlasRegion* region = nullptr;
    for (size_t li = 0; li < lines.size(); li++) {
        const std::string& t = lines[li];
        if (t.empty()) { region = nullptr; continue; }
        size_t colon = t.find(':');
        if (colon == std::string::npos) {
            // a page name is followed by its "size:" (both layouts); a region by bounds / rotate / xy
            size_t n = li + 1;
            while (n < lines.size() && lines[n].empty()) n++;
            bool isPage = !page || (n < lines.size() && lines[n].rfind("size", 0) == 0 && lines[n].find(':') != std::string::npos &&
                                    (li == 0 || lines[li - 1].empty() || region == nullptr));
            if (isPage) {
                atlas.pages.push_back({});
                page = &atlas.pages.back();
                page->name = t;
                region = nullptr;
            } else {
                atlas.regions.push_back({});
                region = &atlas.regions.back();
                region->name = t;
                region->page = int(atlas.pages.size()) - 1;
            }
            continue;
        }
        std::string key = trim(t.substr(0, colon)), val = trim(t.substr(colon + 1));
        if (!region) {
            if (!page) throw std::runtime_error("spine atlas: '" + key + "' before a page name");
            if (key == "size") { auto v = ints(val); if (v.size() >= 2) { page->width = v[0]; page->height = v[1]; } }
            else if (key == "pma") page->pma = val == "true";
            else if (key == "filter") page->linear = val.find("Nearest") == std::string::npos;
            continue;
        }
        auto v = ints(val);
        if (key == "bounds" && v.size() >= 4) { region->x = v[0]; region->y = v[1]; region->w = v[2]; region->h = v[3]; }
        else if (key == "xy" && v.size() >= 2) { region->x = v[0]; region->y = v[1]; }
        else if (key == "size" && v.size() >= 2) { region->w = v[0]; region->h = v[1]; }
        else if (key == "offsets" && v.size() >= 4) { region->offX = v[0]; region->offY = v[1]; region->origW = v[2]; region->origH = v[3]; }
        else if (key == "offset" && v.size() >= 2) { region->offX = v[0]; region->offY = v[1]; }
        else if (key == "orig" && v.size() >= 2) { region->origW = v[0]; region->origH = v[1]; }
        else if (key == "rotate") region->degrees = (val == "true" || val == "90") ? 90 : (val == "false" ? 0 : std::atoi(val.c_str()));
    }
    for (AtlasRegion& r : atlas.regions) {
        if (r.origW == 0) r.origW = r.w;
        if (r.origH == 0) r.origH = r.h;
        if (r.degrees != 0 && r.degrees != 90)
            throw std::runtime_error("spine atlas: region '" + r.name + "' rotated by " + std::to_string(r.degrees) + " degrees (only 0 / 90)");
        const AtlasPage& p = atlas.pages[size_t(r.page)];
        float pw = float(std::max(1, p.width)), ph = float(std::max(1, p.height));
        int pageW = r.degrees == 90 ? r.h : r.w, pageH = r.degrees == 90 ? r.w : r.h;
        r.u0 = float(r.x) / pw; r.v0 = float(r.y) / ph;
        r.u1 = float(r.x + pageW) / pw; r.v1 = float(r.y + pageH) / ph;
    }
    return atlas;
}

const Attachment* Skin::get(int slot, const std::string& name) const {
    auto it = attachments.find({slot, name});
    return it == attachments.end() ? nullptr : it->second.get();
}

float CurveTimeline::value(float t, int c) const {
    int i = frameAt(t);
    if (i < 0) i = 0;
    float v1 = values[size_t(i * channels + c)];
    if (size_t(i + 1) >= times.size()) return v1;
    const Curve& cv = curves[size_t(i)];
    if (cv.type == Curve::Stepped) return v1;
    float t1 = times[size_t(i)], t2 = times[size_t(i + 1)];
    float v2 = values[size_t((i + 1) * channels + c)];
    if (t2 <= t1) return v2;
    if (cv.type == Curve::Linear || cv.bezier.size() < size_t(c * 4 + 4)) return v1 + (v2 - v1) * (t - t1) / (t2 - t1);
    float cx1 = cv.bezier[size_t(c * 4)], cy1 = cv.bezier[size_t(c * 4 + 1)];
    float cx2 = cv.bezier[size_t(c * 4 + 2)], cy2 = cv.bezier[size_t(c * 4 + 3)];
    // solve x(s) = t on the cubic (t1, cx1, cx2, t2), then y(s)
    auto bez = [](float a, float b, float cc, float d, float s) {
        float u = 1 - s;
        return u * u * u * a + 3 * u * u * s * b + 3 * u * s * s * cc + s * s * s * d;
    };
    float lo = 0, hi = 1, s = (t - t1) / (t2 - t1);
    for (int it = 0; it < 24; it++) {
        float x = bez(t1, cx1, cx2, t2, s);
        if (std::fabs(x - t) < 1e-5f) break;
        if (x < t) lo = s; else hi = s;
        s = (lo + hi) * 0.5f;
    }
    return bez(v1, cy1, cy2, v2, s);
}

int CurveTimeline::frameAt(float t) const {
    // keys are few (usually < 50): binary search on the times
    auto it = std::upper_bound(times.begin(), times.end(), t);
    return int(it - times.begin()) - 1;
}

int SkeletonData::findBone(const std::string& n) const {
    for (size_t i = 0; i < bones.size(); i++) if (bones[i].name == n) return int(i);
    return -1;
}
int SkeletonData::findSlot(const std::string& n) const {
    for (size_t i = 0; i < slots.size(); i++) if (slots[i].name == n) return int(i);
    return -1;
}
int SkeletonData::findSkin(const std::string& n) const {
    for (size_t i = 0; i < skins.size(); i++) if (skins[i].name == n) return int(i);
    return -1;
}
int SkeletonData::findAnimation(const std::string& n) const {
    for (size_t i = 0; i < animations.size(); i++) if (animations[i].name == n) return int(i);
    return -1;
}
int SkeletonData::findEvent(const std::string& n) const {
    for (size_t i = 0; i < events.size(); i++) if (events[i].name == n) return int(i);
    return -1;
}

namespace {

struct Parser {
    SkeletonData& d;
    float scale;

    int bone(const std::string& n, const char* what) {
        int i = d.findBone(n);
        if (i < 0) throw std::runtime_error(std::string("spine: ") + what + ": unknown bone '" + n + "'");
        return i;
    }
    int slot(const std::string& n, const char* what) {
        int i = d.findSlot(n);
        if (i < 0) throw std::runtime_error(std::string("spine: ") + what + ": unknown slot '" + n + "'");
        return i;
    }
    void warn(const std::string& w) {
        if (std::find(d.warnings.begin(), d.warnings.end(), w) == d.warnings.end()) d.warnings.push_back(w);
    }

    void resolveRegion(Attachment& a) {
        const Atlas* atlas = d.atlas.get();
        const AtlasRegion* r = atlas ? atlas->find(a.path) : nullptr;
        if (!r) { warn("missing atlas region '" + a.path + "'"); return; }
        a.region = int(r - atlas->regions.data());
        if (a.type == AttachmentType::Mesh) {
            a.uvs.resize(a.regionUVs.size());
            for (size_t i = 0; i + 1 < a.regionUVs.size(); i += 2) {
                // regionUVs are over the original (untrimmed) image, v down
                float px = a.regionUVs[i] * float(r->origW), py = a.regionUVs[i + 1] * float(r->origH);
                float lx = px - float(r->offX), ly = py - float(r->origH - r->h - r->offY);
                pageUV(*atlas, *r, lx, ly, a.uvs[i], a.uvs[i + 1]);
            }
        } else {
            // corners bl, tl, tr, br of the trimmed rect (ly down)
            const float lx[4] = {0, 0, float(r->w), float(r->w)}, ly[4] = {float(r->h), 0, 0, float(r->h)};
            for (int i = 0; i < 4; i++) pageUV(*atlas, *r, lx[i], ly[i], a.uvs4[i * 2], a.uvs4[i * 2 + 1]);
        }
    }

    void regionOffsets(Attachment& a, float x, float y, float sx, float sy, float rot, float w, float h) {
        float ow = w, oh = h, offX = 0, offY = 0, pw = w, ph = h;
        if (a.region >= 0) {
            const AtlasRegion& r = d.atlas->regions[size_t(a.region)];
            ow = float(r.origW); oh = float(r.origH);
            offX = float(r.offX); offY = float(r.offY);
            pw = float(r.w); ph = float(r.h);
        }
        float rsx = w / std::max(ow, 1e-6f) * sx, rsy = h / std::max(oh, 1e-6f) * sy;
        float x1 = -w / 2 * sx + offX * rsx, y1 = -h / 2 * sy + offY * rsy;
        float x2 = x1 + pw * rsx, y2 = y1 + ph * rsy;
        float rad = rot * 3.14159265358979f / 180.f, c = std::cos(rad), s = std::sin(rad);
        auto put = [&](int i, float px, float py) { a.offset[i * 2] = px * c - py * s + x; a.offset[i * 2 + 1] = px * s + py * c + y; };
        put(0, x1, y1);   // bl
        put(1, x1, y2);   // tl
        put(2, x2, y2);   // tr
        put(3, x2, y1);   // br
    }

    std::shared_ptr<Attachment> attachment(const Json& j, const std::string& name, const std::string& skinName,
                                           std::vector<std::tuple<Attachment*, std::string, std::string, bool>>& links) {
        auto a = std::make_shared<Attachment>();
        a->name = j.getStr("name", name);
        a->path = j.getStr("path", a->name);
        a->color = Color::parse(j.getStr("color"));
        std::string type = j.getStr("type", "region");
        if (type == "region") {
            a->type = AttachmentType::Region;
            resolveRegion(*a);
            regionOffsets(*a, num(j, "x", 0) * scale, num(j, "y", 0) * scale, num(j, "scaleX", 1), num(j, "scaleY", 1),
                          num(j, "rotation", 0), num(j, "width", 32) * scale, num(j, "height", 32) * scale);
        } else if (type == "mesh" || type == "linkedmesh") {
            a->type = AttachmentType::Mesh;
            if (type == "linkedmesh") {
                (void)skinName;
                links.emplace_back(a.get(), j.getStr("skin", "default"), j.getStr("parent"), j["deform"].boolean(true));
                return a;
            }
            for (const Json& v : j["uvs"].items()) a->regionUVs.push_back(float(v.toDouble()));
            for (const Json& v : j["triangles"].items()) a->triangles.push_back(uint16_t(v.integer()));
            std::vector<float> verts;
            for (const Json& v : j["vertices"].items()) verts.push_back(float(v.toDouble()));
            a->vertexCount = int(a->regionUVs.size() / 2);
            a->weighted = verts.size() > a->regionUVs.size();
            if (a->weighted) {
                // count, then [bone, x, y, weight] per bone; scale x / y
                for (size_t i = 0; i < verts.size();) {
                    int n = int(verts[i]);
                    a->vertices.push_back(verts[i++]);
                    for (int k = 0; k < n && i + 3 < verts.size(); k++, i += 4) {
                        a->vertices.push_back(verts[i]);
                        a->vertices.push_back(verts[i + 1] * scale);
                        a->vertices.push_back(verts[i + 2] * scale);
                        a->vertices.push_back(verts[i + 3]);
                    }
                }
            } else {
                for (float v : verts) a->vertices.push_back(v * scale);
            }
            for (uint16_t t : a->triangles)
                if (int(t) >= a->vertexCount) throw std::runtime_error("spine: mesh '" + name + "': triangle index out of range");
            resolveRegion(*a);
        } else if (type == "clipping" || type == "path") {
            warn(type + " attachments are not drawn ('" + name + "')");
        }
        return a;
    }

    void skins(const Json& root) {
        std::vector<std::tuple<Attachment*, std::string, std::string, bool>> links;   // mesh, skin, parent, deform
        std::vector<std::pair<Attachment*, int>> linkSlots;
        auto readSkin = [&](const std::string& skinName, const Json& sj) {
            Skin* skin = nullptr;
            int existing = d.findSkin(skinName);
            if (existing >= 0) skin = &d.skins[size_t(existing)];
            else { d.skins.push_back({}); skin = &d.skins.back(); skin->name = skinName; }
            for (const Json& b : sj["bones"].items()) skin->bones.push_back(bone(b.str(), "skin"));
            for (const Json& c : sj["ik"].items()) {
                for (size_t i = 0; i < d.ik.size(); i++) if (d.ik[i].name == c.str()) skin->ik.push_back(int(i));
            }
            for (const Json& c : sj["transform"].items()) {
                for (size_t i = 0; i < d.transforms.size(); i++) if (d.transforms[i].name == c.str()) skin->transforms.push_back(int(i));
            }
            const Json& atts = sj.has("attachments") ? sj["attachments"] : sj;
            for (const auto& [slotName, list] : atts.members()) {
                int s = slot(slotName, "skin");
                for (const auto& [attName, aj] : list.members()) {
                    size_t before = links.size();
                    auto a = attachment(aj, attName, skinName, links);
                    if (links.size() > before) linkSlots.emplace_back(a.get(), s);
                    skin->attachments[{s, attName}] = a;
                }
            }
        };
        d.skins.push_back({});
        d.skins.back().name = "default";
        const Json& sk = root["skins"];
        if (sk.isArray()) {
            for (const Json& s : sk.items()) readSkin(s.getStr("name", "default"), s);
        } else if (sk.isObject()) {
            for (const auto& [name, s] : sk.members()) readSkin(name, s);   // 3.x layout
        }
        for (size_t i = 0; i < links.size(); i++) {
            auto& [mesh, skinName, parentName, inherit] = links[i];
            int s = linkSlots[i].second;
            int si = d.findSkin(skinName);
            const Attachment* parent = si >= 0 ? d.skins[size_t(si)].get(s, parentName) : nullptr;
            if (!parent || parent->type != AttachmentType::Mesh) {
                warn("linked mesh '" + mesh->name + "': parent '" + parentName + "' not found");
                continue;
            }
            mesh->regionUVs = parent->regionUVs;
            mesh->triangles = parent->triangles;
            mesh->vertices = parent->vertices;
            mesh->weighted = parent->weighted;
            mesh->vertexCount = parent->vertexCount;
            mesh->deformParent = inherit ? (parent->deformParent ? parent->deformParent : parent) : mesh;
            resolveRegion(*mesh);
        }
    }

    Animation animation(const std::string& name, const Json& a) {
        Animation anim;
        anim.name = name;
        float dur = 0;
        for (const auto& [bname, tls] : a["bones"].members()) {
            int b = bone(bname, "animation");
            for (const auto& [tname, keys] : tls.members()) {
                BoneTimeline t;
                t.bone = b;
                bool two = true;
                float def = 0, sc = 1;
                if (tname == "rotate") { t.prop = BoneProp::Rotate; two = false; }
                else if (tname == "translate") { t.prop = BoneProp::Translate; sc = scale; }
                else if (tname == "translatex") { t.prop = BoneProp::TranslateX; two = false; sc = scale; }
                else if (tname == "translatey") { t.prop = BoneProp::TranslateY; two = false; sc = scale; }
                else if (tname == "scale") { t.prop = BoneProp::Scale; def = 1; }
                else if (tname == "scalex") { t.prop = BoneProp::ScaleX; two = false; def = 1; }
                else if (tname == "scaley") { t.prop = BoneProp::ScaleY; two = false; def = 1; }
                else if (tname == "shear") t.prop = BoneProp::Shear;
                else if (tname == "shearx") { t.prop = BoneProp::ShearX; two = false; }
                else if (tname == "sheary") { t.prop = BoneProp::ShearY; two = false; }
                else { warn("unknown bone timeline '" + tname + "'"); continue; }
                readTimeline(keys, two ? 2 : 1, t.curve, [&](const Json& k, float* v) {
                    if (two) { v[0] = num(k, "x", def) * sc; v[1] = num(k, "y", def) * sc; }
                    else v[0] = num(k, "value", num(k, "angle", def)) * sc;   // "angle": 3.8 rotate keys
                });
                if (sc != 1) for (Curve& c : t.curve.curves)
                    for (size_t i = 1; i < c.bezier.size(); i += 2) c.bezier[i] *= sc;
                dur = std::max(dur, lastTime(t.curve.times));
                anim.bones.push_back(std::move(t));
            }
        }
        for (const auto& [sname, tls] : a["slots"].members()) {
            int s = slot(sname, "animation");
            for (const auto& [tname, keys] : tls.members()) {
                if (tname == "attachment") {
                    AttachmentTimeline t;
                    t.slot = s;
                    for (const Json& k : keys.items()) {
                        t.times.push_back(num(k, "time", 0));
                        t.names.push_back(k["name"].str());
                    }
                    dur = std::max(dur, lastTime(t.times));
                    anim.attachments.push_back(std::move(t));
                    continue;
                }
                SlotTimeline t;
                t.slot = s;
                if (tname == "alpha") {
                    t.prop = SlotProp::Alpha;
                    readTimeline(keys, 1, t.curve, [&](const Json& k, float* v) { v[0] = num(k, "value", 1); });
                } else if (tname == "rgb" || tname == "rgb2") {
                    t.prop = SlotProp::Rgb;
                    readTimeline(keys, 3, t.curve, [&](const Json& k, float* v) {
                        Color c = Color::parse(k.getStr(tname == "rgb" ? "color" : "light", "ffffff"));
                        v[0] = c.r; v[1] = c.g; v[2] = c.b;
                    });
                } else if (tname == "rgba" || tname == "color" || tname == "rgba2" || tname == "twoColor") {
                    t.prop = SlotProp::Rgba;
                    bool light = tname == "rgba2" || tname == "twoColor";
                    readTimeline(keys, 4, t.curve, [&](const Json& k, float* v) {
                        Color c = Color::parse(k.getStr(light ? "light" : "color", "ffffffff"));
                        v[0] = c.r; v[1] = c.g; v[2] = c.b; v[3] = c.a;
                    });
                } else {
                    warn("unknown slot timeline '" + tname + "'");
                    continue;
                }
                dur = std::max(dur, lastTime(t.curve.times));
                anim.slots.push_back(std::move(t));
            }
        }
        for (const auto& [cname, keys] : a["ik"].members()) {
            IkTimeline t;
            t.ik = -1;
            for (size_t i = 0; i < d.ik.size(); i++) if (d.ik[i].name == cname) t.ik = int(i);
            if (t.ik < 0) throw std::runtime_error("spine: animation '" + name + "': unknown IK constraint '" + cname + "'");
            readTimeline(keys, 2, t.curve, [&](const Json& k, float* v) {
                v[0] = num(k, "mix", 1); v[1] = num(k, "softness", 0) * scale;
                t.bend.push_back(k["bendPositive"].boolean(true) ? 1 : -1);
                t.compress.push_back(k["compress"].boolean(false));
                t.stretch.push_back(k["stretch"].boolean(false));
            });
            dur = std::max(dur, lastTime(t.curve.times));
            anim.ik.push_back(std::move(t));
        }
        for (const auto& [cname, keys] : a["transform"].members()) {
            TransformTimeline t;
            t.constraint = -1;
            for (size_t i = 0; i < d.transforms.size(); i++) if (d.transforms[i].name == cname) t.constraint = int(i);
            if (t.constraint < 0) throw std::runtime_error("spine: animation '" + name + "': unknown transform constraint '" + cname + "'");
            readTimeline(keys, 6, t.curve, [&](const Json& k, float* v) {
                v[0] = num(k, "mixRotate", 1);
                v[1] = num(k, "mixX", 1);
                v[2] = num(k, "mixY", v[1]);
                v[3] = num(k, "mixScaleX", 1);
                v[4] = num(k, "mixScaleY", v[3]);
                v[5] = num(k, "mixShearY", 1);
            });
            dur = std::max(dur, lastTime(t.curve.times));
            anim.transforms.push_back(std::move(t));
        }
        if (a.has("path")) warn("path constraint timelines are ignored");
        auto deformKeys = [&](const std::string& skinName, const std::string& slotName, const std::string& attName, const Json& keys) {
            int si = d.findSkin(skinName);
            int s = slot(slotName, "deform");
            const Attachment* att = si >= 0 ? d.skins[size_t(si)].get(s, attName) : nullptr;
            if (!att || att->type != AttachmentType::Mesh) { warn("deform: no mesh '" + attName + "'"); return; }
            DeformTimeline t;
            t.slot = s;
            t.attachment = att->deformParent ? att->deformParent : att;
            size_t len = att->weighted ? 0 : att->vertices.size();
            if (att->weighted)
                for (size_t i = 0; i < att->vertices.size();) { int n = int(att->vertices[i]); len += size_t(n) * 2; i += 1 + size_t(n) * 4; }
            readTimeline(keys, 1, t.curve, [&](const Json& k, float* v) {
                v[0] = float(t.frames.size());
                std::vector<float> f(len, 0.f);
                size_t off = size_t(k.getInt("offset", 0));
                const Json& vs = k["vertices"];
                for (size_t i = 0; i < vs.size() && off + i < len; i++) f[off + i] = float(vs[i].toDouble()) * scale;
                if (!att->weighted && vs.size() > 0) for (size_t i = 0; i < len; i++) f[i] += att->vertices[i];
                if (!att->weighted && vs.size() == 0) f = att->vertices;
                t.frames.push_back(std::move(f));
            });
            // channel value = key index + fraction to the next key (bezier y is 0..1 per segment)
            for (size_t i = 0; i < t.curve.curves.size(); i++)
                for (size_t c = 1; c < t.curve.curves[i].bezier.size(); c += 2) t.curve.curves[i].bezier[c] += float(i);
            dur = std::max(dur, lastTime(t.curve.times));
            anim.deforms.push_back(std::move(t));
        };
        for (const auto& [skinName, slots] : a["deform"].members())
            for (const auto& [slotName, atts] : slots.members())
                for (const auto& [attName, keys] : atts.members()) deformKeys(skinName, slotName, attName, keys);
        for (const auto& [skinName, slots] : a["attachments"].members())   // 4.1 layout
            for (const auto& [slotName, atts] : slots.members())
                for (const auto& [attName, tls] : atts.members()) {
                    if (tls.has("deform")) deformKeys(skinName, slotName, attName, tls["deform"]);
                    if (tls.has("sequence")) warn("sequence timelines are ignored");
                }
        const Json& dorder = a.has("drawOrder") ? a["drawOrder"] : a["draworder"];
        for (const Json& k : dorder.items()) {
            anim.drawOrder.times.push_back(num(k, "time", 0));
            anim.drawOrder.orders.push_back(k.has("offsets") ? drawOrderFromOffsets(k["offsets"], d) : std::vector<int>{});
            dur = std::max(dur, anim.drawOrder.times.back());
        }
        for (const Json& k : a["events"].items()) {
            EventKey e;
            e.time = num(k, "time", 0);
            e.event = d.findEvent(k.getStr("name"));
            if (e.event < 0) throw std::runtime_error("spine: animation '" + name + "': unknown event '" + k.getStr("name") + "'");
            const EventData& ed = d.events[size_t(e.event)];
            e.intValue = int(k.getInt("int", ed.intValue));
            e.floatValue = num(k, "float", ed.floatValue);
            e.string = k.getStr("string", ed.string);
            dur = std::max(dur, e.time);
            anim.events.push_back(std::move(e));
        }
        anim.duration = dur;
        return anim;
    }
};

} // namespace

std::shared_ptr<SkeletonData> parseSkeleton(const std::string& text, std::shared_ptr<Atlas> atlas, float scale) {
    Json root;
    try { root = Json::parse(text); }
    catch (const std::exception& e) { throw std::runtime_error(std::string("spine: bad JSON: ") + e.what()); }
    auto data = std::make_shared<SkeletonData>();
    SkeletonData& d = *data;
    d.atlas = std::move(atlas);
    d.scale = scale;
    Parser p{d, scale};
    const Json& sk = root["skeleton"];
    d.version = sk.getStr("spine");
    if (!d.version.empty() && d.version[0] < '4')
        p.warn("skeleton exported by Spine " + d.version + ": curves are read as 4.x (re-export from Spine 4)");
    d.x = num(sk, "x", 0); d.y = num(sk, "y", 0); d.width = num(sk, "width", 0); d.height = num(sk, "height", 0);
    for (const Json& b : root["bones"].items()) {
        BoneData bd;
        bd.name = b.getStr("name");
        if (b.has("parent")) bd.parent = p.bone(b.getStr("parent"), "bone");
        else if (!d.bones.empty()) throw std::runtime_error("spine: bone '" + bd.name + "' has no parent");
        bd.length = num(b, "length", 0) * scale;
        bd.x = num(b, "x", 0) * scale; bd.y = num(b, "y", 0) * scale;
        bd.rotation = num(b, "rotation", 0);
        bd.scaleX = num(b, "scaleX", 1); bd.scaleY = num(b, "scaleY", 1);
        bd.shearX = num(b, "shearX", 0); bd.shearY = num(b, "shearY", 0);
        bd.mode = parseMode(b.getStr("transform", b.getStr("inheritRotation") == "false" ? "noRotationOrReflection" : "normal"));
        bd.skinRequired = b.getBool("skin", false);
        d.bones.push_back(std::move(bd));
    }
    if (d.bones.empty()) throw std::runtime_error("spine: skeleton has no bones");
    for (const Json& s : root["slots"].items()) {
        SlotData sd;
        sd.name = s.getStr("name");
        sd.bone = p.bone(s.getStr("bone"), "slot");
        sd.color = Color::parse(s.getStr("color"));
        sd.attachment = s["attachment"].str();
        sd.blend = parseBlend(s.getStr("blend", "normal"));
        d.slots.push_back(std::move(sd));
    }
    for (const Json& c : root["ik"].items()) {
        IkData ik;
        ik.name = c.getStr("name");
        ik.order = int(c.getInt("order", 0));
        for (const Json& b : c["bones"].items()) ik.bones.push_back(p.bone(b.str(), "ik"));
        if (ik.bones.empty() || ik.bones.size() > 2) throw std::runtime_error("spine: IK '" + ik.name + "' needs 1 or 2 bones");
        ik.target = p.bone(c.getStr("target"), "ik");
        ik.mix = num(c, "mix", 1);
        ik.softness = num(c, "softness", 0) * scale;
        ik.bend = c["bendPositive"].boolean(true) ? 1 : -1;
        ik.compress = c.getBool("compress", false);
        ik.stretch = c.getBool("stretch", false);
        ik.uniform = c.getBool("uniform", false);
        ik.skinRequired = c.getBool("skin", false);
        d.ik.push_back(std::move(ik));
    }
    for (const Json& c : root["transform"].items()) {
        TransformData t;
        t.name = c.getStr("name");
        t.order = int(c.getInt("order", 0));
        for (const Json& b : c["bones"].items()) t.bones.push_back(p.bone(b.str(), "transform"));
        t.target = p.bone(c.getStr("target"), "transform");
        t.local = c.getBool("local", false);
        t.relative = c.getBool("relative", false);
        t.offRotation = num(c, "rotation", 0);
        t.offX = num(c, "x", 0) * scale; t.offY = num(c, "y", 0) * scale;
        t.offScaleX = num(c, "scaleX", 0); t.offScaleY = num(c, "scaleY", 0); t.offShearY = num(c, "shearY", 0);
        t.mixRotate = num(c, "mixRotate", num(c, "rotateMix", 1));
        t.mixX = num(c, "mixX", num(c, "translateMix", 1));
        t.mixY = num(c, "mixY", t.mixX);
        t.mixScaleX = num(c, "mixScaleX", num(c, "scaleMix", 1));
        t.mixScaleY = num(c, "mixScaleY", t.mixScaleX);
        t.mixShearY = num(c, "mixShearY", num(c, "shearMix", 1));
        t.skinRequired = c.getBool("skin", false);
        d.transforms.push_back(std::move(t));
    }
    if (root["path"].size() > 0) p.warn("path constraints are ignored");
    for (const auto& [name, e] : root["events"].members()) {
        EventData ed;
        ed.name = name;
        ed.intValue = int(e.getInt("int", 0));
        ed.floatValue = num(e, "float", 0);
        ed.string = e.getStr("string");
        ed.audio = e.getStr("audio");
        ed.volume = num(e, "volume", 1);
        ed.balance = num(e, "balance", 0);
        d.events.push_back(std::move(ed));
    }
    p.skins(root);
    for (const auto& [name, a] : root["animations"].members()) d.animations.push_back(p.animation(name, a));
    return data;
}

} // namespace aiverse::spine
