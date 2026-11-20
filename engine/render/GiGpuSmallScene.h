// Bounded Metal / Vulkan compute GI for small, immutable triangle scenes.
// Independent opt-in backend; NEVER auto-enables in GiDynamicRuntime.
// Caller must invoke dispatch inside beginFrame/endFrame and OUTSIDE render passes.
// GPU BVH traversal writes the RGBA16F dynamic-only atlas. No GPU readback or CPU coefficients.
// In a real app, completion/failure must be paired with a GPU frame fence before
// claiming scheduler tickets complete; this prototype does NOT acknowledge them.
#pragma once
#include "GiGpuSmallSceneShaders.h"
#include "GiGpuVulkanShaders.h"
#include "GiGpuBvh.h"
#include "GiGpuBvhValidator.h"
#include "GiGpuSparsePatch.h"
#include "GiDynamicProbeTexture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace aiverse::render {

class GiGpuSmallScene {
public:
    static constexpr size_t kMaxTriangles = GiGpuBvh::kMaxTriangles;
    static constexpr size_t kMaxLights = 16;
    static constexpr size_t kMaxJobs = 32;
    static constexpr int kMaxSamples = 64;

    struct DispatchResult {
        bool submitted = false;
        uint32_t probes = 0, samples = 0, triangles = 0, nodes = 0, lights = 0;
        uint64_t primaryRayBudget = 0, maximumTriangleTests = 0;
    };
    struct GeometryUploadStats {
        bool full=false;
        size_t bytes=0, regions=0;
    };
    GeometryUploadStats lastGeometryUpload() const { return geometryUpload_; }
    using GpuTri = GiGpuBvhTri;
    using GpuNode = GiGpuBvhNode;
    // Spot parameters are explicit so MSL and std430 SPIR-V see identical 64 B.
    struct GpuLamp { float4 posRadius{}, colorIntensity{}, spotDirectionOuter{}, spotInner{}; };
    struct GpuJob { uint32_t index = 0; float x = 0, y = 0, z = 0; };
    struct Uniforms {
        uint32_t triangles = 0, lights = 0, jobs = 0, samples = 0;
        float traceDistance = 0;
        uint32_t nodes = 0;
        float pad1 = 0, pad2 = 0;
        float4 sunDirectionIntensity{}; // xyz surface->sun, w intensity
        float4 sunColorSkyIntensity{};  // rgb sun radiance, w sky intensity
        float4 skyColor{};              // rgb sky radiance, w unused
    };
    static_assert(sizeof(GpuTri) == 64 && sizeof(GpuNode) == 48 && sizeof(GpuLamp) == 64 &&
                  sizeof(GpuJob) == 16 && sizeof(Uniforms) == 80);

    ~GiGpuSmallScene() { shutdown(); }
    GiGpuSmallScene() = default;
    GiGpuSmallScene(const GiGpuSmallScene&) = delete;
    GiGpuSmallScene& operator=(const GiGpuSmallScene&) = delete;

    bool initialize(gfx::Device& device, size_t probeCount) {
        shutdown();
        // GLES3/WebGL2 have no compute guarantee; they use GiDynamicRuntime's
        // bounded CPU tracer and standard RGBA32F texture update path.
        const auto backend=device.info().backend;
        if ((backend!=gfx::Backend::Metal && backend!=gfx::Backend::Vulkan) ||
            !device.supportsCompute() || !device.supports(gfx::PixelFormat::RGBA16F))
            return false;
        if (!atlas_.initialize(device,probeCount,true)) return false;
        const auto compute = device.createCompute(
            {gi_gpu::kMetalSmallScene,gi_gpu::kVulkanSmallScene,
             "GI bounded triangle transport"});
        if (!compute.valid()) { atlas_.release(); return false; }
        dev_ = &device;
        compute_ = compute;
        // One dummy element is allocated to satisfy Metal argument-buffer validation
        // even for a scene with no lights/triangles.
        triBuffer_ = allocate(sizeof(GpuTri),nullptr);
        nodeBuffer_ = allocate(sizeof(GpuNode),nullptr);
        lampBuffer_ = allocate(sizeof(GpuLamp),nullptr);
        jobBuffer_ = allocate(kMaxJobs*sizeof(GpuJob),nullptr);
        if (!triBuffer_.valid() || !nodeBuffer_.valid() ||
            !lampBuffer_.valid() || !jobBuffer_.valid()) {
            shutdown();
            return false;
        }
        triCapacity_=nodeCapacity_=1; // one dummy element for empty scene
        return true;
    }

