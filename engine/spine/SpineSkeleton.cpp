#include "SpineSkeleton.h"
#include <algorithm>
#include <cmath>

namespace aiverse::spine {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegRad = kPi / 180.f, kRadDeg = 180.f / kPi;

float cosDeg(float d) { return std::cos(d * kDegRad); }
float sinDeg(float d) { return std::sin(d * kDegRad); }
float atan2Deg(float y, float x) { return std::atan2(y, x) * kRadDeg; }
float wrapDeg(float r) { r = std::fmod(r, 360.f); if (r > 180) r -= 360; else if (r < -180) r += 360; return r; }
float wrapRad(float r) { if (r > kPi) r -= 2 * kPi; else if (r < -kPi) r += 2 * kPi; return r; }

uint32_t packColor(float r, float g, float b, float a) {
    auto c = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

} // namespace

float animationTime(const Animation& a, float t, bool loop) {
    if (a.duration <= 0) return 0;
    if (loop) { t = std::fmod(t, a.duration); return t < 0 ? t + a.duration : t; }
    return std::min(t, a.duration);
}

// ---------------------------------------------------------------- Skeleton

Skeleton::Skeleton(std::shared_ptr<const SkeletonData> data) : data_(std::move(data)) {
    const SkeletonData& d = *data_;
    bones.resize(d.bones.size());
    slots.resize(d.slots.size());
    children_.resize(d.bones.size());
    for (size_t i = 1; i < d.bones.size(); i++)
        if (d.bones[i].parent >= 0) children_[size_t(d.bones[i].parent)].push_back(int(i));
    ikMix.resize(d.ik.size()); ikSoftness.resize(d.ik.size()); ikBend.resize(d.ik.size());
    ikCompress.resize(d.ik.size()); ikStretch.resize(d.ik.size());
    tcMix.resize(d.transforms.size() * 6);
    std::vector<std::pair<int, std::pair<int, int>>> order;
    for (size_t i = 0; i < d.ik.size(); i++) order.push_back({d.ik[i].order, {0, int(i)}});
    for (size_t i = 0; i < d.transforms.size(); i++) order.push_back({d.transforms[i].order, {1, int(i)}});
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& o : order) constraints_.push_back(o.second);
    updateActive();
    setToSetupPose();
}

bool Skeleton::setSkin(const std::string& name) {
    int s = data_->findSkin(name);
    if (s < 0) return false;
    skin_ = s;
    updateActive();
    // attachments named in the setup pose come from the new skin
    for (size_t i = 0; i < slots.size(); i++) {
        const std::string& n = data_->slots[i].attachment;
        if (!n.empty()) slots[i].attachment = attachment(int(i), n);
    }
    return true;
}

const std::string& Skeleton::skinName() const { return data_->skins[size_t(skin_)].name; }

void Skeleton::updateActive() {
    const SkeletonData& d = *data_;
    const Skin& sk = d.skins[size_t(skin_)];
    for (size_t i = 0; i < bones.size(); i++) {
        bool req = d.bones[i].skinRequired;
        bones[i].active = !req || std::find(sk.bones.begin(), sk.bones.end(), int(i)) != sk.bones.end();
        int p = d.bones[i].parent;
        if (p >= 0 && !bones[size_t(p)].active) bones[i].active = false;
    }
}

const Attachment* Skeleton::attachment(int slot, const std::string& name) const {
    if (name.empty()) return nullptr;
    if (skin_ != 0)
        if (const Attachment* a = data_->skins[size_t(skin_)].get(slot, name)) return a;
    return data_->skins[0].get(slot, name);
}

void Skeleton::setToSetupPose() { setBonesToSetupPose(); setSlotsToSetupPose(); }

void Skeleton::setBonesToSetupPose() {
    const SkeletonData& d = *data_;
    for (size_t i = 0; i < bones.size(); i++) {
        const BoneData& b = d.bones[i];
        Bone& o = bones[i];
        o.x = b.x; o.y = b.y; o.rotation = b.rotation; o.scaleX = b.scaleX; o.scaleY = b.scaleY; o.shearX = b.shearX; o.shearY = b.shearY;
    }
    for (size_t i = 0; i < d.ik.size(); i++) {
        ikMix[i] = d.ik[i].mix; ikSoftness[i] = d.ik[i].softness; ikBend[i] = d.ik[i].bend;
        ikCompress[i] = d.ik[i].compress; ikStretch[i] = d.ik[i].stretch;
    }
    for (size_t i = 0; i < d.transforms.size(); i++) {
        const TransformData& t = d.transforms[i];
        float* m = &tcMix[i * 6];
        m[0] = t.mixRotate; m[1] = t.mixX; m[2] = t.mixY; m[3] = t.mixScaleX; m[4] = t.mixScaleY; m[5] = t.mixShearY;
    }
}

void Skeleton::setSlotsToSetupPose() {
    const SkeletonData& d = *data_;
    drawOrder.resize(slots.size());
    for (size_t i = 0; i < slots.size(); i++) {
        drawOrder[i] = int(i);
        slots[i].color = d.slots[i].color;
        slots[i].attachment = attachment(int(i), d.slots[i].attachment);
        slots[i].deform.clear();
    }
}

