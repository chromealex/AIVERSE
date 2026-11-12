// Spine runtime: a posed skeleton instance, the animation state (tracks, crossfades, queues, events)
// and the draw list a renderer uploads. No GPU and no simulation: presentation only.
//
//   auto data = spine::parseSkeleton(json, atlas);
//   spine::Skeleton sk(data);  spine::AnimationState st(data);
//   st.setAnimation(0, "idle", true);
//   every frame: st.update(dt); st.apply(sk); sk.updateWorldTransform(); sk.buildDrawList(list);
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "SpineData.h"

namespace aiverse::spine {

struct Bone {
    float x = 0, y = 0, rotation = 0, scaleX = 1, scaleY = 1, shearX = 0, shearY = 0;   // local pose
    float ax = 0, ay = 0, arotation = 0, ascaleX = 1, ascaleY = 1, ashearX = 0, ashearY = 0;   // applied
    float a = 1, b = 0, c = 0, d = 1, worldX = 0, worldY = 0;   // world transform
    bool active = true;
};

struct Slot {
    Color color;
    const Attachment* attachment = nullptr;
    std::vector<float> deform;   // empty = setup vertices of the mesh
};

struct DrawVertex { float x, y, u, v; uint32_t color; };   // color: RGBA8 (r in the low byte), straight alpha
struct DrawBatch { int page = 0; BlendMode blend = BlendMode::Normal; uint32_t firstIndex = 0, indexCount = 0; };
struct DrawList {
    std::vector<DrawVertex> vertices;
    std::vector<uint16_t> indices;    // per batch, relative to batch.baseVertex
    std::vector<DrawBatch> batches;
    std::vector<uint32_t> baseVertex; // per batch
    void clear() { vertices.clear(); indices.clear(); batches.clear(); baseVertex.clear(); }
};

class Skeleton {
public:
    explicit Skeleton(std::shared_ptr<const SkeletonData> data);
    const SkeletonData& data() const { return *data_; }
    std::shared_ptr<const SkeletonData> dataPtr() const { return data_; }

    // Root placement (skeleton space: x right, y up) and tint.
    float x = 0, y = 0, scaleX = 1, scaleY = 1;
    Color color;

    std::vector<Bone> bones;
    std::vector<Slot> slots;
    std::vector<int> drawOrder;       // slot indices, back to front
    std::vector<float> ikMix, ikSoftness;
    std::vector<int> ikBend;
    std::vector<uint8_t> ikCompress, ikStretch;
    std::vector<float> tcMix;         // 6 per transform constraint

    bool setSkin(const std::string& name);   // false: no such skin (keeps the current one)
    const std::string& skinName() const;
    const Attachment* attachment(int slot, const std::string& name) const;   // current skin, then default
    void setToSetupPose();
    void setBonesToSetupPose();
    void setSlotsToSetupPose();
    void updateWorldTransform();

    // World positions of a slot's attachment (x, y pairs); false when nothing is drawn.
    bool worldVertices(int slot, std::vector<float>& out) const;
    void bounds(float& minX, float& minY, float& maxX, float& maxY) const;
    // Every visible attachment in draw order, merged into batches by page + blend.
    void buildDrawList(DrawList& out) const;

    int findBone(const std::string& n) const { return data_->findBone(n); }
    int findSlot(const std::string& n) const { return data_->findSlot(n); }

private:
    void boneWorld(int i, float x, float y, float rot, float sx, float sy, float shx, float shy);
    void updateApplied(int i);
    void updateChildren(int i);
    void applyIk(int c);
    void applyTransform(int c);
    void ik1(int bone, float tx, float ty, bool compress, bool stretch, bool uniform, float alpha);
    void ik2(int parent, int child, float tx, float ty, int bend, bool stretch, bool uniform, float softness, float alpha);
    void updateActive();

    std::shared_ptr<const SkeletonData> data_;
    int skin_ = 0;
    std::vector<std::pair<int, int>> constraints_;   // (kind 0 ik / 1 transform, index) in update order
    std::vector<std::vector<int>> children_;
};

struct TrackEvent {
    enum Type { Start, Complete, End, Event } type = Event;
    int track = 0;
    std::string animation;
    const EventData* data = nullptr;   // Event
    const EventKey* key = nullptr;     // Event
};

class AnimationState {
public:
    explicit AnimationState(std::shared_ptr<const SkeletonData> data);
    float defaultMix = 0.2f;
    float timeScale = 1;
    void setMix(const std::string& from, const std::string& to, float seconds);

    // false: unknown animation (nothing changes)
    bool setAnimation(int track, const std::string& name, bool loop);
    // Plays after the current one: delay <= 0 = when the current one ends (minus the mix), > 0 = seconds after it started.
    bool addAnimation(int track, const std::string& name, bool loop, float delay = 0);
    void setEmptyAnimation(int track, float mix);   // fades the track out
    void clearTrack(int track);
    void clearTracks();
    std::string current(int track) const;          // "" = none
    float trackTime(int track) const;
    bool finished(int track) const;                // non-looping animation reached its end (or empty)

    void update(float dt);
    // Resets the skeleton to the setup pose and applies every track (crossfades included).
    void apply(Skeleton& sk);

    std::function<void(const TrackEvent&)> listener;

private:
    struct Entry { int anim = -1; bool loop = false; float time = 0, last = -1, delay = 0; bool started = false, completed = false; };
    struct Track {
        std::optional<Entry> cur, from;
        float mixTime = 0, mixDuration = 0;
        std::deque<Entry> queue;
        bool fadingOut = false;
    };
    float mixFor(int from, int to) const;
    void start(int ti, Track& t, Entry e);
    void applyAnim(Skeleton& sk, const Animation& a, float time, float alpha, bool attachments) const;
    void fireEvents(int ti, const Entry& e, float from, float to);

    std::shared_ptr<const SkeletonData> data_;
    std::vector<Track> tracks_;
    std::map<std::pair<int, int>, float> mixes_;
};

// Time inside an animation (looping wraps, otherwise clamps).
float animationTime(const Animation& a, float t, bool loop);

} // namespace aiverse::spine
