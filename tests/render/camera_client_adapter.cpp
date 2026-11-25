// AIVERSE LENS: screen-ownership guard at application/client boundary.
// No GPU device required: validate project data before creating a runtime host.
#include "engine/render/CameraClientAdapter.h"
#include "engine/render/Renderer3D.h"
#include "tests/AlwaysCheck.h"
#include <stdexcept>
using namespace aiverse;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int aiverseMain(int, char**) {
    {
        using namespace render;
        // A capture callback is a live CPU replay consumer. Preflight must
        // refuse without changing the callback or touching GPU state.
        Renderer3D renderer;
        AIVERSE_TEST_CHECK(renderer.canDetachCompletedGraphImageReplay());
        renderer.setModelCapture([](const ModelDraw&,const std::vector<mat4>&){});
        AIVERSE_TEST_CHECK(!renderer.canDetachCompletedGraphImageReplay());
        AIVERSE_TEST_CHECK(!renderer.detachCompletedGraphImageReplay());
        AIVERSE_TEST_CHECK(!renderer.canDetachCompletedGraphImageReplay());
        renderer.setModelCapture({});
        renderer.setModelPacketTap([](const ModelDraw&){});
        AIVERSE_TEST_CHECK(!renderer.canDetachCompletedGraphImageReplay());
        renderer.setModelPacketTap({});
        renderer.setGeometryPacketTap([](const CameraGeometryCommand&){});
        AIVERSE_TEST_CHECK(!renderer.canDetachCompletedGraphImageReplay());
        renderer.setGeometryPacketTap({});
        AIVERSE_TEST_CHECK(renderer.canDetachCompletedGraphImageReplay());
    }
    World source;SceneHierarchy hierarchy;
    Entity e=source.create();
    check(hierarchy.attach(source,e,LocalTransform{}),"attach");
    source.add<CameraComponent>(e,CameraComponent{});
    source.add<CameraNameComponent>(e,CameraNameComponent{1,"Offscreen"});
    CameraOutputComponent output{};
    output.kind=CameraOutputKind::Texture;
    output.targetAssetKey=981;
    output.width=256;
    output.height=256;
    source.add<CameraOutputComponent>(e,output);
    std::string error;
    render::CameraClientAdapter adapter;
    check(adapter.loadOffscreenAsset(cameraSceneToJson(source).dump(),&error),"texture asset");
    check(adapter.loaded()&&adapter.host().entities().size()==1,"loaded");
    check(!adapter.loadOffscreenAsset(cameraSceneToJson(source).dump(),&error),"reject duplicate load");
    output.kind=CameraOutputKind::Screen;
    source.add<CameraOutputComponent>(e,output);
    render::CameraClientAdapter invalid;
    check(!invalid.loadOffscreenAsset(cameraSceneToJson(source).dump(),&error),"reject screen output");
    check(!invalid.loaded(),"failed transaction");
    return 0;
}