void Skeleton::boneWorld(int i, float x, float y, float rot, float sx, float sy, float shx, float shy) {
    Bone& o = bones[size_t(i)];
    o.ax = x; o.ay = y; o.arotation = rot; o.ascaleX = sx; o.ascaleY = sy; o.ashearX = shx; o.ashearY = shy;
    int pi = data_->bones[size_t(i)].parent;
    if (pi < 0) {
        float ry = rot + 90 + shy;
        o.a = cosDeg(rot + shx) * sx * scaleX;
        o.b = cosDeg(ry) * sy * scaleX;
        o.c = sinDeg(rot + shx) * sx * scaleY;
        o.d = sinDeg(ry) * sy * scaleY;
        o.worldX = x * scaleX + this->x;
        o.worldY = y * scaleY + this->y;
        return;
    }
    const Bone& p = bones[size_t(pi)];
    float pa = p.a, pb = p.b, pc = p.c, pd = p.d;
    o.worldX = pa * x + pb * y + p.worldX;
    o.worldY = pc * x + pd * y + p.worldY;
    switch (data_->bones[size_t(i)].mode) {
    case TransformMode::Normal: {
        float ry = rot + 90 + shy;
        float la = cosDeg(rot + shx) * sx, lb = cosDeg(ry) * sy, lc = sinDeg(rot + shx) * sx, ld = sinDeg(ry) * sy;
        o.a = pa * la + pb * lc; o.b = pa * lb + pb * ld; o.c = pc * la + pd * lc; o.d = pc * lb + pd * ld;
        return;
    }
    case TransformMode::OnlyTranslation: {
        float ry = rot + 90 + shy;
        o.a = cosDeg(rot + shx) * sx; o.b = cosDeg(ry) * sy; o.c = sinDeg(rot + shx) * sx; o.d = sinDeg(ry) * sy;
        break;
    }
    case TransformMode::NoRotationOrReflection: {
        float s = pa * pa + pc * pc, prx;
        if (s > 0.0001f) {
            s = std::fabs(pa * pd - pb * pc) / s;
            pa /= scaleX; pc /= scaleY;
            pb = pc * s; pd = pa * s;
            prx = atan2Deg(pc, pa);
        } else {
            pa = 0; pc = 0;
            prx = 90 - atan2Deg(pd, pb);
        }
        float rx = rot + shx - prx, ry = rot + shy - prx + 90;
        float la = cosDeg(rx) * sx, lb = cosDeg(ry) * sy, lc = sinDeg(rx) * sx, ld = sinDeg(ry) * sy;
        o.a = pa * la - pb * lc; o.b = pa * lb - pb * ld; o.c = pc * la + pd * lc; o.d = pc * lb + pd * ld;
        break;
    }
    case TransformMode::NoScale:
    case TransformMode::NoScaleOrReflection: {
        float cs = cosDeg(rot), sn = sinDeg(rot);
        float za = (pa * cs + pb * sn) / scaleX, zc = (pc * cs + pd * sn) / scaleY;
        float s = std::sqrt(za * za + zc * zc);
        if (s > 0.00001f) s = 1 / s;
        za *= s; zc *= s;
        s = std::sqrt(za * za + zc * zc);
        if (data_->bones[size_t(i)].mode == TransformMode::NoScale && ((pa * pd - pb * pc < 0) != ((scaleX < 0) != (scaleY < 0)))) s = -s;
        float r = kPi / 2 + std::atan2(zc, za);
        float zb = std::cos(r) * s, zd = std::sin(r) * s;
        float la = cosDeg(shx) * sx, lb = cosDeg(90 + shy) * sy, lc = sinDeg(shx) * sx, ld = sinDeg(90 + shy) * sy;
        o.a = za * la + zb * lc; o.b = za * lb + zb * ld; o.c = zc * la + zd * lc; o.d = zc * lb + zd * ld;
        break;
    }
    }
    o.a *= scaleX; o.b *= scaleX; o.c *= scaleY; o.d *= scaleY;
}

void Skeleton::updateApplied(int i) {
    Bone& o = bones[size_t(i)];
    int pi = data_->bones[size_t(i)].parent;
    if (pi < 0) {
        o.ax = o.worldX - x; o.ay = o.worldY - y;
        o.arotation = atan2Deg(o.c, o.a);
        o.ascaleX = std::sqrt(o.a * o.a + o.c * o.c);
        o.ascaleY = std::sqrt(o.b * o.b + o.d * o.d);
        o.ashearX = 0;
        o.ashearY = atan2Deg(o.a * o.b + o.c * o.d, o.a * o.d - o.b * o.c);
        return;
    }
    const Bone& p = bones[size_t(pi)];
    float pa = p.a, pb = p.b, pc = p.c, pd = p.d, pid = 1 / (pa * pd - pb * pc);
    float dx = o.worldX - p.worldX, dy = o.worldY - p.worldY;
    o.ax = dx * pd * pid - dy * pb * pid;
    o.ay = dy * pa * pid - dx * pc * pid;
    float ia = pid * pd, id = pid * pa, ib = pid * pb, ic = pid * pc;
    float ra = ia * o.a - ib * o.c, rb = ia * o.b - ib * o.d, rc = id * o.c - ic * o.a, rd = id * o.d - ic * o.b;
    o.ashearX = 0;
    o.ascaleX = std::sqrt(ra * ra + rc * rc);
    if (o.ascaleX > 0.0001f) {
        float det = ra * rd - rb * rc;
        o.ascaleY = det / o.ascaleX;
        o.ashearY = -atan2Deg(ra * rb + rc * rd, det);
        o.arotation = atan2Deg(rc, ra);
    } else {
        o.ascaleX = 0;
        o.ascaleY = std::sqrt(rb * rb + rd * rd);
        o.ashearY = 0;
        o.arotation = 90 - atan2Deg(rd, rb);
    }
}

