// Native, headless VIVID facial runtime tests. No render/device dependencies.
#include "engine/character/vivid/Face.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace aiverse::vivid;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}
void names() {
    std::unordered_set<std::string> unique;
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        const auto c = static_cast<Channel>(i);
        const auto name = channelName(c);
        check(!name.empty(), "channel name missing");
        check(unique.emplace(name).second, "duplicate channel name");
        Channel parsed = Channel::Count;
        check(channelFromName(name, parsed) && parsed == c, "channel lookup roundtrip");
    }
    check(unique.size() == 52, "wrong ARKit channel count");
    check(channelName(Channel::Count).empty(), "invalid channel returned a name");
    Channel untouched = Channel::JawOpen;
    check(!channelFromName("jawopen", untouched) && untouched == Channel::JawOpen, "case-insensitive match not allowed");
    check(!channelFromName("", untouched), "empty name matched");
}
void validation() {
    FaceController f(0.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check(!f.set(Layer::Tracking, Channel::JawOpen, nan), "accepted NaN value");
    check(!f.set(Layer::Tracking, Channel::JawOpen, inf), "accepted Inf value");
    check(!f.set(Layer::Tracking, "unknownShape", 0.5f), "accepted unknown channel");
    check(!f.set(Layer::Count, Channel::JawOpen, 0.5f), "accepted invalid layer");
    check(!f.set(Layer::Tracking, Channel::Count, 0.5f), "accepted invalid channel");
    check(f.set(Layer::Tracking, Channel::JawOpen, -2.0f), "negative input not sanitized");
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.0f), "negative not clamped");
    check(f.set(Layer::Tracking, "jawOpen", 4.0f), "positive channel not set");
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 1.0f), "positive not clamped");
    check(!f.setLayerWeight(Layer::Tracking, nan), "NaN layer weight accepted");
    check(!f.setResponseSeconds(-1.0f), "negative response accepted");
    check(!f.setResponseSeconds(inf), "infinite response accepted");
    check(!f.advance(nan) && !f.advance(-0.1f), "invalid dt accepted");
    check(near(f.frame().value(Channel::JawOpen), 1.0f), "invalid dt changed state");
    check(!f.setEmotion(static_cast<Emotion>(99)), "unknown emotion accepted");
    check(!f.setViseme(static_cast<Viseme>(99)), "unknown viseme accepted");
    check(!f.setEmotion(Emotion::Happy, nan), "NaN emotion accepted");
    check(!f.setViseme(Viseme::Aa, nan), "NaN viseme accepted");
}
void layering() {
    FaceController f(0.0f);
    check(f.set(Layer::Tracking, Channel::JawOpen, 0.4f), "tracking set");
    check(f.set(Layer::Tracking, Channel::EyeBlinkLeft, 0.7f), "blink set");
    check(f.setEmotion(Emotion::Happy), "happy emotion set");
    check(f.setViseme(Viseme::Oh), "oh viseme set");
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.45f), "speech did not override jaw");
    check(near(f.frame().value(Channel::MouthSmileLeft), 0.9f), "emotion smile lost");
    check(near(f.frame().value(Channel::EyeBlinkLeft), 0.7f), "untouched tracking channel lost");
    check(f.setLayerWeight(Layer::Speech, 0.5f), "speech layer weight");
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.425f), "speech crossfade wrong");
    f.clear(Layer::Speech);
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.4f), "cleared speech should restore tracking");
    check(f.setEmotion(Emotion::Neutral), "neutral emotion");
    f.snap();
    check(near(f.frame().value(Channel::MouthSmileLeft), 0.0f), "neutral didn't clear expression");
    check(f.setViseme(Viseme::Mbp), "closed lip viseme");
    f.snap();
    check(near(f.frame().value(Channel::MouthClose), 0.5f), "speech weight not applied");
    f.reset();
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.0f), "reset not neutral");
    check(f.setViseme(Viseme::Mbp), "post-reset viseme");
    f.snap();
    check(near(f.frame().value(Channel::MouthClose), 1.0f), "reset didn't restore layer weights");
}
void smoothing() {
    FaceController f(0.1f);
    check(f.set(Layer::Tracking, Channel::JawOpen, 1.0f), "tracking input rejected");
    check(f.advance(0.0f), "dt zero rejected");
    check(near(f.frame().value(Channel::JawOpen), 0.0f), "dt zero changed frame");
    check(f.advance(0.1f), "dt 0.1 rejected");
    check(near(f.frame().value(Channel::JawOpen), 1.0f - std::exp(-1.0f)), "response curve incorrect");
    check(f.advance(100.0f), "large dt rejected");
    check(near(f.frame().value(Channel::JawOpen), 1.0f), "long step did not converge");
    f.clear(Layer::Tracking);
    check(f.advance(0.1f), "clear smoothing rejected");
    check(f.frame().value(Channel::JawOpen) > 0.0f && f.frame().value(Channel::JawOpen) < 1.0f, "outgoing fade incorrect");
    f.snap();
    check(near(f.frame().value(Channel::JawOpen), 0.0f), "snap failed");
    check(f.setResponseSeconds(0.0f), "instant response setup");
    check(f.set(Layer::Tracking, Channel::JawOpen, 0.35f), "instant input");
    check(f.advance(0.001f), "instant frame advance");
    check(near(f.frame().value(Channel::JawOpen), 0.35f), "instant response failed");
}
void isolationAndPresets() {
    FaceController a(0), b(0);
    check(a.setEmotion(Emotion::Sad), "sad preset");
    a.snap(); b.snap();
    check(a.frame().value(Channel::MouthFrownLeft) > 0.0f, "sad preset ineffective");
    check(near(b.frame().value(Channel::MouthFrownLeft), 0.0f), "instance state leaked");
    check(a.setEmotion(Emotion::Angry, 0.4f), "angry preset");
    a.snap();
    check(near(a.frame().value(Channel::BrowDownLeft), 0.85f * 0.4f), "emotion intensity wrong");
    check(near(a.frame().value(Channel::MouthFrownLeft), 0.0f), "previous emotion not cleared");
    check(a.setEmotion(Emotion::Surprise), "surprise");
    check(a.setEmotion(Emotion::Disgust), "disgust");
    check(a.setEmotion(Emotion::Fear), "fear");
    check(a.setViseme(Viseme::Aa), "aa");
    check(a.setViseme(Viseme::Ee), "ee");
    check(a.setViseme(Viseme::Fv), "fv");
    check(a.setViseme(Viseme::Silence), "silence");
    a.snap();
    check(near(a.frame().value(Channel::MouthClose), 0.0f), "silence left speech artifacts");
}
} // namespace

int main() {
    try {
        names();
        validation();
        layering();
        smoothing();
        isolationAndPresets();
        std::cout << "VIVID facial core PASS (52 channels, validation, layering, smoothing, presets)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "VIVID facial core FAIL: " << ex.what() << '\n';
        return 1;
    }
}
