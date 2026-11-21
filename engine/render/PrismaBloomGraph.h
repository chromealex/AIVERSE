#pragma once
// PRISMA Stage 17: cached, GPU-free Bloom graph topology.
// Persistent per viewport; frame-varying uniforms, handles and pipelines are
// bound separately by the renderer. No gameplay or RHI dependencies.
#include "PrismaRenderGraph.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

struct PrismaBloomMip {
    uint32_t width = 0, height = 0;
    bool operator==(const PrismaBloomMip&) const = default;
};

enum class PrismaBloomPassType : uint8_t { Prefilter, Downsample, Upsample };

struct PrismaBloomPassBinding {
    PrismaBloomPassType type = PrismaBloomPassType::Prefilter;
    PrismaGraphId input0 = kInvalidPrismaGraphId;
    PrismaGraphId input1 = kInvalidPrismaGraphId;
    PrismaGraphId output = kInvalidPrismaGraphId;
    // Index into down mips for Prefilter/Downsample, up mips for Upsample.
    uint32_t mipIndex = 0;
};

class PrismaBloomGraphCache {
public:
    // Returns false on invalid layout; a previously cached valid layout is
    // preserved if rebuilding fails. The source can be scene or an FXAA target.
    bool configure(uint32_t sourceWidth, uint32_t sourceHeight,
                   PrismaGraphFormat format, const std::vector<PrismaBloomMip>& mips,
                   bool graphOwnedTargets = false) {
        if (sourceWidth == 0 || sourceHeight == 0 || mips.empty()) return false;
        for (const auto& mip : mips)
            if (!mip.width || !mip.height) return false;
        if (valid_ && sourceWidth_ == sourceWidth &&
            sourceHeight_ == sourceHeight && format_ == format && mips_ == mips &&
            graphOwnedTargets_ == graphOwnedTargets)
            return true;

        PrismaRenderGraph next;
        const auto source = next.addResource({
            "bloom-chain-source", format, sourceWidth, sourceHeight, true});
        std::vector<PrismaGraphId> down;
        std::vector<PrismaGraphId> up(mips.size(), kInvalidPrismaGraphId);
        down.reserve(mips.size());
        for (size_t i = 0; i < mips.size(); ++i)
            down.push_back(next.addResource({
                "bloom-down-" + std::to_string(i), format,
                mips[i].width, mips[i].height, !graphOwnedTargets}));
        // Append ascending mip indices so external binding ID calculation is
        // straightforward. Execution order is still the descending up chain.
        for (size_t i = 0; i + 1 < mips.size(); ++i)
            up[i] = next.addResource({
                "bloom-up-" + std::to_string(i), format,
                mips[i].width, mips[i].height, true});

        std::vector<PrismaBloomPassBinding> operations;
        operations.reserve(2 * mips.size() - 1);
        auto add = [&](const std::string& name, PrismaBloomPassType kind,
                       PrismaGraphId in0, PrismaGraphId in1,
                       PrismaGraphId output, uint32_t mip) {
            PrismaGraphPass p;
            p.name = name;
            p.kind = PrismaGraphPassKind::Raster;
            p.reads.push_back(in0);
            if (in1 != kInvalidPrismaGraphId) p.reads.push_back(in1);
            p.writes.push_back(output);
            next.addPass(std::move(p));
            operations.push_back({kind, in0, in1, output, mip});
        };
        add("bloom-prefilter", PrismaBloomPassType::Prefilter,
            source, kInvalidPrismaGraphId, down.front(), 0);
        for (size_t i = 1; i < mips.size(); ++i)
            add("bloom-downsample-" + std::to_string(i),
                PrismaBloomPassType::Downsample, down[i-1],
                kInvalidPrismaGraphId, down[i], static_cast<uint32_t>(i));
        auto low = down.back();
        for (size_t i = mips.size() - 1; i-- > 0;) {
            add("bloom-upsample-" + std::to_string(i),
                PrismaBloomPassType::Upsample, low, down[i],
                up[i], static_cast<uint32_t>(i));
            low = up[i];
        }
        const auto compiled = next.compile();
        if (!compiled.valid) return false;

        graph_ = std::move(next);
        plan_ = compiled;
        operations_ = std::move(operations);
        down_ = std::move(down);
        up_ = std::move(up);
        output_ = low;
        source_ = source;
        sourceWidth_ = sourceWidth;
        sourceHeight_ = sourceHeight;
        format_ = format;
        graphOwnedTargets_ = graphOwnedTargets;
        mips_ = mips;
        valid_ = true;
        ++rebuilds_;
        return true;
    }

    bool valid() const { return valid_; }
    uint64_t rebuilds() const { return rebuilds_; }
    bool graphOwnedTargets() const { return graphOwnedTargets_; }
    const PrismaRenderGraph& graph() const { return graph_; }
    const PrismaGraphPlan& plan() const { return plan_; }
    const std::vector<PrismaBloomPassBinding>& operations() const { return operations_; }
    const std::vector<PrismaGraphId>& downIds() const { return down_; }
    const std::vector<PrismaGraphId>& upIds() const { return up_; }
    PrismaGraphId sourceId() const { return source_; }
    PrismaGraphId outputId() const { return output_; }
    const std::vector<PrismaBloomMip>& mips() const { return mips_; }
    void clear() {
        graph_.clear();
        plan_ = {};
        operations_.clear();
        down_.clear();
        up_.clear();
        mips_.clear();
        source_ = output_ = kInvalidPrismaGraphId;
        sourceWidth_ = sourceHeight_ = 0;
        valid_ = false;
    }

private:
    PrismaRenderGraph graph_;
    PrismaGraphPlan plan_;
    std::vector<PrismaBloomPassBinding> operations_;
    std::vector<PrismaGraphId> down_, up_;
    std::vector<PrismaBloomMip> mips_;
    PrismaGraphId source_ = kInvalidPrismaGraphId;
    PrismaGraphId output_ = kInvalidPrismaGraphId;
    PrismaGraphFormat format_ = PrismaGraphFormat::RGBA8;
    uint32_t sourceWidth_ = 0, sourceHeight_ = 0;
    uint64_t rebuilds_ = 0;
    bool valid_ = false;
    bool graphOwnedTargets_ = false;
};

} // namespace aiverse::render
