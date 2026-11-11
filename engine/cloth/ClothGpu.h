#pragma once
#include "Cloth.h"
#include "ClothColoring.h"
#include "engine/gfx/Gfx.h"
#include <array>
namespace aiverse::cloth {
// Attach to a Batch before stepping. Calls must be inside a GPU frame, outside render passes.
// Cloth positions are GPU-owned until download(); CPU accessors explicitly report unavailable.
// Grid, object order and count remain fixed. reset()/movePins()/wind/active and TemporalLod work normally.
// Destroy before the Batch, cloth array and Device. shutdown() resets GPU-owned CPU proxies to rest.
class GpuSolver {
public:
    GpuSolver()=default;
    ~GpuSolver(){shutdown();}
    GpuSolver(const GpuSolver&)=delete;
    GpuSolver& operator=(const GpuSolver&)=delete;
    bool init(gfx::Device&,Batch&,std::span<Cloth>);
    void shutdown();
    bool download(); // synchronous diagnostics only; between frames
    gfx::TextureHandle positionsTexture()const{return texture_;}
    bool healthy()const{return healthy_&&device_&&device_->gpuHealthy();}
    const std::string& error()const{return error_;}
    size_t uploadBytes()const{return uploadBytes_;}
    size_t memoryBytes()const{return memoryBytes_;}
    unsigned dispatches()const{return dispatches_;}
private:
    bool execute(std::span<Cloth>,std::span<const size_t>,std::span<const size_t>,std::span<const float>,const Environment&,std::span<const Environment>);
    struct Input {render::float4 forceH,pinFloor,solver;std::array<uint32_t,4> meta;};
    struct CapsuleData {render::float4 a,b;};
    gfx::Device* device_=nullptr;
    Batch* batch_=nullptr;
    std::span<Cloth> cloths_;
    gfx::ComputeHandle shader_;
    gfx::TextureHandle texture_;
    std::array<gfx::BufferHandle,10> buffers_{}; // slot 8 reserved for uniform bytes
    std::vector<size_t> offsets_;
    std::vector<Input> inputs_;
    std::vector<CapsuleData> capsules_;
    std::vector<uint32_t> jobs_,sources_;
    std::vector<bool> wasExternal_;
    std::vector<uint64_t> uploaded_;
    std::vector<render::float4> scratch_;
    uint32_t n_=0,columns_=0,edgeCount_=0;
    size_t memoryBytes_=0,uploadBytes_=0;
    unsigned dispatches_=0;
    bool healthy_=false;
    std::string error_;
};
}
