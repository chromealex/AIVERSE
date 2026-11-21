// VIVID real offscreen Vulkan ID/depth raster and async RGBA pixel readback.
// No window/swapchain needed; valid on native Vulkan and Mesa lavapipe.
// Exit 77 when no Vulkan loader/device is installed; CTest marks SKIP.
// This does NOT prove Renderer3D vertexOverride/LOD/ShaderGraph parity.
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/character/vivid/SculptGpuIdPass.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
std::vector<std::uint8_t> sample(gfx::Device& device,
                                 SculptGpuIdPass& pass,
                                 const render::Model& model,
                                 const SkinnedWeightSurface& surface,
                                 std::uint32_t x,std::uint32_t y,
                                 std::span<const SculptGpuIdVertex> occluders={}) {
    device.beginFrame(64,64);
    SculptGpuPickRequest request{};
    request.generation=1;request.modelRevision=1;
    request.frameSerial=device.frameSerial();
    request.meshId=0;request.width=request.height=64;
    request.x=x;request.y=y;
    require(request.valid(),"Vulkan picking frame request invalid");
    const auto projection=render::mat4::identity();
    require(pass.record(model,surface,model.vertices.size(),{},
                        projection,request,occluders),
            "Vulkan GPU ID pass could not record");
    device.endFrame();
    SculptGpuIdPass::Completed completed;
    for(int i=0;i<80;++i) {
        device.beginFrame(64,64);
        const bool done=pass.takeCompleted(completed);
        device.endFrame();
        if(done) {
            require(completed.request==request &&
                    completed.rgba.size()==4,
                    "Vulkan pixel callback missing/stale");
            return completed.rgba;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Vulkan async one-pixel transfer timed out");
}
}
int main() {
    std::string error;
    auto device=gfx::createVulkanDevice({},&error);
    if(!device) {
        std::cout<<"VIVID Vulkan ID SKIP: "<<error<<"\n";
        return 77;
    }
    try {
        render::Model model;
        model.vertices.resize(6);
        const std::array<render::float3,3> xy{{
            {-0.7f,-0.7f,0},{0.7f,-0.7f,0},{0,0.7f,0}
        }};
        for(std::size_t i=0;i<3;++i) {
            auto far=xy[i],near=xy[i];
            far.z=0.8f;near.z=0.2f;
            model.vertices[i].pos=far;
            model.vertices[i+3].pos=near;
        }
        model.indices={0,1,2,3,4,5};
        render::MeshDef mesh;
        render::Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=6;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        render::Node node;node.mesh=0;
        model.nodes.push_back(node);
        SkinnedWeightSurface surface;
        surface.mesh=0;
        surface.ready=true;
        surface.included.assign(6,1);
        for(const auto& v:model.vertices)surface.world.push_back(v.pos);
        SculptGpuIdPass pass;
        require(pass.initialize(*device),
                "Vulkan shader/pipeline ID source unsupported");
        const auto center=sample(*device,pass,model,surface,32,32);
        require(center==std::vector<std::uint8_t>({2,0,0,0}),
                "Vulkan ID/depth did not select nearer triangle");
        const auto outside=sample(*device,pass,model,surface,2,2);
        require(outside==std::vector<std::uint8_t>({0,0,0,0}),
                "Vulkan cleared pixel returned nonzero ID");
        const std::array<SculptGpuIdVertex,3> blocker{{
            {{-0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.0f, 0.7f,0.1f},{0,0,0,0}}
        }};
        const auto blocked=sample(*device,pass,model,surface,32,32,blocker);
        require(blocked==std::vector<std::uint8_t>({0,0,0,0}),
                "Vulkan scene zero-ID blocker failed depth test");
        require(pass.shutdownAfterIdle(),
                "Vulkan ID GPU resources failed idle retirement");
        std::cout<<"VIVID Vulkan ID/depth async readback pass=1\n";
        return 0;
    }catch(const std::exception& ex) {
        std::cerr<<"VIVID Vulkan GPU ID FAIL: "<<ex.what()<<"\n";
        return 1;
    }
}
