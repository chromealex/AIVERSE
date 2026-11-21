// VIVID versioned digital human descriptor. Presentation only, no gameplay
// authority and no dependency on Unreal/MetaHuman proprietary DNA files.
#pragma once
#include "engine/data/Json.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::vivid {

struct HumanSkin {
    std::array<float,3> melaninColor{0.52f,0.34f,0.25f}; // linear RGB
    float melanin=0.42f, hemoglobin=0.42f, scatteringMm=1.2f;
    float roughness=0.42f, oiliness=0.18f, poreScale=0.32f;
    float wrinkleStrength=0.25f, wetness=0.0f;
};
struct HumanEyes {
    std::array<float,3> irisColor{0.18f,0.38f,0.34f};
    float irisRadius=0.54f, pupil=0.36f, corneaIor=1.376f;
    float limbus=0.35f, scleraRedness=0.08f;
    float tearFilm=0.65f, gazeLimitDegrees=35.0f;
};
struct HumanGroom {
    std::array<float,3> color{0.12f,0.07f,0.045f};
    std::uint32_t guideCount=512;
    std::uint32_t segments=8;
    float lengthMeters=0.22f, density=0.65f, curl=0.08f;
    float clump=0.2f, melanin=0.75f, roughness=0.28f;
    float stiffness=0.55f, damping=0.25f, gravity=0.65f, wind=0.15f;
};
struct HumanAnatomy {
    float heightMeters=1.74f, headScale=1.0f;
    float headWidth=1.0f, jawWidth=1.0f, jawLength=1.0f;
    float cheekFullness=0.5f, noseLength=0.5f, noseWidth=0.5f;
    float eyeSpacing=0.5f, eyeScale=1.0f, browHeight=0.5f;
    float mouthWidth=0.5f, lipFullness=0.5f, earScale=1.0f;
    float neckLength=0.5f, shoulderWidth=0.5f, bodyBuild=0.5f;
};
struct HumanRigDNA {
    std::string rootBone="root";
    std::string headBone="head";
    std::string jawBone="jaw";
    std::string leftEyeBone="eye.L";
    std::string rightEyeBone="eye.R";
    std::uint32_t revision=1;
    std::vector<std::string> optionalBones;
};
struct HumanDNA {
    static constexpr std::uint32_t kVersion=1;
    std::uint32_t version=kVersion;
    std::string id="vivid_default";
    std::string displayName="VIVID Human";
    std::uint32_t seed=0x3568a1u;
    HumanAnatomy anatomy{};
    HumanSkin skin{};
    HumanEyes eyes{};
    HumanGroom groom{};
    HumanRigDNA rig{};
    // 52 semantic ARKit-style facial rest weights; additional corrective
    // morphs are driven by the existing FaceShapeBinding mapping.
    std::array<float,52> expressionRest{};

    // Throws std::invalid_argument / JsonError for invalid or nonfinite
    // values, unsupported versions and unsafe resource budgets.
    void validate() const;
    Json toJson() const;
    static HumanDNA fromJson(const Json& source);
    static HumanDNA preset(std::string_view id); // neutral, warm, cool, stylized
};
} // namespace aiverse::vivid
