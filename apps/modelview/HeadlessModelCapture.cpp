#include "HeadlessModelCapture.h"
#include "engine/render/HeadlessModelFrame.h"
#include "engine/render/CameraPng.h"
#include <cstdio>

namespace aiverse::modelview {
int captureHeadlessModel(const std::string& backend,const std::string& path,const std::string& output) {
    if(!render::headlessModelBackendAvailable(backend)) {
        std::fprintf(stderr,"Unavailable capture backend: %s\n",backend.c_str());
        return 2;
    }
    try {
        const auto model=render::loadHeadlessModel(path);
        const auto pixels=render::renderHeadlessModelFrame(*model,backend,512,512,.55f,.3f);
        if(!render::cameraWritePngRgba8(output,512,512,pixels)) throw std::runtime_error("could not write model capture");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
}
