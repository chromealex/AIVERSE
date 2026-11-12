// agent-bw-client: Spine runtime, gamepad mapping, client services (Steam social, push, links).
// Appended to tests/tests.cpp (static registration, no GPU).
#include "engine/spine/SpineSkeleton.h"
#include <cmath>
#include <fstream>
#include <sstream>

namespace bwc {
static std::string readText(const std::string& rel) {
    std::ifstream f(std::string(AIVERSE_SOURCE_DIR) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static std::shared_ptr<spine::SkeletonData> mini() {
    auto atlas = std::make_shared<spine::Atlas>(spine::parseAtlas(readText("tests/data/spine/mini.atlas")));
    return spine::parseSkeleton(readText("tests/data/spine/mini.json"), atlas);
}
} // namespace bwc

TEST(spine_atlas_reads_both_layouts_and_rotated_regions) {
    auto a = spine::parseAtlas(bwc::readText("tests/data/spine/mini.atlas"));
    CHECK(a.pages.size() == 1 && a.pages[0].pma && a.pages[0].width == 64);
    const spine::AtlasRegion* arm = a.find("arm");
    CHECK(arm && arm->degrees == 90 && arm->w == 8 && arm->h == 16 && arm->origW == 10 && arm->origH == 20);
    // old libGDX layout (xy / size / orig / offset, rotate: true)
    auto old = spine::parseAtlas("p.png\nsize: 32, 32\nformat: RGBA8888\nfilter: Linear,Linear\nrepeat: none\n"
                                 "r\n  rotate: true\n  xy: 2, 4\n  size: 6, 10\n  orig: 8, 12\n  offset: 1, 1\n  index: -1\n");
    const spine::AtlasRegion* r = old.find("r");
    CHECK(r && r->degrees == 90 && r->x == 2 && r->y == 4 && r->w == 6 && r->h == 10 && r->origW == 8 && r->offY == 1);
    CHECK(bwc::near(r->u1, (2 + 10) / 32.f) && bwc::near(r->v1, (4 + 6) / 32.f));
    bool threw = false;
    try { spine::parseAtlas("p.png\nsize: 8,8\nr\nbounds: 0,0,2,2\nrotate: 45\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST(spine_setup_pose_world_transforms_and_attachments) {
    auto d = bwc::mini();
    CHECK(d->bones.size() == 6 && d->slots.size() == 3 && d->animations.size() == 2 && d->warnings.empty());
    spine::Skeleton sk(d);
    sk.updateWorldTransform();
    const spine::Bone& arm = sk.bones[size_t(sk.findBone("arm"))];
    CHECK(bwc::near(arm.worldX, 15) && bwc::near(arm.worldY, 2) && bwc::near(arm.a, 0) && bwc::near(arm.c, 1));
    const spine::Bone& hand = sk.bones[size_t(sk.findBone("hand"))];
    CHECK(bwc::near(hand.worldX, 15) && bwc::near(hand.worldY, 10));
    // transform constraint: "follow" sits on "hand"
    const spine::Bone& follow = sk.bones[size_t(sk.findBone("follow"))];
    CHECK(bwc::near(follow.worldX, 15) && bwc::near(follow.worldY, 10));
    std::vector<float> v;
    CHECK(sk.worldVertices(sk.findSlot("body"), v) && v.size() == 8);
    CHECK(bwc::near(v[0], -3) && bwc::near(v[1], -2) && bwc::near(v[4], 13) && bwc::near(v[5], 6));
    // trimmed (offsets) + rotated region: corners in the arm bone, uvs on the rotated page rect
    CHECK(sk.worldVertices(sk.findSlot("arm"), v));
    CHECK(bwc::near(v[0], 23) && bwc::near(v[1], -2));
    const spine::Attachment* att = sk.slots[size_t(sk.findSlot("arm"))].attachment;
    CHECK(att && bwc::near(att->uvs4[0], 0.5f) && bwc::near(att->uvs4[1], 0.125f) && bwc::near(att->uvs4[4], 0.25f) && bwc::near(att->uvs4[5], 0));
    // weighted mesh: vertex 1 is half body, half arm
    CHECK(sk.worldVertices(sk.findSlot("skin"), v) && v.size() == 6);
    CHECK(bwc::near(v[0], 5) && bwc::near(v[1], 2));
    CHECK(bwc::near(v[2], 0.5f * (5 + 4) + 0.5f * (15 - 0.5f)) && bwc::near(v[3], 0.5f * 2 + 0.5f * (2 + 4)));
    spine::DrawList list;
    sk.buildDrawList(list);
    // normal box, additive arm, normal mesh: three batches, mesh color carries the slot tint
    CHECK(list.batches.size() == 3 && list.vertices.size() == 4 + 4 + 3 && list.indices.size() == 6 + 6 + 3);
    CHECK(list.batches[1].blend == spine::BlendMode::Additive);
    CHECK((list.vertices.back().color & 0xff) == 255 && (list.vertices.back().color >> 24) == 128);
}

TEST(spine_animation_timelines_events_and_mixing) {
    auto d = bwc::mini();
    spine::Skeleton sk(d);
    spine::AnimationState st(d);
    std::vector<int> ints;
    int completes = 0;
    st.listener = [&](const spine::TrackEvent& e) {
        if (e.type == spine::TrackEvent::Event) ints.push_back(e.key->intValue);
        if (e.type == spine::TrackEvent::Complete) completes++;
    };
    CHECK(!st.setAnimation(0, "nope", true));
    CHECK(st.setAnimation(0, "wave", false));
    st.update(0.5f);
    st.apply(sk);
    sk.updateWorldTransform();
    const spine::Bone& body = sk.bones[size_t(sk.findBone("body"))];
    CHECK(bwc::near(body.rotation, 45) && bwc::near(body.x, 9) && bwc::near(body.y, 4));   // stepped translate
    const spine::Slot& bs = sk.slots[size_t(sk.findSlot("body"))];
    CHECK(bwc::near(bs.color.r, 0.5f, 0.01f) && bwc::near(bs.color.a, 0.5f, 0.01f));
    CHECK(sk.slots[size_t(sk.findSlot("arm"))].attachment == nullptr);
    CHECK(sk.drawOrder == std::vector<int>({1, 2, 0}));
    CHECK(bwc::near(sk.ikMix[0], 0.5f));
    CHECK(ints == std::vector<int>({3}));
    st.update(0.6f);   // past the end: second event, completes once, pose holds the last key
    st.apply(sk);
    sk.updateWorldTransform();
    CHECK(ints == std::vector<int>({3, 7}) && completes == 1 && st.finished(0));
    CHECK(bwc::near(sk.bones[size_t(sk.findBone("body"))].rotation, 90));
    // deform of a weighted mesh: first weight moved by +2 in x
    std::vector<float> v;
    CHECK(sk.worldVertices(sk.findSlot("skin"), v));
    const spine::Bone& b = sk.bones[size_t(sk.findBone("body"))];
    CHECK(bwc::near(v[0], b.worldX + b.a * 2) && bwc::near(v[1], b.worldY + b.c * 2));
    // IK at mix 1: the arm points at the target
    const spine::Bone& arm = sk.bones[size_t(sk.findBone("arm"))];
    const spine::Bone& tgt = sk.bones[size_t(sk.findBone("target"))];
    float ang = std::atan2(arm.c, arm.a), want = std::atan2(tgt.worldY - arm.worldY, tgt.worldX - arm.worldX);
    CHECK(bwc::near(ang, want, 1e-3f));

    // bezier: eased rotation is ahead of linear in the second half
    spine::AnimationState e(d);
    e.setAnimation(0, "ease", false);
    e.update(0.5f);
    e.apply(sk);
    float r = sk.bones[size_t(sk.findBone("body"))].rotation;
    CHECK(r > 55 && r < 95);
    // crossfade: halfway through a 0.2 s mix the pose is between both animations
    spine::AnimationState m(d);
    m.setAnimation(0, "wave", true);
    m.update(1.0f);   // wave wraps to 0: rotate 0
    m.setMix("wave", "ease", 0.2f);
    m.setAnimation(0, "ease", false);
    m.update(0.1f);
    m.apply(sk);
    float from = 9, to = 0;   // wave at 0.1 s -> 9 degrees; ease at 0.1 s -> small
    {
        spine::Skeleton s2(d);
        spine::AnimationState only(d);
        only.setAnimation(0, "ease", false);
        only.update(0.1f);
        only.apply(s2);
        to = s2.bones[size_t(s2.findBone("body"))].rotation;
    }
    float mixed = sk.bones[size_t(sk.findBone("body"))].rotation;
    CHECK(bwc::near(mixed, from + (to - from) * 0.5f, 0.05f));
    m.update(0.2f);
    m.apply(sk);
    CHECK(m.current(0) == "ease");
    // queue: ease (1 s, non-looping) then wave
    spine::AnimationState q(d);
    q.defaultMix = 0;
    q.setAnimation(0, "ease", false);
    q.addAnimation(0, "wave", true);
    q.update(0.9f);
    CHECK(q.current(0) == "ease");
    q.update(0.2f);
    CHECK(q.current(0) == "wave");
}

TEST(spine_bad_data_is_reported) {
    auto atlas = std::make_shared<spine::Atlas>();
    bool threw = false;
    try { spine::parseSkeleton(R"({"bones":[{"name":"root"}],"slots":[{"name":"s","bone":"nope"}]})", atlas); }
    catch (const std::exception& e) { threw = std::string(e.what()).find("unknown bone 'nope'") != std::string::npos; }
    CHECK(threw);
    // missing regions are warnings, the skeleton still loads and draws nothing for them
    auto d = spine::parseSkeleton(R"({"bones":[{"name":"root"}],"slots":[{"name":"s","bone":"root","attachment":"a"}],
        "skins":[{"name":"default","attachments":{"s":{"a":{"width":4,"height":4}}}}]})", atlas);
    CHECK(d->warnings.size() == 1);
    spine::Skeleton sk(d);
    sk.updateWorldTransform();
    spine::DrawList list;
    sk.buildDrawList(list);
    CHECK(list.batches.empty());
}

// ---------------- client services: social (Steam), push, deep links through the desktop mocks ----------------
// clipping attachments, path constraints, screen slots (Spine 4.0 JSON)
TEST(spine_clipping_paths_and_screen_blend) {
    auto atlas = std::make_shared<spine::Atlas>(spine::parseAtlas(bwc::readText("tests/data/spine/mini.atlas")));
    // a 16 x 8 box at the origin (x -8..8, y -4..4) cut by a square clip (x -2..2, y -10..10) up to slot "box";
    // slot "glow" draws after the clip ends with screen blending; bone "rider" follows a straight path along x
    const char* json = R"({"skeleton": {"spine": "4.0.64"},
      "bones": [{"name": "root"}, {"name": "rider", "parent": "root", "length": 10}, {"name": "rider2", "parent": "rider", "x": 10, "length": 10}],
      "slots": [{"name": "clip", "bone": "root", "attachment": "clip"}, {"name": "box", "bone": "root", "attachment": "box"},
                {"name": "glow", "bone": "root", "attachment": "box", "blend": "screen"}, {"name": "rail", "bone": "root", "attachment": "rail"}],
      "path": [{"name": "ride", "bones": ["rider", "rider2"], "target": "rail", "rotateMode": "chain", "spacingMode": "length",
                "positionMode": "fixed", "position": 20}],
      "skins": [{"name": "default", "attachments": {
          "clip": {"clip": {"type": "clipping", "end": "box", "vertexCount": 4, "vertices": [-2, -10, 2, -10, 2, 10, -2, 10]}},
          "box": {"box": {"width": 16, "height": 8}},
          "glow": {"box": {"width": 16, "height": 8}},
          "rail": {"rail": {"type": "path", "constantSpeed": true, "vertexCount": 6, "lengths": [100],
                            "vertices": [-10, 0, 0, 0, 10, 0, 90, 0, 100, 0, 110, 0]}}}}],
      "animations": {"move": {"path": {"ride": {"position": [{"time": 0, "value": 20}, {"time": 1, "value": 60}]}}}}})";
    auto d = spine::parseSkeleton(json, atlas);
    CHECK(d->paths.size() == 1 && d->warnings.empty());
    spine::Skeleton sk(d);
    sk.updateWorldTransform();
    spine::DrawList list;
    sk.buildDrawList(list);
    // box: clipped to x in [-2, 2]; glow: whole, its own (screen) batch
    CHECK(list.batches.size() == 2 && list.batches[1].blend == spine::BlendMode::Screen);
    float minX = 1e9f, maxX = -1e9f;
    for (uint32_t i = 0; i < list.batches[0].indexCount; i++) {
        const spine::DrawVertex& dv = list.vertices[list.baseVertex[0] + list.indices[list.batches[0].firstIndex + i]];
        minX = std::min(minX, dv.x); maxX = std::max(maxX, dv.x);
    }
    CHECK(bwc::near(minX, -2) && bwc::near(maxX, 2));
    CHECK(list.batches[1].indexCount == 6);
    // uvs of a clipped vertex are interpolated: x = 0 is the middle of the box region (u 0 .. 0.25)
    bool mid = false;
    for (uint32_t i = 0; i < list.batches[0].indexCount; i++) {
        const spine::DrawVertex& dv = list.vertices[list.baseVertex[0] + list.indices[list.batches[0].firstIndex + i]];
        if (bwc::near(dv.x, 2)) mid |= bwc::near(dv.u, 0.125f + 0.25f * 2 / 16, 1e-3f) || bwc::near(dv.u, 0.125f - 0.25f * 2 / 16, 1e-3f);
    }
    CHECK(mid);
    // path: the rider starts 20 units along the rail (fixed position), the chain keeps the bone on the x axis
    const spine::Bone& r = sk.bones[size_t(sk.findBone("rider"))];
    const spine::Bone& r2 = sk.bones[size_t(sk.findBone("rider2"))];
    CHECK(bwc::near(r.worldX, 20, 0.5f) && bwc::near(r.worldY, 0, 0.01f) && bwc::near(r2.worldX, 30, 0.5f));
    spine::AnimationState st(d);
    st.setAnimation(0, "move", false);
    st.update(1);
    st.apply(sk);
    sk.updateWorldTransform();
    CHECK(bwc::near(sk.bones[size_t(sk.findBone("rider"))].worldX, 60, 0.5f));
}

#include "engine/platform/Plugins.h"
#include "engine/services/Consent.h"
#include "engine/services/Privacy.h"
#include "engine/services/Links.h"
#include "engine/services/Push.h"
#include "engine/services/Social.h"

TEST(client_services_social_push_links_through_mocks) {
    plugins::configureGame(R"({
      "plugins": { "mock-social": { "achievements": { "first_win": "ACH_WIN_ONE_GAME", "veteran": "ACH_100" } },
                   "mock-push": {}, "mock-links": {} },
      "social": { "providers": { "default": ["steam", "mock-social"] } }
    })");
    social::reset(); push::reset(); links::reset();
    CHECK(social::available() && social::provider() == "mock-social");
    std::vector<std::string> events;
    int sl = social::onEvent([&](const std::string& ev, const std::string& d) { events.push_back(ev + d); });
    social::User me;
    social::user([&](const social::User& u) { me = u; });
    bool unlocked = false;
    social::unlock("first_win", [&](bool ok, const std::string&) { unlocked = ok; });
    social::unlock("first_win");   // idempotent: one "achievement" event
    social::addStat("wins", 2);
    social::addStat("wins", 3);
    double wins = 0;
    social::stat("wins", [&](bool ok, double v) { if (ok) wins = v; });
    std::vector<social::Achievement> achs;
    social::achievements([&](bool ok, const std::vector<social::Achievement>& a, const std::string&) { if (ok) achs = a; });
    std::vector<social::Friend> fr;
    social::friends([&](bool ok, const std::vector<social::Friend>& f, const std::string&) { if (ok) fr = f; });
    plugins::pump();
    CHECK(me.ok && !me.id.empty() && me.name == "Player");
    CHECK(unlocked && wins == 5);
    CHECK(achs.size() == 2 && achs[0].id == "first_win" && achs[0].unlocked && !achs[1].unlocked);
    CHECK(fr.size() == 3 && fr[0].playingThisGame && fr[2].state == "offline");
    CHECK(events.size() == 1 && events[0] == R"(achievement{"id":"first_win"})");
    plugins::call("mock-social", "join", R"({"connect":"+lobby 42"})");   // a friend accepts an invite
    plugins::pump();
    CHECK(events.size() == 2 && events[1].rfind("join", 0) == 0 && events[1].find("+lobby 42") != std::string::npos);
    social::removeListener(sl);