void Skeleton::updateChildren(int i) {
    for (int c : children_[size_t(i)]) {
        Bone& b = bones[size_t(c)];
        boneWorld(c, b.ax, b.ay, b.arotation, b.ascaleX, b.ascaleY, b.ashearX, b.ashearY);
        updateChildren(c);
    }
}

void Skeleton::updateWorldTransform() {
    for (size_t i = 0; i < bones.size(); i++) {
        Bone& b = bones[i];
        boneWorld(int(i), b.x, b.y, b.rotation, b.scaleX, b.scaleY, b.shearX, b.shearY);
    }
    const Skin& sk = data_->skins[size_t(skin_)];
    for (auto [kind, idx] : constraints_) {
        if (kind == 0) {
            const IkData& c = data_->ik[size_t(idx)];
            if (c.skinRequired && std::find(sk.ik.begin(), sk.ik.end(), idx) == sk.ik.end()) continue;
            applyIk(idx);
        } else {
            const TransformData& c = data_->transforms[size_t(idx)];
            if (c.skinRequired && std::find(sk.transforms.begin(), sk.transforms.end(), idx) == sk.transforms.end()) continue;
            applyTransform(idx);
        }
    }
}

void Skeleton::applyIk(int ci) {
    const IkData& c = data_->ik[size_t(ci)];
    float mix = ikMix[size_t(ci)];
    if (mix == 0) return;
    const Bone& t = bones[size_t(c.target)];
    if (c.bones.size() == 1) {
        ik1(c.bones[0], t.worldX, t.worldY, ikCompress[size_t(ci)], ikStretch[size_t(ci)], c.uniform, mix);
        updateChildren(c.bones[0]);
    } else {
        ik2(c.bones[0], c.bones[1], t.worldX, t.worldY, ikBend[size_t(ci)], ikStretch[size_t(ci)], c.uniform, ikSoftness[size_t(ci)], mix);
        updateChildren(c.bones[0]);
    }
}

void Skeleton::ik1(int bi, float targetX, float targetY, bool compress, bool stretch, bool uniform, float alpha) {
    Bone& bone = bones[size_t(bi)];
    int pi = data_->bones[size_t(bi)].parent;
    if (pi < 0) return;
    const Bone& p = bones[size_t(pi)];
    float pa = p.a, pb = p.b, pc = p.c, pd = p.d;
    float rotationIK = -bone.ashearX - bone.arotation, tx = 0, ty = 0;
    TransformMode mode = data_->bones[size_t(bi)].mode;
    if (mode == TransformMode::OnlyTranslation) {
        tx = targetX - bone.worldX; ty = targetY - bone.worldY;
    } else {
        if (mode == TransformMode::NoRotationOrReflection) {
            float s = std::fabs(pa * pd - pb * pc) / std::max(pa * pa + pc * pc, 1e-9f);
            float sa = pa / scaleX, sc = pc / scaleY;
            pb = -sc * s * scaleX; pd = sa * s * scaleY;
            rotationIK += atan2Deg(sc, sa);
        }
        float x = targetX - p.worldX, y = targetY - p.worldY, d = pa * pd - pb * pc;
        if (std::fabs(d) < 1e-9f) return;
        tx = (x * pd - y * pb) / d - bone.ax;
        ty = (y * pa - x * pc) / d - bone.ay;
    }
    rotationIK += atan2Deg(ty, tx);
    if (bone.ascaleX < 0) rotationIK += 180;
    rotationIK = wrapDeg(rotationIK);
    float sx = bone.ascaleX, sy = bone.ascaleY;
    if (compress || stretch) {
        if (mode == TransformMode::NoScale || mode == TransformMode::NoScaleOrReflection) { tx = targetX - bone.worldX; ty = targetY - bone.worldY; }
        float b = data_->bones[size_t(bi)].length * sx, dd = std::sqrt(tx * tx + ty * ty);
        if (((compress && dd < b) || (stretch && dd > b)) && b > 0.0001f) {
            float s = (dd / b - 1) * alpha + 1;
            sx *= s;
            if (uniform) sy *= s;
        }
    }
    boneWorld(bi, bone.ax, bone.ay, bone.arotation + rotationIK * alpha, sx, sy, bone.ashearX, bone.ashearY);
}

