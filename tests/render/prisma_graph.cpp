// GPU-free PRISMA render-graph contracts; run through ./run-tests.sh and CTest.
#include "engine/render/PrismaRenderGraph.h"
#include "engine/render/PrismaGraphDispatch.h"
#include "engine/render/PrismaGraphResourcePool.h"
#include "engine/render/PrismaRhiExecutor.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace aiverse::render;

namespace {
int errors = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++errors; std::fprintf(stderr, "FAIL: %s\n", message); }
}
PrismaGraphResource resource(const char* name, bool external = false,
                             PrismaGraphFormat format = PrismaGraphFormat::RGBA16F,
                             uint32_t w = 64, uint32_t h = 64) {
    PrismaGraphResource r;
    r.name = name; r.external = external; r.format = format;
    r.width = w; r.height = h;
    return r;
}
PrismaGraphPass pass(const char* name, std::vector<PrismaGraphId> reads,
                     std::vector<PrismaGraphId> writes,
                     std::vector<PrismaGraphId> after = {}) {
    PrismaGraphPass p;
    p.name = name;
    p.reads = std::move(reads);
    p.writes = std::move(writes);
    p.after = std::move(after);
    return p;
}
void positive() {
    PrismaRenderGraph g;
    auto empty = g.compile();
    check(empty.valid && empty.order.empty() && empty.bytes == 0, "empty graph is valid");
    auto source = g.addResource(resource("SceneColor", true));
    auto a = g.addResource(resource("BloomA"));
    auto b = g.addResource(resource("BloomB"));
    auto c = g.addResource(resource("BloomC"));
    auto out = g.addResource(resource("Output", true, PrismaGraphFormat::RGBA8));
    // Deliberately declare the consumer before its producer.
    g.addPass(pass("display", {c}, {out}));
    g.addPass(pass("prefilter", {source}, {a}));
    g.addPass(pass("downsample", {a}, {b}));
    g.addPass(pass("upsample", {b}, {c}));
    auto plan = g.compile();
    check(plan.valid, "dependency graph is valid");
    check(plan.order == std::vector<PrismaGraphId>({1,2,3,0}), "topological producer-before-consumer");
    check(plan.resources[source].slot == -1 && plan.resources[out].slot == -1, "externals have no graph allocation");
    check(plan.resources[a].slot >= 0 && plan.resources[a].slot == plan.resources[c].slot,
          "non-overlapping equal descriptors alias");
    check(plan.resources[a].slot != plan.resources[b].slot, "overlapping lifetimes never alias");
    check(plan.slots.size() == 2 && plan.bytes == 2 * 64ull * 64ull * 8ull, "two HDR transient slots");
    check(plan.resources[a].first == 0 && plan.resources[a].last == 1, "lifetime from execution order");
    check(plan.resources[c].first == 2 && plan.resources[c].last == 3, "later lifetime");
}
void hazards() {
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("feedback", {a}, {a}));
        check(!g.compile().valid, "in-place feedback rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("a1", {}, {a}));
        g.addPass(pass("a2", {}, {a}));
        check(!g.compile().valid, "double-writer rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("sink", {a}, {}));
        check(!g.compile().valid, "missing transient producer rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        auto b = g.addResource(resource("b"));
        g.addPass(pass("a-write", {b}, {a}));
        g.addPass(pass("b-write", {a}, {b}));
        auto p=g.compile();
        check(!p.valid && p.error.find("cycle") != std::string::npos, "resource dependency cycle rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {}, {}, {1}));
        g.addPass(pass("b", {}, {}, {0}));
        check(!g.compile().valid, "explicit cycle rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {}, {}, {0}));
        check(!g.compile().valid, "self dependency rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {kInvalidPrismaGraphId}, {}));
        check(!g.compile().valid, "invalid resource ID rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("dup", true));
        g.addResource(resource("dup", true));
        check(!g.compile().valid, "duplicate resource names rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("dup", {}, {}));
        g.addPass(pass("dup", {}, {}));
        check(!g.compile().valid, "duplicate pass names rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("unwritten"));
        check(!g.compile().valid, "unused transient without producer rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("zero",true,PrismaGraphFormat::RGBA8,0,128));
        check(!g.compile().valid, "zero-sized resource rejected");
    }
    {
        PrismaRenderGraph g;
        auto prev = resource("prev-frame", true);
        prev.history = true; // resource lifetime belongs to the host
        auto in = g.addResource(prev);
        auto out = g.addResource(resource("current-frame", true));
        g.addPass(pass("temporal resolve", {in}, {out}));
        const auto p = g.compile();
        check(p.valid && p.slots.empty(), "caller-managed external history is accepted");
    }
}
void resources() {
    PrismaRenderGraph g;
    auto input = g.addResource(resource("input", true));
    auto history = resource("history");
    history.history = true;
    auto h = g.addResource(history);
    auto temp = resource("temp");
    temp.allowAlias = false;
    auto t = g.addResource(temp);
    auto later = g.addResource(resource("later"));
    auto out = g.addResource(resource("output", true));
    g.addPass(pass("fill history", {input}, {h}));
    g.addPass(pass("consume history", {h}, {t}));
    g.addPass(pass("fill later", {t}, {later}));
    g.addPass(pass("send output", {later}, {out}));
    const auto p = g.compile();
    check(p.valid, "history/exclusive resources accepted");
    check(p.slots.size() == 3, "history and exclusive targets do not alias");
    check(p.resources[h].slot != p.resources[later].slot, "history never aliased");
    check(p.resources[t].slot != p.resources[later].slot, "allowAlias=false");
    PrismaRenderGraph independentlyOrdered;
    independentlyOrdered.addPass(pass("first", {}, {}));
    independentlyOrdered.addPass(pass("second", {}, {}));
    independentlyOrdered.addPass(pass("third", {}, {}));
    check(independentlyOrdered.compile().order == std::vector<PrismaGraphId>({0,1,2}),
          "independent passes preserve registration order");
}

void dispatchContract() {
    PrismaRenderGraph graph;
    const auto in = graph.addResource(resource("scene", true));
    const auto temp = graph.addResource(resource("intermediate"));
    const auto out = graph.addResource(resource("screen", true));
    // Register consumer first to ensure dispatcher uses sorted order.
    graph.addPass(pass("present", {temp}, {out}));
    graph.addPass(pass("process", {in}, {temp}));
    const auto plan = graph.compile();
    check(plan.valid && plan.slots.size() == 1, "dispatch graph compiled");
    if (!plan.valid || plan.slots.size() != 1) return;
    PrismaGraphBindings<int> bindings;
    bindings.external = {11, 0, 33};
    bindings.slots = {22};
    std::vector<PrismaGraphId> sequence;
    auto run = [&](const PrismaGraphPass&, PrismaGraphId id,
                   const PrismaGraphDispatchView<int>& view) {
        sequence.push_back(id);
        check(view.resource(in) == 11 && view.resource(temp) == 22 &&
              view.resource(out) == 33, "dispatch bindings resolve physical handles");
        return true;
    };
    const auto success = dispatchPrismaGraph(graph, plan, bindings, run);
    check(success.success && success.executedPasses == 2, "dispatch invokes both passes");
    check(sequence == std::vector<PrismaGraphId>({1,0}), "dispatch follows topological order");

    bindings.slots[0] = 0; sequence.clear();
    const auto missing = dispatchPrismaGraph(graph, plan, bindings, run);
    check(!missing.success && missing.executedPasses == 0 && sequence.empty(),
          "unbound transient rejected before callbacks");
    bindings.slots[0] = 22;
    bindings.external[out] = 0;
    const auto badOutput = dispatchPrismaGraph(graph, plan, bindings, run);
    check(!badOutput.success && badOutput.executedPasses == 0 && sequence.empty(),
          "unbound external output rejected before callbacks");
    bindings.external[out] = 33;
    auto stop = dispatchPrismaGraph(graph, plan, bindings,
        [&](const PrismaGraphPass&, PrismaGraphId, const PrismaGraphDispatchView<int>&) {
            return false;
        });
    check(!stop.success && stop.executedPasses == 0 &&
          stop.error.find("process") != std::string::npos,
          "callback aborts without invoking later passes");
    auto stale = plan;
    stale.order[1] = stale.order[0];
    const auto invalid = dispatchPrismaGraph(graph, stale, bindings, run);
    check(!invalid.success && invalid.executedPasses == 0, "duplicate pass plan rejected");
}

void poolContract() {
    PrismaRenderGraph graph;
    auto scene = graph.addResource(resource("scene", true));
    auto tmp = graph.addResource(resource("transient"));
    auto history = resource("taa-history");
    history.history = true;
    auto temporal = graph.addResource(history);
    auto screen = graph.addResource(resource("screen", true));
    graph.addPass(pass("sample", {scene}, {tmp}));
    graph.addPass(pass("history", {tmp}, {temporal}));
    graph.addPass(pass("present", {temporal}, {screen}));
    const auto plan = graph.compile();
    check(plan.valid && plan.slots.size() == 2, "resource pool test graph compiled");
    if (!plan.valid || plan.slots.size() != 2) return;

    PrismaGraphResourcePool<int> pool;
    int nextHandle = 100;
    std::vector<int> released;
    auto create = [&](PrismaGraphFormat, uint32_t, uint32_t) { return nextHandle++; };
    auto release = [&](int h) { released.push_back(h); };
    auto first = pool.prepare(graph, plan, create, release);
    check(first.success && first.slots.size() == 2 && pool.entries().size() == 2,
          "pool first allocation");
    check(!first.historyValid[0] && !first.historyValid[1],
          "history initially uninitialized");
    pool.markHistoryWritten("taa-history");
    auto second = pool.prepare(graph, plan, create, release);
    check(second.success && second.slots == first.slots && nextHandle == 102,
          "pool reuses compatible handles across frames");
    check(second.historyValid[static_cast<size_t>(plan.resources[temporal].slot)],
          "history marked valid after successful frame");
    pool.invalidateHistory();
    auto afterCut = pool.prepare(graph, plan, create, release);
    check(afterCut.success &&
          !afterCut.historyValid[static_cast<size_t>(plan.resources[temporal].slot)],
          "camera cut invalidates history contents without reallocating");

    PrismaRenderGraph resized;
    auto newIn = resized.addResource(resource("scene", true, PrismaGraphFormat::RGBA16F, 128, 128));
    auto newTmp = resized.addResource(resource("transient", false, PrismaGraphFormat::RGBA16F, 128, 128));
    auto newHist = resource("taa-history", false, PrismaGraphFormat::RGBA16F, 128, 128);
    newHist.history = true;
    auto newHistory = resized.addResource(newHist);
    auto newOut = resized.addResource(resource("screen", true, PrismaGraphFormat::RGBA8, 128, 128));
    resized.addPass(pass("sample", {newIn}, {newTmp}));
    resized.addPass(pass("history", {newTmp}, {newHistory}));
    resized.addPass(pass("present", {newHistory}, {newOut}));
    const auto resizedPlan = resized.compile();
    auto resizedFrame = pool.prepare(resized, resizedPlan, create, release);
    check(resizedFrame.success && nextHandle == 104, "resize allocates new slots");
    check(!resizedFrame.historyValid[static_cast<size_t>(resizedPlan.resources[newHistory].slot)],
          "resized history must not import old pixels");
    pool.pruneUnused(release);
    check(released.size() == 2 && pool.entries().size() == 2,
          "pruning releases old-size resources only");

    int allocCalls = 0;
    PrismaRenderGraph failedGraph;
    auto failedIn = failedGraph.addResource(resource("input", true, PrismaGraphFormat::RGBA8, 512, 512));
    auto failedA = failedGraph.addResource(resource("temporary-a", false, PrismaGraphFormat::RGBA8, 512, 512));
    auto failedB = failedGraph.addResource(resource("temporary-b", false, PrismaGraphFormat::RGBA8, 512, 512));
    auto failedOut = failedGraph.addResource(resource("output", true, PrismaGraphFormat::RGBA8, 512, 512));
    failedGraph.addPass(pass("a", {failedIn}, {failedA}));
    failedGraph.addPass(pass("b", {failedA}, {failedB}));
    failedGraph.addPass(pass("c", {failedB}, {failedOut}));
    auto failPlan = failedGraph.compile();
    const size_t existing = pool.entries().size();
    const size_t priorReleased = released.size();
    auto failure = pool.prepare(failedGraph, failPlan,
        [&](PrismaGraphFormat, uint32_t, uint32_t) {
            return ++allocCalls == 2 ? 0 : nextHandle++;
        }, release);
    check(!failure.success && pool.entries().size() == existing,
          "allocation failure retains previous pool");
    check(released.size() == priorReleased + 1,
          "allocation failure releases partially allocated target");
    pool.clear(release);
    check(pool.entries().empty(), "pool clear releases owned resources");
}

void rhiFormatContract() {
    aiverse::gfx::PixelFormat fmt = aiverse::gfx::PixelFormat::RGBA8;
    check(prismaRhiFormat(PrismaGraphFormat::RGBA8, fmt) &&
          fmt == aiverse::gfx::PixelFormat::RGBA8, "RHI RGBA8 format mapped");
    check(prismaRhiFormat(PrismaGraphFormat::RGBA16F, fmt) &&
          fmt == aiverse::gfx::PixelFormat::RGBA16F, "RHI HDR format mapped");
    check(prismaRhiFormat(PrismaGraphFormat::Depth32F, fmt) &&
          fmt == aiverse::gfx::PixelFormat::Depth, "RHI depth format mapped");
    check(!prismaRhiFormat(PrismaGraphFormat::RG16F, fmt) &&
          !prismaRhiFormat(PrismaGraphFormat::R32F, fmt),
          "unavailable RHI formats rejected instead of emulated");
}

void fxaaGraphContract() {
    // Same two external RGBA8 targets as the Stage 14 runtime integration.
    PrismaRenderGraph g;
    auto src = g.addResource(resource("scene-fxaa", true, PrismaGraphFormat::RGBA8, 1920, 1080));
    auto dst = g.addResource(resource("fxaa-output", true, PrismaGraphFormat::RGBA8, 1920, 1080));
    auto p = pass("fxaa", {src}, {dst});
    g.addPass(p);
    const auto plan = g.compile();
    check(plan.valid && plan.slots.empty() && plan.order == std::vector<PrismaGraphId>({0}),
          "FXAA graph uses no extra pooled textures");
    if (!plan.valid) return;
    PrismaGraphBindings<int> bindings;
    bindings.external = {17, 19};
    int draws = 0;
    const auto result = dispatchPrismaGraph(g, plan, bindings,
        [&](const PrismaGraphPass& pass, PrismaGraphId,
            const PrismaGraphDispatchView<int>& view) {
            ++draws;
            return pass.kind == PrismaGraphPassKind::Raster &&
                   view.resource(src) == 17 && view.resource(dst) == 19;
        });
    check(result.success && result.executedPasses == 1 && draws == 1,
          "FXAA graph resolves scene/output handles and dispatches once");
}

void bloomPrefilterGraphContract() {
    // Stage 15: the prefilter is one raster pass, supports both SDR/HDR,
    // and samples the input at full size into a smaller target.
    for (auto fmt : {PrismaGraphFormat::RGBA8, PrismaGraphFormat::RGBA16F}) {
        PrismaRenderGraph graph;
        auto source = graph.addResource(resource("bloom-source", true, fmt, 1920, 1080));
        auto down = graph.addResource(resource("bloom-prefilter", true, fmt, 960, 540));
        graph.addPass(pass("bloom-prefilter", {source}, {down}));
        const auto plan = graph.compile();
        check(plan.valid && plan.slots.empty() &&
              plan.order == std::vector<PrismaGraphId>({0}),
              "bloom prefilter has one compiled pass and no graph allocations");
        if (!plan.valid) continue;
        PrismaGraphBindings<int> handles;
        handles.external = {31, 41};
        unsigned calls = 0;
        auto resolved = dispatchPrismaGraph(graph, plan, handles,
            [&](const PrismaGraphPass& p, PrismaGraphId id,
                const PrismaGraphDispatchView<int>& view) {
                ++calls;
                return id == 0 && p.kind == PrismaGraphPassKind::Raster &&
                       view.resource(source) == 31 && view.resource(down) == 41 &&
                       view.graph.resources()[source].width == 1920 &&
                       view.graph.resources()[down].width == 960;
            });
        check(resolved.success && resolved.executedPasses == 1 && calls == 1,
              "bloom prefilter binds source and destination without aliasing");
        handles.external[down] = 0;
        resolved = dispatchPrismaGraph(graph, plan, handles,
            [&](const PrismaGraphPass&, PrismaGraphId,
                const PrismaGraphDispatchView<int>&) {
                ++calls; return true;
            });
        check(!resolved.success && resolved.executedPasses == 0 && calls == 1,
              "missing bloom output prevents any GPU callback");
    }
}
}
int main() {
    positive();
    hazards();
    resources();
    dispatchContract();
    poolContract();
    rhiFormatContract();
    fxaaGraphContract();
    bloomPrefilterGraphContract();
    std::printf("PRISMA RenderGraph: %d failures\n",errors);
    return errors ? 1 : 0;
}
