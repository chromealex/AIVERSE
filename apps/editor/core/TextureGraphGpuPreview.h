// Editor-only asset publication adapter; the single GPU owner lives in engine/.
#pragma once
#include "engine/render/graph/TextureGraphGpuOwner.h"
#include "MaterialOps.h"

namespace aiverse::editor {
class TextureGraphGpuPreview final : public render::texture_graph::TextureGraphGpuOwner {
public:
    Json publish(Workspace& ws,const Json& p){
        return publishBaked(ws.project.dir,p.getStr("request"),
            [&](const Json& graph,const Json& overrides,const std::vector<render::Image>& images,
                const std::vector<std::string>& backends){
                return publishTextureGraphSet(ws,p.getStr("textureName"),graph,overrides,images,backends);
            },
            [&](const std::string& output,const render::Image& image){
                return publishTextureGraphImage(ws,p.getStr("textureName"),output,image);
            });
    }
};
} // namespace aiverse::editor