void Skeleton::ik2(int pi, int ci, float targetX, float targetY, int bendDir, bool stretch, bool uniform, float softness, float alpha) {
    Bone& parent = bones[size_t(pi)];
    Bone& child = bones[size_t(ci)];
    int ppi = data_->bones[size_t(pi)].parent;
    if (ppi < 0) return;
    float px = parent.ax, py = parent.ay, psx = parent.ascaleX, psy = parent.ascaleY, sx = psx, sy = psy, csx = child.ascaleX;
    float os1, os2, s2;
    if (psx < 0) { psx = -psx; os1 = 180; s2 = -1; } else { os1 = 0; s2 = 1; }
    if (psy < 0) { psy = -psy; s2 = -s2; }
    if (csx < 0) { csx = -csx; os2 = 180; } else os2 = 0;
    float cx = child.ax, cy, cwx, cwy, a = parent.a, b = parent.b, c = parent.c, d = parent.d;
    bool u = std::fabs(psx - psy) <= 0.0001f;
    if (!u || stretch) { cy = 0; cwx = a * cx + parent.worldX; cwy = c * cx + parent.worldY; }
    else { cy = child.ay; cwx = a * cx + b * cy + parent.worldX; cwy = c * cx + d * cy + parent.worldY; }
    const Bone& pp = bones[size_t(ppi)];
    a = pp.a; b = pp.b; c = pp.c; d = pp.d;
    float det = a * d - b * c;
    if (std::fabs(det) < 1e-9f) return;
    float id = 1 / det, x = cwx - pp.worldX, y = cwy - pp.worldY;
    float dx = (x * d - y * b) * id - px, dy = (y * a - x * c) * id - py;
    float l1 = std::sqrt(dx * dx + dy * dy), l2 = data_->bones[size_t(ci)].length * csx, a1, a2;
    if (l1 < 0.0001f) {
        ik1(pi, targetX, targetY, false, stretch, false, alpha);
        boneWorld(ci, cx, cy, 0, child.ascaleX, child.ascaleY, child.ashearX, child.ashearY);
        return;
    }
    x = targetX - pp.worldX; y = targetY - pp.worldY;
    float tx = (x * d - y * b) * id - px, ty = (y * a - x * c) * id - py;
    float dd = tx * tx + ty * ty;
    if (softness != 0) {
        softness *= psx * (csx + 1) * 0.5f;
        float td = std::sqrt(dd), sd = td - l1 - l2 * psx + softness;
        if (sd > 0) {
            float p = std::min(1.f, sd / (softness * 2)) - 1;
            p = (sd - softness * (1 - p * p)) / td;
            tx -= p * tx; ty -= p * ty;
            dd = tx * tx + ty * ty;
        }
    }
    if (u) {
        l2 *= psx;
        float cs = (dd - l1 * l1 - l2 * l2) / (2 * l1 * l2);
        if (cs < -1) { cs = -1; a2 = kPi * float(bendDir); }
        else if (cs > 1) {
            cs = 1; a2 = 0;
            if (stretch) { a = (std::sqrt(dd) / (l1 + l2) - 1) * alpha + 1; sx *= a; if (uniform) sy *= a; }
        } else a2 = std::acos(cs) * float(bendDir);
        a = l1 + l2 * cs;
        b = l2 * std::sin(a2);
        a1 = std::atan2(ty * a - tx * b, tx * a + ty * b);
    } else {
        a = psx * l2; b = psy * l2;
        float aa = a * a, bb = b * b, ta = std::atan2(ty, tx);
        c = bb * l1 * l1 + aa * dd - aa * bb;
        float c1 = -2 * bb * l1, c2 = bb - aa;
        d = c1 * c1 - 4 * c2 * c;
        bool done = false;
        if (d >= 0 && std::fabs(c2) > 1e-12f) {
            float q = std::sqrt(d);
            if (c1 < 0) q = -q;
            q = -(c1 + q) * 0.5f;
            float r0 = q / c2, r1 = c / q;
            float r = std::fabs(r0) < std::fabs(r1) ? r0 : r1;
            if (r * r <= dd) {
                y = std::sqrt(dd - r * r) * float(bendDir);
                a1 = ta - std::atan2(y, r);
                a2 = std::atan2(y / psy, (r - l1) / psx);
                done = true;
            }
        }
        if (!done) {
            float minAngle = kPi, minX = l1 - a, minDist = minX * minX, minY = 0;
            float maxAngle = 0, maxX = l1 + a, maxDist = maxX * maxX, maxY = 0;
            c = -a * l1 / (aa - bb);
            if (c >= -1 && c <= 1) {
                c = std::acos(c);
                x = a * std::cos(c) + l1;
                y = b * std::sin(c);
                d = x * x + y * y;
                if (d < minDist) { minAngle = c; minDist = d; minX = x; minY = y; }
                if (d > maxDist) { maxAngle = c; maxDist = d; maxX = x; maxY = y; }
            }
            if (dd <= (minDist + maxDist) * 0.5f) { a1 = ta - std::atan2(minY * float(bendDir), minX); a2 = minAngle * float(bendDir); }
            else { a1 = ta - std::atan2(maxY * float(bendDir), maxX); a2 = maxAngle * float(bendDir); }
        }
    }
    float os = std::atan2(cy, cx) * s2;
    float rotation = parent.arotation;
    a1 = wrapDeg((a1 - os) * kRadDeg + os1 - rotation);
    boneWorld(pi, px, py, rotation + a1 * alpha, sx, sy, 0, 0);
    rotation = child.arotation;
    a2 = wrapDeg(((a2 + os) * kRadDeg - child.ashearX) * s2 + os2 - rotation);
    boneWorld(ci, cx, cy, rotation + a2 * alpha, child.ascaleX, child.ascaleY, child.ashearX, child.ashearY);
}

