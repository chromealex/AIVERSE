#include "PrismaCompositor.h"
#include "graph/ShaderGraph.h"

namespace aiverse::render {
namespace {
[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument("postfx.compositor: " + message);
}
}

std::shared_ptr<const PrismaCompositor> PrismaCompositor::compile(const Json& doc) {
    const auto& targets = doc["targets"];
    const auto& passes = doc["passes"];
    if (!targets.isArray() || targets.size() == 0 || targets.size() > maxPasses ||
        !passes.isArray() || passes.size() != targets.size())
        invalid("expected 1–12 targets and one producer per target");

    auto result = std::shared_ptr<PrismaCompositor>(new PrismaCompositor);
    auto& graph = result->graph_;
    std::unordered_map<std::string, PrismaGraphId> resources;
    auto declare = [&](const std::string& id, unsigned divisor, bool external) {
        if (id.empty() || id.size() > 96 || resources.count(id)) invalid("invalid or duplicate target " + id);
        // Normalized dimensions preserve size-class equivalence across window resizes.
        resources.emplace(id, graph.addResource({id, PrismaGraphFormat::RGBA16F, 8 / divisor, 8 / divisor, external}));
        result->divisors_.push_back(divisor);
        result->cameraInputs_.push_back(-1);
    };
    declare("scene", 1, true);
    const auto& sources = doc["sources"];
    if (doc.has("sources") && (!sources.isArray() || sources.size() > maxCameraInputs))
        invalid("expected at most eight camera sources");
    for (const auto& source : sources.items()) {
        const double index = source["cameraInput"].toDouble(-1);
        if (!std::isfinite(index) || index < 0 || index >= maxCameraInputs || index != std::floor(index))
            invalid("cameraInput must index the camera dependency list (0 through 7)");
        declare(source.getStr("id"), 1, true);
        result->cameraInputs_.back() = int(index);
    }
    for (const auto& target : targets.items()) {
        const double div = target.has("divisor") ? target["divisor"].toDouble(0) : 1;
        if (div != 1 && div != 2 && div != 4 && div != 8) invalid("invalid target divisor");
        declare(target.getStr("id"), unsigned(div), false);
    }
    auto lookup = [&](const std::string& id) {
        const auto it = resources.find(id);
        if (it == resources.end()) invalid("unknown target " + id);
        return it->second;
    };
    result->output_ = lookup(doc.getStr("output"));
    if (graph.resources()[result->output_].external || result->divisor(result->output_) != 1) invalid("output must be a full-resolution target");
    std::vector<bool> consumed(resources.size());
    for (const auto& pass : passes.items()) {
        const std::string op = pass.getStr("op");
        PrismaCompositorOp kind;
        if (op == "bright") kind = PrismaCompositorOp::Bright;
        else if (op == "blurX") kind = PrismaCompositorOp::BlurX;
        else if (op == "blurY") kind = PrismaCompositorOp::BlurY;
        else if (op == "downsample") kind = PrismaCompositorOp::Downsample;
        else if (op == "add") kind = PrismaCompositorOp::Add;
        else if (op == "shaderGraph") kind = PrismaCompositorOp::ShaderGraph;
        else invalid("unknown operation " + op);
        const double amount = pass.has("amount") ? pass["amount"].toDouble(-1) : 1;
        if (!std::isfinite(amount) || amount < 0 || amount > 16) invalid("amount must be between 0 and 16");
        const auto& inputs = pass["inputs"];
        if (!inputs.isArray() || inputs.size() != (kind == PrismaCompositorOp::Add ? 2u : 1u))
            invalid("unexpected input count for " + op);
        PrismaGraphPass node;
        node.name = pass.getStr("id");
        if (node.name.empty() || node.name.size() > 96) invalid("invalid pass id");
        for (const auto& input : inputs.items()) {
            const auto id = lookup(input.str());
            node.reads.push_back(id);
            consumed[id] = true;
        }
        const auto dest = lookup(pass.getStr("target"));
        if (graph.resources()[dest].external) invalid("external inputs are read-only");
        node.writes.push_back(dest);
        graph.addPass(std::move(node));
        PrismaCompositorCommand command{kind,float(amount),{},{},{}};
        if (kind == PrismaCompositorOp::ShaderGraph) {
            command.shader = pass["graph"];
            try { command.textures = graph::compile(command.shader, "postfx").textures; }
            catch (const std::exception& e) { invalid(e.what()); }
            command.shaderKey = command.shader.dump(-1);
        }
        result->commands_.push_back(std::move(command));
    }
    result->plan_ = graph.compile();
    if (!result->plan_.valid) invalid(result->plan_.error);
    for (PrismaGraphId id = 1; id < consumed.size(); ++id)
        if (consumed[id] != (id != result->output_)) invalid("expected a single output and no disconnected branches");
    return result;
}
} // namespace aiverse::render
