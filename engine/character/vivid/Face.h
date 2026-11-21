// VIVID semantic facial animation. Presentation only, no renderer/genre dependencies.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aiverse::vivid {

// Canonical order matches Apple's 52 ARKit facial blend shape semantics.
// Values are normalized to [0, 1]. Additional custom channels belong to a
// future, versioned rig schema; do not silently reinterpret this enum.
enum class Channel : std::uint8_t {
    BrowDownLeft, BrowDownRight, BrowInnerUp, BrowOuterUpLeft, BrowOuterUpRight,
    CheekPuff, CheekSquintLeft, CheekSquintRight,
    EyeBlinkLeft, EyeBlinkRight, EyeLookDownLeft, EyeLookDownRight,
    EyeLookInLeft, EyeLookInRight, EyeLookOutLeft, EyeLookOutRight,
    EyeLookUpLeft, EyeLookUpRight, EyeSquintLeft, EyeSquintRight,
    EyeWideLeft, EyeWideRight,
    JawForward, JawLeft, JawOpen, JawRight,
    MouthClose, MouthDimpleLeft, MouthDimpleRight, MouthFrownLeft,
    MouthFrownRight, MouthFunnel, MouthLeft, MouthLowerDownLeft,
    MouthLowerDownRight, MouthPressLeft, MouthPressRight, MouthPucker,
    MouthRight, MouthRollLower, MouthRollUpper, MouthShrugLower,
    MouthShrugUpper, MouthSmileLeft, MouthSmileRight, MouthStretchLeft,
    MouthStretchRight, MouthUpperUpLeft, MouthUpperUpRight,
    NoseSneerLeft, NoseSneerRight, TongueOut,
    Count
};
inline constexpr std::size_t kChannelCount = static_cast<std::size_t>(Channel::Count);
static_assert(kChannelCount == 52);

std::string_view channelName(Channel channel) noexcept;
// Strict, case-sensitive lookup; unknown names return false.
bool channelFromName(std::string_view name, Channel& out) noexcept;

enum class Layer : std::uint8_t { Tracking, Emotion, Speech, Count };
enum class Emotion : std::uint8_t { Neutral, Happy, Sad, Angry, Surprise, Disgust, Fear };
enum class Viseme : std::uint8_t { Silence, Aa, Ee, Oh, Fv, Mbp };

struct FaceFrame {
    std::array<float, kChannelCount> values{};
    float value(Channel channel) const noexcept {
        const auto index = static_cast<std::size_t>(channel);
        return index < values.size() ? values[index] : 0.0f;
    }
};

// Instance-owned, allocation-free mixer. Sources are applied in order:
// Tracking -> Emotion -> Speech. Only explicitly set channels in a layer
// participate; speech can override a mouth channel without zeroing others.
// Source weights are crossfade factors, not gain multipliers.
// advance(dt) applies exponential time smoothing; snap() commits targets now.
class FaceController {
public:
    explicit FaceController(float responseSeconds = 0.075f) noexcept;

    // Non-finite weights are rejected, leaving state untouched. Finite
    // values outside [0,1] are clamped to the range.
    bool set(Layer layer, Channel channel, float value) noexcept;
    bool set(Layer layer, std::string_view channel, float value) noexcept;
    bool setLayerWeight(Layer layer, float weight) noexcept;
    bool setResponseSeconds(float seconds) noexcept;
    float responseSeconds() const noexcept { return responseSeconds_; }

    void clear(Layer layer) noexcept;
    void reset() noexcept;  // restores neutral frame and default layer weights
    bool setEmotion(Emotion emotion, float intensity = 1.0f) noexcept;
    bool setViseme(Viseme viseme, float intensity = 1.0f) noexcept;

    // Non-finite or negative dt is rejected (false), with no state changes.
    // Zero dt preserves the current frame.
    bool advance(float dt) noexcept;
    void snap() noexcept;
    const FaceFrame& frame() const noexcept { return frame_; }

private:
    struct Source {
        FaceFrame frame;
        std::bitset<kChannelCount> active;
    };
    static constexpr std::size_t kLayerCount = static_cast<std::size_t>(Layer::Count);
    std::array<Source, kLayerCount> sources_{};
    std::array<float, kLayerCount> layerWeights_{1, 1, 1};
    FaceFrame frame_{};
    float responseSeconds_ = 0.075f;
    FaceFrame target() const noexcept;
};

} // namespace aiverse::vivid