    void shutdown() {
        if (dev_) {
            if (triBuffer_.valid()) dev_->destroyBuffer(triBuffer_);
            if (nodeBuffer_.valid()) dev_->destroyBuffer(nodeBuffer_);
            if (lampBuffer_.valid()) dev_->destroyBuffer(lampBuffer_);
            if (jobBuffer_.valid()) dev_->destroyBuffer(jobBuffer_);
            if (compute_.valid()) dev_->destroyCompute(compute_);
        }
        triBuffer_={};
        nodeBuffer_={};
        lampBuffer_={};
        jobBuffer_={};
        compute_={};
        atlas_.release();
        dev_=nullptr;
        triangleCount_=0;
        nodeCount_=0;
        lightCount_=0;
        geometryRevision_=0;
        environment_={};
        lastTriangles_.clear();
        lastNodes_.clear();
        geometryUpload_={};
        triCapacity_=nodeCapacity_=0;
    }

    bool ready() const { return dev_ && compute_.valid() && atlas_.texture().valid(); }
    const GiDynamicProbeTexture& atlas() const { return atlas_; }
    uint64_t geometryRevision() const { return geometryRevision_; }
    size_t triangleCount() const { return triangleCount_; }
    size_t nodeCount() const { return nodeCount_; }
    size_t lightCount() const { return lightCount_; }
    const GiEnvironmentLight& environment() const { return environment_; }
    // CPU state only: the next dispatch snapshots the parameters as uniforms.
    // An in-flight GPU job keeps its old encoded values; the owning scheduler
    // invalidates its tickets independently on a changed environment.
    bool setEnvironment(GiEnvironmentLight value) {
        if (!ready() || !value.valid()) return false;
        environment_=value;
        return true;
    }

    // Publish only when a new scene geometry revision becomes visible.
    // This prototype CPU-builds a packed BVH for each bounded scene revision.
    // Large worlds still need incremental GPU chunk uploads, not full flattening.
    bool setGeometry(const GiChunkedGeometrySnapshot& snapshot, uint64_t revision) {
        if (!ready()) return false;
        if (revision==0 || revision<=geometryRevision_) return false;
        if (snapshot.triangleCount()>kMaxTriangles || !dev_->canUpdateSharedBuffers())
            return false;
        return setPackedGeometry(GiGpuBvh::build(snapshot), revision);
    }

