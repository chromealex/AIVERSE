// Real Metal lifecycle test: asynchronous Studio import, last-good failure,
// generation replacement and a reader spanning actual renderer submission.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "apps/studio-native/mac/StudioFacialGpuPreview.h"
#include "tests/AlwaysCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace aiverse;
namespace fs=std::filesystem;

int aiverseMain(int,char**) {
    @autoreleasepool {
        const auto root=fs::temp_directory_path()/
            ("studio-facial-gpu-"+std::to_string(getpid()));
        fs::create_directories(root/"content");
        try {
            const std::string gltf=R"({"asset":{"version":"2.0"},
              "buffers":[{"uri":"face.bin","byteLength":72}],
              "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                             {"buffer":0,"byteOffset":36,"byteLength":36}],
              "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                           {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],
              "materials":[{"name":"Fabric"}],
              "meshes":[{"primitives":[{"material":0,"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}],
                         "extras":{"targetNames":["jawOpen"]}}],
              "nodes":[{"name":"Face","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
            {std::ofstream file(root/"content/face.gltf");file<<gltf;}
            const float data[18]={-1,-1,0,1,-1,0,0,1,0, 0,0,0,0,0,0,0,.2f,0};
            {std::ofstream file(root/"content/face.bin",std::ios::binary);
             file.write(reinterpret_cast<const char*>(data),sizeof(data));}
            studio::StudioAssetCatalog catalog(root);
            catalog.rebuild();
            const auto asset=catalog.byPath("face.gltf");
            AIVERSE_TEST_CHECK(asset.has_value());
            studio::StudioFacialPreviewBinding binding;
            const auto rig=Json::parse(R"({"version":1,"morphs":[
                {"parameter":"jawOpen","node":"Face","target":"jawOpen"}],"rig":[]})");
            binding.bind(catalog,asset->guid,rig);
            auto layer=[CAMetalLayer layer];
            layer.device=MTLCreateSystemDefaultDevice();
            layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
            layer.drawableSize=CGSizeMake(64,64);
            auto device=gfx::createMetalDevice((__bridge void*)layer);
            AIVERSE_TEST_CHECK(bool(device));
            render::RenderDeviceLifetime epoch(*device);
            render::Renderer3D renderer;
            renderer.setGpuDeviceLease(epoch.lease());
            AIVERSE_TEST_CHECK(renderer.init(*device));
            renderer.setShadowQuality(render::ShadowQuality::Off);
            studio::StudioFacialGpuPreview preview;
            preview.setDeviceLease(epoch.lease());
            std::string error;
            auto waitReady=[&] {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(std::chrono::steady_clock::now()<deadline) {
                    if(preview.sync(*device,renderer,root.string(),binding,error))return true;
                    if(error!="Loading facial model")return false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return false;
            };
            AIVERSE_TEST_CHECK(waitReady());
            const auto* first=preview.model();
            const auto firstRevision=preview.sourceRevision();
            {
                auto read=preview.acquireRead();
                AIVERSE_TEST_CHECK(bool(read));
                device->beginFrame(64,64);
                render::RTSCamera camera;
                camera.distance=4.f;
                renderer.beginScene(camera,64,64);
                AIVERSE_TEST_CHECK(preview.submit(renderer,binding,1.f/60.f,error));
                renderer.render(gfx::PassDesc{});
                device->endPass();device->endFrame();
            }
            // A bad reimport must neither publish nor destroy last-good.
            {std::ofstream file(root/"content/face.gltf");file<<"broken glTF";}
            binding.bind(catalog,asset->guid,rig);
            AIVERSE_TEST_CHECK(!waitReady());
            AIVERSE_TEST_CHECK(preview.model()==first && preview.sourceRevision()==firstRevision);
            {std::ofstream file(root/"content/face.gltf");file<<gltf;}
            binding.bind(catalog,asset->guid,rig);
            // Supersede an in-flight request for the SAME source. A stale
            // completion must not occupy the bounded ready queue forever.
            (void)preview.sync(*device,renderer,root.string(),binding,error);
            binding.bind(catalog,asset->guid,rig);
            AIVERSE_TEST_CHECK(waitReady());
            AIVERSE_TEST_CHECK(preview.model()!=first && preview.sourceRevision()!=firstRevision);
            binding.reset();
            vivid::HumanAppearance look;
            const auto materialName=preview.model()->materials.front().name;
            look.materials[materialName]="cloth";
            binding.bindAppearance(catalog,asset->guid,binding.revision(),look.toJson());
            AIVERSE_TEST_CHECK(waitReady());
            AIVERSE_TEST_CHECK(preview.ready() && binding.appearanceOnly());
            AIVERSE_TEST_CHECK(preview.model()->materials.front().roughness>.8f);
            {
                auto read=preview.acquireRead();
                device->beginFrame(64,64);render::RTSCamera camera;camera.distance=4.f;
                renderer.beginScene(camera,64,64);
                AIVERSE_TEST_CHECK(preview.submit(renderer,binding,1.f/60.f,error));
                renderer.render(gfx::PassDesc{});device->endPass();device->endFrame();
            }
            AIVERSE_TEST_CHECK(preview.clear(*device,renderer));
            AIVERSE_TEST_CHECK(!preview.ready());
            renderer.shutdown();epoch.retire();
            fs::remove_all(root);
            std::cout<<"Studio facial async import / last-good / Metal frame lease PASS\n";
        }catch(const std::exception& ex) {
            fs::remove_all(root);
            std::cerr<<ex.what()<<'\n';return 1;
        }
    }
    return 0;
}
