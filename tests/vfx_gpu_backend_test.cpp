// SpectraFX CPU / GPU backend selection and CPU fallback headless regression.
// GPU kernels remain gated by Metal/RHI and require a real-device test separately.
#include "engine/fx/SpectraFX.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;

static void check(bool ok,const char* why) {
    if(!ok) throw std::runtime_error(why);
}
int main() {
    using spectra::ComputePreference;
    using spectra::ComputeBackend;
    using spectra::chooseComputeBackend;
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::Automatic)==ComputeBackend::Metal,
          "available Metal compute was not selected");
    check(chooseComputeBackend(gfx::Backend::Metal,false,ComputePreference::PreferGpu)==ComputeBackend::Cpu,
          "Metal without compute did not fall back");
    check(chooseComputeBackend(gfx::Backend::GLES3,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GLES3 illegally selected Metal backend");
    check(chooseComputeBackend(gfx::Backend::Vulkan,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "Vulkan GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::GL33,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GL33 GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::CpuOnly)==ComputeBackend::Cpu,
          "CpuOnly policy ignored");

    spectra::ComputeEmitterDesc config;
    config.capacity=80;
    config.burst=5;
    config.lifetime=0.75f;
    config.speed=3;
    config.gravityY=-9.81f;
    config.backend=ComputePreference::Automatic;

    std::string reason;
    check(spectra::validComputeEmitter(config,&reason),"default config invalid");
    auto invalid=config;
    invalid.capacity=4097;
    check(!spectra::validComputeEmitter(invalid,&reason),"unsafe >4096 vertex/index capacity accepted");
    invalid=config; invalid.burst=0;
    check(!spectra::validComputeEmitter(invalid,&reason),"zero burst accepted");
    invalid=config; invalid.lifetime=-3;
    check(!spectra::validComputeEmitter(invalid,&reason),"negative lifetime accepted");
    invalid=config; invalid.speed=999999;
    check(!spectra::validComputeEmitter(invalid,&reason),"out-of-range speed accepted");

    spectra::ComputeParticles emitter;
    check(emitter.init(nullptr,config),"CPU fallback headless init failed");
    check(emitter.backend()==ComputeBackend::Cpu,"null GPU device should use CPU backend");
    check(emitter.burstAt({0,2,0},{1,0,0}),"CPU fallback failed to spawn particles");
    check(emitter.step(0.08f),"CPU fallback step failed");
    std::vector<fx::FxParticleInfo> particles;
    emitter.cpuFallback().snapshot(particles);
    check(particles.size()==5,"fallback lost expected burst size");
    check(particles.front().pos.x>0.001f,"fallback did not move particle");
    emitter.clear();
    emitter.cpuFallback().snapshot(particles);
    check(particles.empty(),"clear did not remove CPU fallback particles");
    check(emitter.burstAt({0,0,0}),"fallback spawn after clear failed");
    emitter.shutdown();
    check(!emitter.burstAt({0,0,0}),"spawn accepted after shutdown");

    // Explicit hard GPU requirement never silently reports a CPU implementation.
    config.backend=ComputePreference::RequireGpu;
    check(!emitter.init(nullptr,config),"RequireGpu should fail without a Device");
    check(!emitter.reason().empty(),"RequireGpu failure reason missing");
    std::cout<<"PASS SpectraFX compute policy, config validation, CPU fallback and lifecycle\n";
}