    CHECK(push::available() && push::provider() == "mock-push");
    std::vector<std::string> pev;
    int pl = push::onEvent([&](const std::string& ev, const std::string& d) { pev.push_back(ev + d); });
    std::string tok;
    bool granted = false;
    push::requestPermission([&](bool g) { granted = g; });
    push::token([&](const std::string& t) { tok = t; });
    push::schedule({"energy", "Energy", "Full!", 3600, "comeback", "energy", R"({"screen":"battle"})", 0});
    push::schedule({"chest", "Chest", "Ready", 60, "rewards", "chest", "", 0});
    push::schedule({"daily", "Daily", "Bonus", 86400, "rewards", "daily", "", 86400});
    push::cancelCategory("rewards");
    bool bad = true;
    push::schedule({"", "x", "y", 1, "", "", "", 0}, [&](bool ok, const std::string&) { bad = ok; });
    plugins::pump();
    CHECK(granted && tok == "mock-push-token" && !bad);
    std::string pending;
    plugins::call("mock-push", "pending", "{}", [&](const plugins::Result& r) { pending = r.data; });
    plugins::call("mock-push", "fire", R"({"id":"energy"})");
    plugins::pump();
    CHECK(pending.find("\"energy\"") != std::string::npos && pending.find("chest") == std::string::npos);
    CHECK(!pev.empty() && pev.back().rfind("opened", 0) == 0 && pev.back().find("battle") != std::string::npos);
    push::removeListener(pl);

