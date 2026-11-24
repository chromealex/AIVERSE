#pragma once
#include "engine/mechanics/Namespace.h"
// vehicle-fx: lamp channels of a vehicle for its look. Headlights / tail lamps, brake lamps that stay lit for a
// moment after hard deceleration, reverse lamps, indicators and hazards that blink, emergency beacons with a
// double-flash strobe (left and right out of phase), a roof sign ("available"). The model names its emissive
// parts by channel; the view asks level(channel, side, time) every frame. Presentation only (float, wall or
// scene time): drive it from simulation state, never write back.
#include <algorithm>
#include <cmath>
#include <string_view>

namespace aiverse::lib::mechanics {

enum class VehicleLamp { None, Headlight, Tail, Brake, Reverse, Indicator, Beacon, RoofSign, Destination };

// Channel names used by models: "headlight", "tail", "brake", "reverse", "indicator*", "beacon*" / "strobe",
// "roof_sign" (alias "taxi_available"), "destination" (bus destination board: lit with the headlights).
inline VehicleLamp vehicleLampOf(std::string_view channel) {
    auto starts = [&](std::string_view p) { return channel.substr(0, p.size()) == p; };
    if (channel == "headlight") return VehicleLamp::Headlight;
    if (channel == "tail") return VehicleLamp::Tail;
    if (channel == "brake") return VehicleLamp::Brake;
    if (channel == "reverse") return VehicleLamp::Reverse;
    if (starts("indicator")) return VehicleLamp::Indicator;
    if (starts("beacon") || channel == "strobe") return VehicleLamp::Beacon;
    if (channel == "roof_sign" || channel == "taxi_available") return VehicleLamp::RoofSign;
    if (channel == "destination") return VehicleLamp::Destination;
    return VehicleLamp::None;
}

struct VehicleLightsConfig {
    double blinkPeriod = 0.75, blinkOn = 0.375;      // indicators / hazards, seconds
    double brakeHold = 0.35;                         // brake lamps stay lit after a hard deceleration
    float brakeDecel = 1.5f, brakeMinSpeed = 0.4f;   // speed units per second / speed units
    float reverseSpeed = 0.25f;                      // signed speed below -this lights the reverse lamps
    double strobePeriod = 0.8, strobeRightOffset = 0.4;
    double flash1 = 0.12, flash2From = 0.19, flash2To = 0.31;   // double flash inside one strobe period
};

struct VehicleLights {
    bool headlights = true, left = false, right = false, hazards = false, beacons = false, siren = false;
    bool roofSign = true, braking = false, reversing = false;
    double brakeUntil = 0;

    // Once per frame with the speed before and after (absolute), the signed speed and whether the brake pedal is down.
    void motion(float oldSpeed, float speed, float signedSpeed, double dt, double now, bool pedal,
                const VehicleLightsConfig& c = {}) {
        if (dt > 0 && oldSpeed - speed > float(c.brakeDecel * dt) && oldSpeed > c.brakeMinSpeed) brakeUntil = now + c.brakeHold;
        braking = pedal || now < brakeUntil;
        reversing = signedSpeed < -c.reverseSpeed;
    }
    // Turn signals from the steering: indicators on while the steer is past `threshold` (-1..1, right positive).
    void indicateFromSteer(float steer, float threshold = 0.35f) { left = steer < -threshold; right = steer > threshold; }

    bool blinkPhase(double time, const VehicleLightsConfig& c = {}) const {
        return c.blinkPeriod > 0 && std::fmod(time, c.blinkPeriod) < c.blinkOn;
    }
    bool strobePhase(bool leftSide, double time, const VehicleLightsConfig& c = {}) const {
        if (c.strobePeriod <= 0) return false;
        const double phase = std::fmod(time + (leftSide ? 0 : c.strobeRightOffset), c.strobePeriod);
        return phase < c.flash1 || (phase > c.flash2From && phase < c.flash2To);
    }
    float level(VehicleLamp lamp, bool leftSide, double time, const VehicleLightsConfig& c = {}) const {
        switch (lamp) {
        case VehicleLamp::Headlight: case VehicleLamp::Tail: case VehicleLamp::Destination: return headlights ? 1.f : 0.f;
        case VehicleLamp::Brake: return braking ? 1.f : 0.f;
        case VehicleLamp::Reverse: return reversing ? 1.f : 0.f;
        case VehicleLamp::Indicator: return blinkPhase(time, c) && (hazards || (leftSide ? left : right)) ? 1.f : 0.f;
        case VehicleLamp::Beacon: return beacons && strobePhase(leftSide, time, c) ? 1.f : 0.f;
        case VehicleLamp::RoofSign: return roofSign ? 1.f : 0.f;
        case VehicleLamp::None: break;
        }
        return 0.f;
    }
    float level(std::string_view channel, bool leftSide, double time, const VehicleLightsConfig& c = {}) const {
        return level(vehicleLampOf(channel), leftSide, time, c);
    }
};

} // namespace aiverse::lib::mechanics
