// Maps VIVID's 52 semantic expression channels to any number of packed morph
// targets without being limited to the legacy render::Pose[8] interface.
#pragma once

#include "Face.h"
#include "SparseMorph.h"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {

struct ShapeRoute {
    Channel channel = Channel::JawOpen;
    std::string targetName;
    float gain = 1.0f; // [0,4]; result is saturated to [0,1]
};

// Immutable route table, shared by instances; callers own one weight buffer
// per character. Routes are validated at asset load/compile, not every frame.
class FaceShapeBinding {
public:
    // Throws std::invalid_argument on unknown/empty target name, bad channel
    // or gain, and multiple routes to the same target. A semantic channel can
    // legitimately drive several different named corrective shapes.
    static FaceShapeBinding compile(const SparseMorphLibrary& library,
                                    std::span<const ShapeRoute> routes);

    // False without changing output on invalid args / non-finite used channel.
    // Clears unmapped weights to zero on success. No allocation per frame.
    bool evaluate(const FaceFrame& face, std::span<float> weights,
                  float influence = 1.0f) const noexcept;

    std::size_t targets() const noexcept { return targetCount_; }
    std::size_t routes() const noexcept { return routes_.size(); }

private:
    struct CompiledRoute { Channel channel; std::size_t target; float gain; };
    std::size_t targetCount_ = 0;
    std::vector<CompiledRoute> routes_;
};

} // namespace aiverse::vivid