void Skeleton::applyTransform(int ci) {
    const TransformData& c = data_->transforms[size_t(ci)];
    const float* m = &tcMix[size_t(ci) * 6];
    float mixRotate = m[0], mixX = m[1], mixY = m[2], mixScaleX = m[3], mixScaleY = m[4], mixShearY = m[5];
    if (mixRotate == 0 && mixX == 0 && mixY == 0 && mixScaleX == 0 && mixScaleY == 0 && mixShearY == 0) return;
    const Bone& t = bones[size_t(c.target)];
    float ta = t.a, tb = t.b, tc = t.c, td = t.d;
    float degRadReflect = ta * td - tb * tc > 0 ? kDegRad : -kDegRad;
    float offRot = c.offRotation * degRadReflect, offShearY = c.offShearY * degRadReflect;
    bool translate = mixX != 0 || mixY != 0;
    for (int bi : c.bones) {
        Bone& b = bones[size_t(bi)];
        if (mixRotate != 0) {
            float a = b.a, bb = b.b, cc = b.c, d = b.d;
            float r = c.relative ? std::atan2(tc, ta) + offRot : std::atan2(tc, ta) - std::atan2(cc, a) + offRot;
            r = wrapRad(r) * mixRotate;
            float cs = std::cos(r), sn = std::sin(r);
            b.a = cs * a - sn * cc; b.b = cs * bb - sn * d; b.c = sn * a + cs * cc; b.d = sn * bb + cs * d;
        }
        if (translate) {
            float wx = ta * c.offX + tb * c.offY + t.worldX, wy = tc * c.offX + td * c.offY + t.worldY;
            if (c.relative) { b.worldX += wx * mixX; b.worldY += wy * mixY; }
            else { b.worldX += (wx - b.worldX) * mixX; b.worldY += (wy - b.worldY) * mixY; }
        }
        if (mixScaleX != 0) {
            float s = std::sqrt(b.a * b.a + b.c * b.c);
            if (s != 0) {
                float ts = std::sqrt(ta * ta + tc * tc);
                s = c.relative ? ((ts - 1 + c.offScaleX) * mixScaleX + 1) : (s + (ts - s + c.offScaleX) * mixScaleX) / s;
            }
            b.a *= s; b.c *= s;
        }
        if (mixScaleY != 0) {
            float s = std::sqrt(b.b * b.b + b.d * b.d);
            if (s != 0) {
                float ts = std::sqrt(tb * tb + td * td);
                s = c.relative ? ((ts - 1 + c.offScaleY) * mixScaleY + 1) : (s + (ts - s + c.offScaleY) * mixScaleY) / s;
            }
            b.b *= s; b.d *= s;
        }
        if (mixShearY > 0) {
            float bb = b.b, d = b.d, by = std::atan2(d, bb);
            float r = std::atan2(td, tb) - std::atan2(tc, ta);
            if (!c.relative) r -= by - std::atan2(b.c, b.a);
            r = wrapRad(r);
            r = by + (r + offShearY) * mixShearY;
            float s = std::sqrt(bb * bb + d * d);
            b.b = std::cos(r) * s; b.d = std::sin(r) * s;
        }
        updateApplied(bi);
        updateChildren(bi);
    }
}

bool Skeleton::worldVertices(int si, std::vector<float>& out) const {
    const Slot& s = slots[size_t(si)];
    const Attachment* a = s.attachment;
    if (!a || a->region < 0) return false;
    const Bone& b = bones[size_t(data_->slots[size_t(si)].bone)];
    if (!b.active) return false;
    if (a->type == AttachmentType::Region) {
        out.resize(8);
        for (int i = 0; i < 4; i++) {
            float ox = a->offset[i * 2], oy = a->offset[i * 2 + 1];
            out[size_t(i * 2)] = ox * b.a + oy * b.b + b.worldX;
            out[size_t(i * 2 + 1)] = ox * b.c + oy * b.d + b.worldY;
        }
        return true;
    }
    if (a->type != AttachmentType::Mesh) return false;
    out.resize(size_t(a->vertexCount) * 2);
    const std::vector<float>& deform = s.deform;
    if (!a->weighted) {
        const std::vector<float>& v = deform.size() == a->vertices.size() ? deform : a->vertices;
        for (int i = 0; i < a->vertexCount; i++) {
            float vx = v[size_t(i * 2)], vy = v[size_t(i * 2 + 1)];
            out[size_t(i * 2)] = vx * b.a + vy * b.b + b.worldX;
            out[size_t(i * 2 + 1)] = vx * b.c + vy * b.d + b.worldY;
        }
        return true;
    }
    size_t p = 0, f = 0;
    bool hasDeform = !deform.empty();
    for (int i = 0; i < a->vertexCount && p < a->vertices.size(); i++) {
        int n = int(a->vertices[p++]);
        float wx = 0, wy = 0;
        for (int k = 0; k < n && p + 3 < a->vertices.size(); k++, p += 4, f += 2) {
            const Bone& wb = bones[size_t(a->vertices[p])];
            float vx = a->vertices[p + 1], vy = a->vertices[p + 2], w = a->vertices[p + 3];
            if (hasDeform && f + 1 < deform.size()) { vx += deform[f]; vy += deform[f + 1]; }
            wx += (vx * wb.a + vy * wb.b + wb.worldX) * w;
            wy += (vx * wb.c + vy * wb.d + wb.worldY) * w;
        }
        out[size_t(i * 2)] = wx;
        out[size_t(i * 2 + 1)] = wy;
    }
    return true;
}