    // Cache callers may reuse unchanged per-chunk BLAS and provide a packed
    // TLAS+BLAS packet. Must only run after the previous GPU fence completed.
    bool setPackedGeometry(const GiGpuBvh& bvh, uint64_t revision) {
        if (!ready() || !dev_->canUpdateSharedBuffers() ||
            revision==0 || revision<=geometryRevision_ ||
            bvh.triangles.size()>kMaxTriangles || bvh.nodes.size()>2*kMaxTriangles+4096)
            return false;
        // The CPU paged allocator may contain unreachable holes and a TLAS
        // reserved prefix. Validate only root-reachable BVH references, but
        // do so BEFORE any sparse GPU writes, allocations or state changes.
        // An invalid packet must never be published to MSL/SPIR-V compute.
        if (!giValidateGpuBvh(bvh).valid) return false;
        GeometryUploadStats stats;
        // Keep storage-buffer handles resident across changing chunk counts
        // whenever the existing capacity suffices. Allocate new buffers
        // only on exponential growth, NOT on shrink/append of a few triangles.
        // All updates must be issued after the prior GPU ticket fence is ACKed.
        const size_t triCount=bvh.triangles.size();
        const size_t nodeCount=bvh.nodes.size();
        const size_t nextTriCapacity=giGpuGrowCapacity(
            triCapacity_,triCount,kMaxTriangles);
        const size_t nextNodeCapacity=giGpuGrowCapacity(
            nodeCapacity_,nodeCount,2*kMaxTriangles+4096);
        const bool growTri=nextTriCapacity>triCapacity_;
        const bool growNode=nextNodeCapacity>nodeCapacity_;
        gfx::BufferHandle newTri{},newNode{};
        if(growTri) {
            newTri=allocate(nextTriCapacity*sizeof(GpuTri),nullptr);
            if(!newTri.valid())return false;
        }
        if(growNode) {
            newNode=allocate(nextNodeCapacity*sizeof(GpuNode),nullptr);
            if(!newNode.valid()) {
                if(newTri.valid())dev_->destroyBuffer(newTri);
                return false;
            }
        }
        const auto triPlan=giGpuPlanSparsePatch(
            lastTriangles_,bvh.triangles,growTri);
        const auto nodePlan=giGpuPlanSparsePatch(
            lastNodes_,bvh.nodes,growNode);
        const auto upload=[&](const auto& data,const GiGpuPatchPlan& plan,
                              gfx::BufferHandle target) {
            using Element=typename std::decay_t<decltype(data)>::value_type;
            for(const auto& range:plan.ranges) {
                dev_->updateBuffer(target,data.data()+range.first,
                                   range.count*sizeof(Element),
                                   range.first*sizeof(Element));
                stats.bytes+=range.count*sizeof(Element);
                ++stats.regions;
            }
        };
        upload(bvh.triangles,triPlan,growTri?newTri:triBuffer_);
        upload(bvh.nodes,nodePlan,growNode?newNode:nodeBuffer_);
        if(!dev_->gpuHealthy()) {
            if(newTri.valid())dev_->destroyBuffer(newTri);
            if(newNode.valid())dev_->destroyBuffer(newNode);
            return false;
        }
        // Only after all allocations and uploads are encoded, switch the
        // submission's buffer handles. Defer actual backend resource free
        // through the RHI; pending submissions were fenced by the caller.
        if(growTri) {
            dev_->destroyBuffer(triBuffer_);
            triBuffer_=newTri;
            triCapacity_=nextTriCapacity;
        }
        if(growNode) {
            dev_->destroyBuffer(nodeBuffer_);
            nodeBuffer_=newNode;
            nodeCapacity_=nextNodeCapacity;
        }
        stats.full=growTri || growNode || triPlan.full || nodePlan.full;
        lastTriangles_=bvh.triangles;
        lastNodes_=bvh.nodes;
        triangleCount_=bvh.triangles.size();
        nodeCount_=bvh.nodes.size();
        geometryRevision_=revision;
        geometryUpload_=stats;
        return true;
    }

    bool setLights(std::vector<GiMovingLight> lights) {
        if (!ready() || lights.size()>kMaxLights || !dev_->canUpdateSharedBuffers())
            return false;
        std::sort(lights.begin(),lights.end(),
                  [](const auto& a,const auto& b){return a.key<b.key;});
        std::vector<GpuLamp> packed;
        packed.reserve(lights.size());
        uint64_t last=0;
        bool haveLast=false;
        for (const auto& l:lights) {
            if ((haveLast && l.key==last) || !finite(l.position) || !finite(l.color) ||
                l.color.x<0 || l.color.y<0 || l.color.z<0 ||
                !std::isfinite(l.radius) || l.radius<=0 ||
                !std::isfinite(l.intensity) || l.intensity<0 ||
                !std::isfinite(l.cosOuter) || !std::isfinite(l.cosInner) ||
                (l.spot() && (!finite(l.direction) ||
                    std::abs(dot(l.direction,l.direction)-1.0f)>.002f ||
                    l.cosOuter < -1.0f || l.cosOuter>=1.0f ||
                    l.cosInner < l.cosOuter || l.cosInner>1.0f)))
                return false;
            packed.push_back({{l.position.x,l.position.y,l.position.z,l.radius},
                              {l.color.x,l.color.y,l.color.z,l.intensity},
                              {l.direction.x,l.direction.y,l.direction.z,l.cosOuter},
                              {l.cosInner,0,0,0}});
            haveLast=true;
            last=l.key;
        }
        const auto next=allocate(std::max<size_t>(1,packed.size())*sizeof(GpuLamp),
                                 packed.empty()?nullptr:packed.data());
        if (!next.valid()) return false;
        dev_->destroyBuffer(lampBuffer_);
        lampBuffer_=next;
        lightCount_=packed.size();
        return true;
    }

