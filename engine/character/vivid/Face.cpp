#include "Face.h"

#include <algorithm>
#include <cmath>

namespace aiverse::vivid {
namespace {
constexpr std::array<std::string_view, kChannelCount> kNames{{
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight",
    "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
    "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight",
    "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
    "eyeWideLeft", "eyeWideRight",
    "jawForward", "jawLeft", "jawOpen", "jawRight",
    "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
    "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft",
    "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight", "mouthPucker",
    "mouthRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower",
    "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft",
    "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight",
    "noseSneerLeft", "noseSneerRight", "tongueOut"
}};
static_assert(kNames.size() == 52);

constexpr std::size_t index(Channel c) noexcept {
    return static_cast<std::size_t>(c);
}
constexpr std::size_t index(Layer layer) noexcept {
    return static_cast<std::size_t>(layer);
}
float unit(float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }
} // namespace

std::string_view channelName(Channel channel) noexcept {
    const std::size_t i = index(channel);
    return i < kNames.size() ? kNames[i] : std::string_view{};
}
bool channelFromName(std::string_view name, Channel& out) noexcept {
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (kNames[i] == name) {
            out = static_cast<Channel>(i);
            return true;
        }
    }
    return false;
}

FaceController::FaceController(float seconds) noexcept {
    setResponseSeconds(seconds);
}
bool FaceController::set(Layer layer, Channel channel, float value) noexcept {
    const auto li = index(layer), ci = index(channel);
    if (li >= sources_.size() || ci >= kChannelCount || !std::isfinite(value)) return false;
    sources_[li].frame.values[ci] = unit(value);
    sources_[li].active.set(ci);
    return true;
}
bool FaceController::set(Layer layer, std::string_view name, float value) noexcept {
    Channel channel{};
    return channelFromName(name, channel) && set(layer, channel, value);
}
bool FaceController::setLayerWeight(Layer layer, float weight) noexcept {
    const auto li = index(layer);
    if (li >= layerWeights_.size() || !std::isfinite(weight)) return false;
    layerWeights_[li] = unit(weight);
    return true;
}
bool FaceController::setResponseSeconds(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0f) return false;
    responseSeconds_ = seconds;
    return true;
}
void FaceController::clear(Layer layer) noexcept {
    const auto li = index(layer);
    if (li < sources_.size()) sources_[li] = {};
}
void FaceController::reset() noexcept {
    for (Source& source : sources_) source = {};
    layerWeights_.fill(1.0f);
    frame_ = {};
}
bool FaceController::setEmotion(Emotion emotion, float intensity) noexcept {
    if (!std::isfinite(intensity) || static_cast<unsigned>(emotion) > static_cast<unsigned>(Emotion::Fear)) return false;
    const float w = unit(intensity);
    clear(Layer::Emotion);
    auto apply = [&](Channel c, float value) { set(Layer::Emotion, c, value * w); };
    switch (emotion) {
        case Emotion::Neutral: break;
        case Emotion::Happy:
            apply(Channel::MouthSmileLeft, 0.9f); apply(Channel::MouthSmileRight, 0.9f);
            apply(Channel::CheekSquintLeft, 0.4f); apply(Channel::CheekSquintRight, 0.4f);
            break;
        case Emotion::Sad:
            apply(Channel::BrowInnerUp, 0.75f);
            apply(Channel::MouthFrownLeft, 0.8f); apply(Channel::MouthFrownRight, 0.8f);
            break;
        case Emotion::Angry:
            apply(Channel::BrowDownLeft, 0.85f); apply(Channel::BrowDownRight, 0.85f);
            apply(Channel::EyeSquintLeft, 0.4f); apply(Channel::EyeSquintRight, 0.4f);
            apply(Channel::MouthPressLeft, 0.35f); apply(Channel::MouthPressRight, 0.35f);
            break;
        case Emotion::Surprise:
            apply(Channel::BrowInnerUp, 0.9f);
            apply(Channel::BrowOuterUpLeft, 0.75f); apply(Channel::BrowOuterUpRight, 0.75f);
            apply(Channel::EyeWideLeft, 0.8f); apply(Channel::EyeWideRight, 0.8f);
            apply(Channel::JawOpen, 0.45f);
            break;
        case Emotion::Disgust:
            apply(Channel::NoseSneerLeft, 0.8f); apply(Channel::NoseSneerRight, 0.8f);
            apply(Channel::MouthUpperUpLeft, 0.45f); apply(Channel::MouthUpperUpRight, 0.45f);
            break;
        case Emotion::Fear:
            apply(Channel::EyeWideLeft, 0.85f); apply(Channel::EyeWideRight, 0.85f);
            apply(Channel::BrowInnerUp, 0.7f);
            apply(Channel::MouthStretchLeft, 0.6f); apply(Channel::MouthStretchRight, 0.6f);
            break;
    }
    return true;
}
bool FaceController::setViseme(Viseme viseme, float intensity) noexcept {
    if (!std::isfinite(intensity) || static_cast<unsigned>(viseme) > static_cast<unsigned>(Viseme::Mbp)) return false;
    const float w = unit(intensity);
    clear(Layer::Speech);
    auto apply = [&](Channel c, float value) { set(Layer::Speech, c, value * w); };
    switch (viseme) {
        case Viseme::Silence: break;
        case Viseme::Aa:
            apply(Channel::JawOpen, 0.95f); apply(Channel::MouthFunnel, 0.15f);
            break;
        case Viseme::Ee:
            apply(Channel::MouthStretchLeft, 0.7f); apply(Channel::MouthStretchRight, 0.7f);
            apply(Channel::JawOpen, 0.25f);
            break;
        case Viseme::Oh:
            apply(Channel::MouthFunnel, 0.85f); apply(Channel::MouthPucker, 0.65f);
            apply(Channel::JawOpen, 0.45f);
            break;
        case Viseme::Fv:
            apply(Channel::MouthLowerDownLeft, 0.65f); apply(Channel::MouthLowerDownRight, 0.65f);
            apply(Channel::MouthPressLeft, 0.4f); apply(Channel::MouthPressRight, 0.4f);
            break;
        case Viseme::Mbp:
            apply(Channel::MouthClose, 1.0f);
            apply(Channel::MouthPressLeft, 0.6f); apply(Channel::MouthPressRight, 0.6f);
            break;
    }
    return true;
}
FaceFrame FaceController::target() const noexcept {
    FaceFrame result;
    for (std::size_t l = 0; l < sources_.size(); ++l) {
        const Source& source = sources_[l];
        const float weight = layerWeights_[l];
        if (weight <= 0.0f) continue;
        for (std::size_t i = 0; i < kChannelCount; ++i) {
            if (!source.active.test(i)) continue;
            result.values[i] += (source.frame.values[i] - result.values[i]) * weight;
        }
    }
    return result;
}
bool FaceController::advance(float dt) noexcept {
    if (!std::isfinite(dt) || dt < 0.0f) return false;
    if (dt == 0.0f) return true;
    const FaceFrame desired = target();
    const float factor = responseSeconds_ == 0.0f
        ? 1.0f : -std::expm1(-dt / responseSeconds_);
    for (std::size_t i = 0; i < kChannelCount; ++i)
        frame_.values[i] += (desired.values[i] - frame_.values[i]) * factor;
    return true;
}
void FaceController::snap() noexcept { frame_ = target(); }

} // namespace aiverse::vivid