void Skeleton::bounds(float& minX, float& minY, float& maxX, float& maxY) const {
    minX = minY = 1e30f; maxX = maxY = -1e30f;
    std::vector<float> v;
    for (int si : drawOrder) {
        if (!worldVertices(si, v)) continue;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            minX = std::min(minX, v[i]); maxX = std::max(maxX, v[i]);
            minY = std::min(minY, v[i + 1]); maxY = std::max(maxY, v[i + 1]);
        }
    }
    if (minX > maxX) minX = minY = maxX = maxY = 0;
}

void Skeleton::buildDrawList(DrawList& out) const {
    out.clear();
    std::vector<float> v;
    for (int si : drawOrder) {
        const Slot& s = slots[size_t(si)];
        const Attachment* a = s.attachment;
        if (!a || a->region < 0 || !worldVertices(si, v)) continue;
        Color c{color.r * s.color.r * a->color.r, color.g * s.color.g * a->color.g, color.b * s.color.b * a->color.b,
                color.a * s.color.a * a->color.a};
        if (c.a <= 0) continue;
        uint32_t packed = packColor(c.r, c.g, c.b, c.a);
        int page = data_->atlas->regions[size_t(a->region)].page;
        BlendMode blend = data_->slots[size_t(si)].blend;
        size_t nv = v.size() / 2;
        bool merge = !out.batches.empty() && out.batches.back().page == page && out.batches.back().blend == blend &&
                     out.vertices.size() - out.baseVertex.back() + nv < 65535;
        if (!merge) {
            DrawBatch b;
            b.page = page;
            b.blend = blend;
            b.firstIndex = uint32_t(out.indices.size());
            out.batches.push_back(b);
            out.baseVertex.push_back(uint32_t(out.vertices.size()));
        }
        uint32_t base = uint32_t(out.vertices.size() - out.baseVertex.back());
        const float* uv = a->type == AttachmentType::Region ? a->uvs4 : a->uvs.data();
        for (size_t i = 0; i < nv; i++) out.vertices.push_back({v[i * 2], v[i * 2 + 1], uv[i * 2], uv[i * 2 + 1], packed});
        if (a->type == AttachmentType::Region) {
            static const uint16_t q[6] = {0, 1, 2, 2, 3, 0};
            for (uint16_t i : q) out.indices.push_back(uint16_t(base + i));
        } else {
            for (uint16_t i : a->triangles) out.indices.push_back(uint16_t(base + i));
        }
        out.batches.back().indexCount = uint32_t(out.indices.size()) - out.batches.back().firstIndex;
    }
}

// ---------------------------------------------------------------- AnimationState

AnimationState::AnimationState(std::shared_ptr<const SkeletonData> data) : data_(std::move(data)) {}

void AnimationState::setMix(const std::string& from, const std::string& to, float seconds) {
    int a = data_->findAnimation(from), b = data_->findAnimation(to);
    if (a >= 0 && b >= 0) mixes_[{a, b}] = seconds;
}

float AnimationState::mixFor(int from, int to) const {
    auto it = mixes_.find({from, to});
    return it != mixes_.end() ? it->second : defaultMix;
}

void AnimationState::start(int ti, Track& t, Entry e) {
    if (t.cur && t.cur->anim >= 0) {
        if (listener && t.from) listener({TrackEvent::End, ti, data_->animations[size_t(t.from->anim)].name});
        t.from = t.cur;
        t.mixTime = 0;
        t.mixDuration = e.anim >= 0 ? mixFor(t.cur->anim, e.anim) : t.mixDuration;
    } else {
        t.from.reset();
        t.mixDuration = 0;
    }
    t.fadingOut = e.anim < 0;
    e.time = 0;
    e.last = -1;
    e.started = false;
    e.completed = false;
    t.cur = e;
}

bool AnimationState::setAnimation(int track, const std::string& name, bool loop) {
    int a = data_->findAnimation(name);
    if (a < 0 || track < 0 || track > 15) return false;
    if (size_t(track) >= tracks_.size()) tracks_.resize(size_t(track) + 1);
    Track& t = tracks_[size_t(track)];
    t.queue.clear();
    Entry e;
    e.anim = a;
    e.loop = loop;
    start(track, t, e);
    return true;
}

bool AnimationState::addAnimation(int track, const std::string& name, bool loop, float delay) {
    int a = data_->findAnimation(name);
    if (a < 0 || track < 0 || track > 15) return false;
    if (size_t(track) >= tracks_.size()) tracks_.resize(size_t(track) + 1);
    Track& t = tracks_[size_t(track)];
    Entry e;
    e.anim = a;
    e.loop = loop;
    e.delay = delay;
    if (!t.cur) start(track, t, e);
    else t.queue.push_back(e);
    return true;
}

void AnimationState::setEmptyAnimation(int track, float mix) {
    if (track < 0 || size_t(track) >= tracks_.size()) return;
    Track& t = tracks_[size_t(track)];
    t.queue.clear();
    if (!t.cur || t.cur->anim < 0) return;
    Entry e;
    e.anim = -1;
    start(track, t, e);
    t.mixDuration = mix;
}

void AnimationState::clearTrack(int track) {
    if (track >= 0 && size_t(track) < tracks_.size()) tracks_[size_t(track)] = Track{};
}

