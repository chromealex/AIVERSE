#pragma once
#include "PrismaRenderGraph.h"
#include "engine/data/Json.h"
#include <cmath>
#include <memory>
#include <stdexcept>

namespace aiverse::render {

enum class PrismaCompositorOp { Downsample, Bright, BlurX, BlurY, Add, ShaderGraph };
struct PrismaCompositorCommand {
    PrismaCompositorOp op;
    float amount;
    Json shader;
    std::string shaderKey;
    std::vector<std::string> textures;
};

// Runtime data is created only by the validating compiler. IDs are stable within a snapshot.
class PrismaCompositor {
public:
    static constexpr unsigned maxPasses = 12;
    static constexpr unsigned maxCameraInputs = 8;
    static std::shared_ptr<const PrismaCompositor> compile(const Json& document);
    const auto& graph() const { return graph_; }
    const auto& plan() const { return plan_; }
    const auto& commands() const { return commands_; }
    unsigned divisor(PrismaGraphId resource) const { return divisors_.at(resource); }
    int cameraInput(PrismaGraphId id) const { return cameraInputs_.at(id); }
    PrismaGraphId output() const { return output_; }

private:
    PrismaCompositor() = default;
    PrismaRenderGraph graph_;
    PrismaGraphPlan plan_;
    std::vector<unsigned> divisors_;
    std::vector<int> cameraInputs_;
    std::vector<PrismaCompositorCommand> commands_;
    PrismaGraphId output_ = kInvalidPrismaGraphId;
};

} // namespace aiverse::render