    CHECK(links::available() && links::providers() == std::vector<std::string>({"mock-links"}));
    std::vector<links::Link> got;
    int ll = links::onLink([&](const links::Link& l) { got.push_back(l); });
    links::Link first{"x", "", false, "{}"};
    links::initial([&](const links::Link& l) { first = l; });
    plugins::call("mock-links", "open", R"({"url":"bunkerwars://invite?c=42&n=Big%20Boss"})");
    plugins::call("mock-links", "open", R"({"url":"bunkerwars://invite?c=42&n=Big%20Boss"})");   // repeat: dropped
    plugins::pump();
    CHECK(first.url.empty());
    CHECK(got.size() == 1 && got[0].source == "mock-links");
    links::Parsed p = links::parse(got[0].url);
    CHECK(p.scheme == "bunkerwars" && p.host == "invite" && p.path.empty() && links::query(p, "c") == "42" && links::query(p, "n") == "Big Boss");
    links::Parsed w = links::parse("https://bunkerwars.onelink.me/abc/clan?id=7#x");
    CHECK(w.scheme == "https" && w.host == "bunkerwars.onelink.me" && w.path == "abc/clan" && links::query(w, "id") == "7");
    links::removeListener(ll);

    // consent: no CMP in the build -> nothing to ask; mock CMP -> the player's answer
    consent::reset();
    plugins::configureGame(R"({"plugins": {}})");
    consent::Result none;
    consent::request([&](const consent::Result& r) { none = r; });
    CHECK(!consent::available() && none.status == consent::Status::NotRequired && none.personalized);
    plugins::configureGame(R"({"plugins": {"mock-consent": {}}})");
    consent::reset();
    setEnvVar("AIVERSE_MOCK_CONSENT", "denied");
    privacy::reset();
    consent::Result cr;
    consent::request([&](const consent::Result& r) { cr = r; }, true);
    plugins::pump();
    CHECK(consent::provider() == "mock-consent" && cr.status == consent::Status::Obtained && cr.canRequestAds && !cr.personalized && cr.privacyOptionsRequired);
    // the CMP answer reaches the privacy service (ads / analytics) without game code
    CHECK(privacy::consentSet() && !privacy::consent().personalizedAds && privacy::consent().analytics);
    privacy::reset();
    setEnvVar("AIVERSE_MOCK_CONSENT", "");
    consent::reset();
    social::reset(); push::reset(); links::reset();
    plugins::configureGame("{}");
}

// ---------------- gamepads: platform state + the client's pad cursor ----------------
#include "apps/client/ClientGamepad.h"
#include "engine/platform/Gamepad.h"

namespace bwc {
static platform::GamepadState g_fakePad;
static void fakePads(platform::GamepadState (&pads)[platform::kMaxGamepads]) {
    pads[1] = g_fakePad;   // keep prev from the common layer: copy only the live fields
}
} // namespace bwc

TEST(gamepad_state_edges_dead_zone_and_names) {
    using platform::PadButton;
    platform::detail::setGamepadBackend([](platform::GamepadState (&pads)[platform::kMaxGamepads]) {
        bool prev[int(PadButton::Count)];
        std::memcpy(prev, pads[1].prev, sizeof prev);
        bwc::fakePads(pads);
        std::memcpy(pads[1].prev, prev, sizeof prev);
    });
    bwc::g_fakePad = {};
    bwc::g_fakePad.connected = true;
    bwc::g_fakePad.buttons[int(PadButton::A)] = true;
    CHECK(platform::pollGamepads() == 1 && platform::gamepad(1).pressed(PadButton::A) && !platform::gamepad(0).connected);
    platform::pollGamepads();
    CHECK(platform::gamepad(1).down(PadButton::A) && !platform::gamepad(1).pressed(PadButton::A));
    bwc::g_fakePad.buttons[int(PadButton::A)] = false;
    platform::pollGamepads();
    CHECK(platform::gamepad(1).released(PadButton::A));
    bwc::g_fakePad.connected = false;
    bwc::g_fakePad.buttons[int(PadButton::B)] = true;
    CHECK(platform::pollGamepads() == 0 && !platform::gamepad(1).down(PadButton::B));   // gone: all released
    platform::detail::setGamepadBackend(nullptr);
    float x = 0.1f, y = 0.1f;
    platform::applyDeadZone(x, y, 0.2f);
    CHECK(x == 0 && y == 0);
    x = 1; y = 0;
    platform::applyDeadZone(x, y, 0.2f);
    CHECK(bwc::near(x, 1) && y == 0);
    x = 0.6f; y = 0;
    platform::applyDeadZone(x, y, 0.2f);
    CHECK(bwc::near(x, 0.5f));
    CHECK(platform::padButtonFromName("lb") == PadButton::LB && std::string(platform::padButtonName(PadButton::Start)) == "start");
    CHECK(platform::padButtonFromName("nope") == PadButton::Count);
}

TEST(gamepad_cursor_clicks_flicks_and_bindings) {
    using platform::EventType;
    using platform::PadAxis;
    using platform::PadButton;
    client::ClientGamepad g;
    g.configure(Json::parse(R"({"gamepad": {"cursorSpeed": 1000, "buttons": {"x": "key:Q", "y": "key:Num4", "lb": "nonsense"}}})"));
    CHECK(client::ClientGamepad::keyFromName("Escape") == platform::Key::Escape && client::ClientGamepad::keyFromName("7") == platform::Key::Num7);
    platform::GamepadState p;
    p.connected = true;
    std::vector<client::ClientGamepad::Target> none;
    auto f = g.update(p, 0.016f, 800, 600, none);
    CHECK(!g.active() && f.events.empty());   // untouched pad: the mouse keeps the pointer
    // half tilt right for 0.5 s: quadratic speed, starts at the centre
    p.axes[int(PadAxis::LX)] = 0.6f;
    for (int i = 0; i < 50; i++) f = g.update(p, 0.01f, 800, 600, none);
    CHECK(g.active() && g.cursorX() > 400 + 60 && g.cursorX() < 400 + 200 && bwc::near(g.cursorY(), 300));
    CHECK(!f.events.empty() && f.events[0].type == EventType::MouseMove);
    // A press / release = left mouse button at the cursor; X = key Q; d-pad and right stick pan; triggers zoom
    p.axes[int(PadAxis::LX)] = 0;
    p.buttons[int(PadButton::A)] = true;
    p.buttons[int(PadButton::X)] = true;
    p.buttons[int(PadButton::Left)] = true;
    p.axes[int(PadAxis::RT)] = 1;
    f = g.update(p, 0.1f, 800, 600, none);
    bool down = false, q = false, wheel = false;
    for (auto& e : f.events) {
        down |= e.type == EventType::MouseDown && e.button == 0 && bwc::near(e.x, g.cursorX());
        q |= e.type == EventType::KeyDown && e.key == platform::Key::Q;
        wheel |= e.type == EventType::MouseWheel && e.wheelY > 0;
    }
    CHECK(down && q && wheel && f.panX < 0);
    std::memcpy(p.prev, p.buttons, sizeof p.buttons);
    p.buttons[int(PadButton::A)] = false;
    p.axes[int(PadAxis::RT)] = 0;
    f = g.update(p, 0.1f, 800, 600, none);
    CHECK(f.events.size() == 1 && f.events[0].type == EventType::MouseUp && f.events[0].button == 0);
    // flick: a full push jumps onto the nearest target in that direction, holding it doesn't drift
    std::memcpy(p.prev, p.buttons, sizeof p.buttons);
    std::vector<client::ClientGamepad::Target> targets{{g.cursorX() + 200, g.cursorY() + 10}, {g.cursorX() - 150, g.cursorY()}, {g.cursorX() + 400, g.cursorY()}};
    float tx = targets[0].x, ty = targets[0].y;
    p.axes[int(PadAxis::LX)] = 1;
    for (int i = 0; i < 5; i++) f = g.update(p, 0.016f, 800, 600, targets);
    CHECK(bwc::near(g.cursorX(), tx) && bwc::near(g.cursorY(), ty));
    // released near a target: the magnet settles the cursor on it
    p.axes[int(PadAxis::LX)] = 0;
    g.mouseUsed(tx + 30, ty - 20);
    p.buttons[int(PadButton::Y)] = true;   // any input wakes the pad cursor
    for (int i = 0; i < 60; i++) { f = g.update(p, 0.016f, 800, 600, targets); std::memcpy(p.prev, p.buttons, sizeof p.buttons); }
    CHECK(bwc::near(g.cursorX(), tx, 0.5f) && bwc::near(g.cursorY(), ty, 0.5f));
}

// Bunker Wars pad layout: R1 + face = send percent from the origin to the cursor, L1 + face = upgrade / convert,
// A twice = upgrade, d-pad = skill slots, L2 + face = emotions (events), chords swallow the face button's own binding
TEST(gamepad_bunkerwars_chords) {
    using platform::PadAxis;
    using platform::PadButton;
    client::ClientGamepad g;
    g.configure(Json::parse(R"({"gamepad": {"scheme": "bunkerwars", "chords": {"rb+y": "send:90"}}})"));
    platform::GamepadState p;
    p.connected = true;
    std::vector<client::ClientGamepad::Target> none;
    auto step = [&](float dt = 0.016f) { auto f = g.update(p, dt, 800, 600, none); std::memcpy(p.prev, p.buttons, sizeof p.buttons); return f; };
    auto press = [&](PadButton b, bool on) { p.buttons[int(b)] = on; };
    step();
    press(PadButton::RB, true);
    auto f = step();
    CHECK(f.aiming && f.events.empty() && f.actions.empty());
    const float ox = f.originX;
    p.axes[int(PadAxis::LX)] = 0.7f;
    for (int i = 0; i < 20; i++) step(0.02f);   // aim at the target
    p.axes[int(PadAxis::LX)] = 0;
    press(PadButton::B, true);
    f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "send:25" && bwc::near(f.actions[0].originX, ox) && f.actions[0].x > ox + 50);
    CHECK(f.events.empty());   // B's right click is swallowed by the chord
    press(PadButton::B, false);
    f = step();
    CHECK(f.events.empty());   // ... and so is its release
    press(PadButton::Y, true);
    f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "send:90");   // game.json chords override the scheme
    press(PadButton::Y, false); press(PadButton::RB, false);
    f = step();
    CHECK(!f.aiming);
    press(PadButton::LB, true); step();
    press(PadButton::X, true);
    f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "convert:bunker");
    press(PadButton::X, false); press(PadButton::LB, false); step();
    press(PadButton::Down, true);
    f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "ability:2" && f.panY == 0);   // d-pad = skill slots, no pan
    press(PadButton::Down, false); step();
    // A twice within 0.3 s: two clicks and an upgrade on the second press
    press(PadButton::A, true); f = step();
    CHECK(f.actions.empty() && !f.events.empty());
    press(PadButton::A, false); step(0.1f);
    press(PadButton::A, true); f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "upgrade");
    press(PadButton::A, false); step();
    // L2 is a modifier now: emotions, no zoom
    p.axes[int(PadAxis::LT)] = 1; step();
    press(PadButton::Y, true);
    f = step();
    CHECK(f.actions.size() == 1 && f.actions[0].what == "event:emotion4" && f.zoom == 0);
    CHECK(client::ClientGamepad::slotFromName("rt") == client::ClientGamepad::kRT && client::ClientGamepad::slotFromName("zz") == -1);
}