void AnimationState::clearTracks() { tracks_.clear(); }

std::string AnimationState::current(int track) const {
    if (track < 0 || size_t(track) >= tracks_.size()) return {};
    const Track& t = tracks_[size_t(track)];
    return t.cur && t.cur->anim >= 0 ? data_->animations[size_t(t.cur->anim)].name : std::string();
}

float AnimationState::trackTime(int track) const {
    if (track < 0 || size_t(track) >= tracks_.size() || !tracks_[size_t(track)].cur) return 0;
    return tracks_[size_t(track)].cur->time;
}

bool AnimationState::finished(int track) const {
    if (track < 0 || size_t(track) >= tracks_.size()) return true;
    const Track& t = tracks_[size_t(track)];
    if (!t.cur || t.cur->anim < 0) return true;
    return !t.cur->loop && t.cur->time >= data_->animations[size_t(t.cur->anim)].duration && t.queue.empty();
}

void AnimationState::fireEvents(int ti, const Entry& e, float from, float to) {
    if (e.anim < 0) return;
    const Animation& a = data_->animations[size_t(e.anim)];
    float d = a.duration;
    auto fire = [&](float lo, float hi, float base) {
        for (const EventKey& k : a.events) {
            float t = base + k.time;
            if (t > lo && t <= hi && listener)
                listener({TrackEvent::Event, ti, a.name, &data_->events[size_t(k.event)], &k});
        }
    };
    if (!e.loop || d <= 0) { fire(from, std::min(to, d), 0); return; }
    int c0 = int(std::floor(std::max(from, 0.f) / d)), c1 = int(std::floor(to / d));
    for (int c = std::max(c0, c1 - 3); c <= c1; c++) fire(from, to, float(c) * d);
    if (listener && c1 > c0 && from >= 0) listener({TrackEvent::Complete, ti, a.name});
}

void AnimationState::update(float dt) {
    dt *= timeScale;
    for (size_t ti = 0; ti < tracks_.size(); ti++) {
        Track& t = tracks_[ti];
        if (t.from) {
            t.from->time += dt;
            t.mixTime += dt;
            if (t.mixTime >= t.mixDuration) {
                if (listener && t.from->anim >= 0) listener({TrackEvent::End, int(ti), data_->animations[size_t(t.from->anim)].name});
                t.from.reset();
                if (t.fadingOut) { t.cur.reset(); t.fadingOut = false; }
            }
        }
        if (!t.cur) continue;
        Entry& e = *t.cur;
        if (e.anim < 0) continue;
        const Animation& a = data_->animations[size_t(e.anim)];
        if (!e.started) {
            e.started = true;
            if (listener) listener({TrackEvent::Start, int(ti), a.name});
        }
        float prev = e.time;
        e.time += dt;
        fireEvents(int(ti), e, e.last < 0 ? -1.f : prev, e.time);
        e.last = e.time;
        if (!e.loop && !e.completed && e.time >= a.duration) {
            e.completed = true;
            if (listener) listener({TrackEvent::Complete, int(ti), a.name});
        }
        if (!t.queue.empty()) {
            Entry& next = t.queue.front();
            float startAt = next.delay > 0 ? next.delay : std::max(0.f, a.duration + next.delay - mixFor(e.anim, next.anim));
            if (e.loop && next.delay <= 0 && a.duration > 0)
                startAt = std::ceil(std::max(e.time - 1e-6f, 0.f) / a.duration) * a.duration + next.delay - mixFor(e.anim, next.anim);
            if (e.time >= startAt) {
                Entry n = next;
                t.queue.pop_front();
                float over = e.time - startAt;
                start(int(ti), t, n);
                t.cur->time = std::max(0.f, over);
            }
        }
    }
}

