#include "aiverse_render.h"
#include "engine/render/HeadlessModelFrame.h"
#include <cstdio>
#include <cstring>
struct aiverse_render_model { std::unique_ptr<aiverse::render::Model> value; };
namespace {
thread_local char lastError[1024]{};
void setError(const char* error) noexcept { std::snprintf(lastError,sizeof lastError,"%s",error); }
}
uint32_t aiverse_render_abi_version(void) { return 1; }
const char* aiverse_render_last_error(void) { return lastError; }
aiverse_render_model* aiverse_render_open(const char* path) {
    try {
        if(!path || !*path) throw std::invalid_argument("a model path is required");
        auto handle=std::make_unique<aiverse_render_model>();
        handle->value=aiverse::render::loadHeadlessModel(path);
        return handle.release();
    }catch(const std::exception& e){setError(e.what());}catch(...){setError("unknown model loading error");}
    return nullptr;
}
void aiverse_render_close(aiverse_render_model* model) {delete model;}
int aiverse_render_rgba8(const aiverse_render_model* model,const char* backend,int width,int height,
                        float yaw,float pitch,uint8_t* pixels,size_t capacity) {
    try {
        if(!model || !backend || !pixels || width<1 || width>4096 || height<1 || height>4096 ||
           capacity<size_t(width)*height*4) throw std::invalid_argument("invalid model, dimensions or destination buffer");
        const auto frame=aiverse::render::renderHeadlessModelFrame(*model->value,backend,width,height,yaw,pitch);
        std::memcpy(pixels,frame.data(),frame.size());
        return 0;
    }catch(const std::exception& e){setError(e.what());}catch(...){setError("unknown frame rendering error");}
    return 1;
}