    DispatchResult dispatch(const std::vector<GiProbeTicket>& jobs,
                            uint32_t samples, float traceDistance) {
        DispatchResult result;
        if (!ready() || jobs.empty() || jobs.size()>kMaxJobs ||
            samples<8 || samples>kMaxSamples || !std::isfinite(traceDistance) ||
            traceDistance<=0 || traceDistance>1000 || !dev_->canUpdateSharedBuffers())
            return result;

        std::vector<GpuJob> packed;
        packed.reserve(jobs.size());
        std::set<uint32_t> unique;
        for (const auto& t:jobs) {
            if (t.index>=atlas_.probeCount() || !finite(t.position) ||
                !unique.insert(t.index).second)
                return result;
            packed.push_back({t.index,t.position.x,t.position.y,t.position.z});
        }
        dev_->updateBuffer(jobBuffer_,packed.data(),packed.size()*sizeof(GpuJob));
        // With zero geometry the kernel overwrites requested probes with zero RGB
        // (the cache is initialized to zero; old light must not survive).
        if (!dev_->beginCompute(compute_)) return result;
        dev_->setComputeBuffer(0,triBuffer_);
        dev_->setComputeBuffer(1,lampBuffer_);
        dev_->setComputeBuffer(2,jobBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,atlas_.texture());
        Uniforms params;
        params.triangles=uint32_t(triangleCount_);
        params.nodes=uint32_t(nodeCount_);
        params.lights=uint32_t(lightCount_);
        params.jobs=uint32_t(packed.size());
        params.samples=samples;
        params.traceDistance=traceDistance;
        params.sunDirectionIntensity={environment_.sunDirection.x,
                                     environment_.sunDirection.y,
                                     environment_.sunDirection.z,
                                     environment_.sunIntensity};
        params.sunColorSkyIntensity={environment_.sunColor.x,
                                     environment_.sunColor.y,
                                     environment_.sunColor.z,
                                     environment_.skyIntensity};
        params.skyColor={environment_.skyColor.x,environment_.skyColor.y,
                         environment_.skyColor.z,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(params.jobs);
        dev_->endCompute();
        if (!dev_->computeDispatchSucceeded()) return result;
        result.submitted=true;
        result.probes=params.jobs;
        result.samples=params.samples;
        result.triangles=params.triangles;
        result.nodes=params.nodes;
        result.lights=params.lights;
        result.primaryRayBudget=uint64_t(params.jobs)*params.samples;
        result.maximumTriangleTests=result.primaryRayBudget*
            uint64_t(params.triangles)*(uint64_t(params.lights)+1u);
        return result;
    }

private:
    static bool finite(float3 x) {
        return std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
    }
    gfx::BufferHandle allocate(size_t size,const void* data) const {
        gfx::BufferDesc desc;
        desc.type=gfx::BufferType::Storage;
        desc.usage=gfx::Usage::Immutable;
        desc.size=size;
        desc.data=data;
        desc.label="GI bounded compute input";
        return dev_->createBuffer(desc);
    }

    gfx::Device* dev_=nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle triBuffer_{},nodeBuffer_{},lampBuffer_{},jobBuffer_{};
    GiDynamicProbeTexture atlas_;
    size_t triangleCount_=0,nodeCount_=0,lightCount_=0;
    std::vector<GpuTri> lastTriangles_;
    std::vector<GpuNode> lastNodes_;
    GeometryUploadStats geometryUpload_{};
    size_t triCapacity_=0,nodeCapacity_=0;
    uint64_t geometryRevision_=0;
    GiEnvironmentLight environment_{};
};

} // namespace aiverse::render