// ---------------- audio.json "chance" (Bunker Wars FMOD playChance) ----------------
TEST(audio_play_chance) {
    auto a = offlineAudio(64);
    std::string err;
    CHECK(!loadBank(*a, R"({"sounds":{"x":{"synth":{"wave":"sine","duration":0.1},"chance":1.5}}})", &err) && err.find("chance must be in [0, 1]") != std::string::npos);
    CHECK(loadBank(*a, R"({"sounds":{
        "never":  {"synth":{"wave":"sine","duration":0.1}, "chance":0, "maxInstances":0},
        "always": {"synth":{"wave":"sine","duration":0.1}, "maxInstances":0},
        "half":   {"synth":{"wave":"sine","duration":0.1}, "chance":0.5, "maxInstances":0, "cooldown":0}}})"));
    CHECK(a->play("never") == audio::kNoVoice && a->play("always") != audio::kNoVoice);
    int started = 0;
    for (int i = 0; i < 400; ++i) started += a->play("half") != audio::kNoVoice;
    CHECK(started > 140 && started < 260);
    a->stopAll(0.0f);
}

// ---------------- gameplay time scale (trigger variable "gameplay_scale": BW tutorial windows freeze the battle) ----------------
TEST(gameplay_scale_freezes_gameplay_systems) {
    using namespace modes_t;
    const std::string r = rules(R"("resources": {"energy": {"start": 0, "regen": 1}})");
    auto income = [&](int pct) {   // pct < 0: no variable
        auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
        auto& gs = mod(*sim).state(*sim);
        if (pct >= 0) gs.trig.vars["gameplay_scale"] = Fixed::fromRatio(pct, 100);
        run(*sim, 20);
        return gs.players[1].stock[0].toInt();
    };
    const int64_t normal = income(-1), frozen = income(0), half = income(50), full = income(100);
    CHECK(normal >= 19 && frozen == 0 && full == normal);
    CHECK(std::abs(half * 2 - normal) <= 1);   // every second tick runs the gameplay systems
}