void AnimationState::applyAnim(Skeleton& sk, const Animation& a, float time, float alpha, bool attachments) const {
    const SkeletonData& d = *data_;
    for (const BoneTimeline& tl : a.bones) {
        if (tl.curve.times.empty() || time < tl.curve.times[0]) continue;
        Bone& b = sk.bones[size_t(tl.bone)];
        const BoneData& s = d.bones[size_t(tl.bone)];
        float v0 = tl.curve.value(time, 0), v1 = tl.curve.channels > 1 ? tl.curve.value(time, 1) : 0;
        switch (tl.prop) {
        case BoneProp::Rotate: {
            float r = s.rotation + v0 - b.rotation;
            r = std::fmod(r, 360.f);
            if (r > 180) r -= 360; else if (r < -180) r += 360;
            b.rotation += r * alpha;
            break;
        }
        case BoneProp::Translate: b.x += (s.x + v0 - b.x) * alpha; b.y += (s.y + v1 - b.y) * alpha; break;
        case BoneProp::TranslateX: b.x += (s.x + v0 - b.x) * alpha; break;
        case BoneProp::TranslateY: b.y += (s.y + v0 - b.y) * alpha; break;
        case BoneProp::Scale: b.scaleX += (s.scaleX * v0 - b.scaleX) * alpha; b.scaleY += (s.scaleY * v1 - b.scaleY) * alpha; break;
        case BoneProp::ScaleX: b.scaleX += (s.scaleX * v0 - b.scaleX) * alpha; break;
        case BoneProp::ScaleY: b.scaleY += (s.scaleY * v0 - b.scaleY) * alpha; break;
        case BoneProp::Shear: b.shearX += (s.shearX + v0 - b.shearX) * alpha; b.shearY += (s.shearY + v1 - b.shearY) * alpha; break;
        case BoneProp::ShearX: b.shearX += (s.shearX + v0 - b.shearX) * alpha; break;
        case BoneProp::ShearY: b.shearY += (s.shearY + v0 - b.shearY) * alpha; break;
        }
    }
    for (const SlotTimeline& tl : a.slots) {
        if (tl.curve.times.empty() || time < tl.curve.times[0]) continue;
        Color& c = sk.slots[size_t(tl.slot)].color;
        if (tl.prop == SlotProp::Alpha) { c.a += (tl.curve.value(time, 0) - c.a) * alpha; continue; }
        c.r += (tl.curve.value(time, 0) - c.r) * alpha;
        c.g += (tl.curve.value(time, 1) - c.g) * alpha;
        c.b += (tl.curve.value(time, 2) - c.b) * alpha;
        if (tl.prop == SlotProp::Rgba) c.a += (tl.curve.value(time, 3) - c.a) * alpha;
    }
    if (attachments) {
        for (const AttachmentTimeline& tl : a.attachments) {
            auto it = std::upper_bound(tl.times.begin(), tl.times.end(), time);
            if (it == tl.times.begin()) continue;
            const std::string& n = tl.names[size_t(it - tl.times.begin() - 1)];
            Slot& s = sk.slots[size_t(tl.slot)];
            const Attachment* att = sk.attachment(tl.slot, n);
            if (att != s.attachment) { s.attachment = att; s.deform.clear(); }
        }
        const DrawOrderTimeline& dot = a.drawOrder;
        auto it = std::upper_bound(dot.times.begin(), dot.times.end(), time);
        if (it != dot.times.begin()) {
            const std::vector<int>& order = dot.orders[size_t(it - dot.times.begin() - 1)];
            if (order.empty()) for (size_t i = 0; i < sk.drawOrder.size(); i++) sk.drawOrder[i] = int(i);
            else sk.drawOrder = order;
        }
    }
    for (const DeformTimeline& tl : a.deforms) {
        if (tl.frames.empty() || tl.curve.times.empty() || time < tl.curve.times[0]) continue;
        Slot& s = sk.slots[size_t(tl.slot)];
        const Attachment* att = s.attachment;
        if (!att || (att != tl.attachment && att->deformParent != tl.attachment)) continue;
        float fv = tl.curve.value(time, 0);
        size_t i = size_t(std::max(0.f, std::floor(fv)));
        if (i >= tl.frames.size()) i = tl.frames.size() - 1;
        float f = fv - float(i);
        const std::vector<float>& f0 = tl.frames[i];
        const std::vector<float>& f1 = i + 1 < tl.frames.size() ? tl.frames[i + 1] : f0;
        if (s.deform.size() != f0.size()) {
            if (att->weighted) s.deform.assign(f0.size(), 0.f);
            else s.deform = att->vertices;
            s.deform.resize(f0.size(), 0.f);
        }
        for (size_t k = 0; k < f0.size(); k++) {
            float target = f0[k] + (f1[k] - f0[k]) * f;
            s.deform[k] += (target - s.deform[k]) * alpha;
        }
    }
    for (const IkTimeline& tl : a.ik) {
        if (tl.curve.times.empty() || time < tl.curve.times[0]) continue;
        size_t ci = size_t(tl.ik);
        sk.ikMix[ci] += (tl.curve.value(time, 0) - sk.ikMix[ci]) * alpha;
        sk.ikSoftness[ci] += (tl.curve.value(time, 1) - sk.ikSoftness[ci]) * alpha;
        int f = std::max(0, tl.curve.frameAt(time));
        if (alpha >= 0.5f) { sk.ikBend[ci] = tl.bend[size_t(f)]; sk.ikCompress[ci] = tl.compress[size_t(f)]; sk.ikStretch[ci] = tl.stretch[size_t(f)]; }
    }
    for (const TransformTimeline& tl : a.transforms) {
        if (tl.curve.times.empty() || time < tl.curve.times[0]) continue;
        float* m = &sk.tcMix[size_t(tl.constraint) * 6];
        for (int c = 0; c < 6; c++) m[c] += (tl.curve.value(time, c) - m[c]) * alpha;
    }
}

void AnimationState::apply(Skeleton& sk) {
    sk.setToSetupPose();
    for (size_t ti = 0; ti < tracks_.size(); ti++) {
        Track& t = tracks_[ti];
        float pct = 1;
        if (t.from && t.mixDuration > 0) pct = std::min(1.f, t.mixTime / t.mixDuration);
        if (t.from && t.from->anim >= 0) {
            const Animation& a = data_->animations[size_t(t.from->anim)];
            float fromAlpha = ti == 0 && !t.fadingOut ? 1.f : 1.f - pct;
            applyAnim(sk, a, animationTime(a, t.from->time, t.from->loop), fromAlpha, t.fadingOut);
        }
        if (t.cur && t.cur->anim >= 0) {
            const Animation& a = data_->animations[size_t(t.cur->anim)];
            applyAnim(sk, a, animationTime(a, t.cur->time, t.cur->loop), t.from ? pct : 1.f, true);
        }
    }
}

} // namespace aiverse::spine
