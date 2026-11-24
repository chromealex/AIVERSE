// Editor-only asset publication adapter; the single GPU owner lives in engine/.
#pragma once
#include "engine/render/graph/TextureGraphGpuOwner.h"
#include "MaterialOps.h"

namespace aiverse::editor {
class TextureGraphGpuPreview final : public render::texture_graph::TextureGraphGpuOwner {
public:
    template<class Publish>
    Json consumeRadiance(const std::string& scope,const std::string& request,Publish&& publish){
        return publishRadianceBaked(scope,request,std::forward<Publish>(publish));
    }
    Json publish(Workspace& ws,const Json& p){
        if(isRadianceBake())return publishRadianceBaked(ws.project.dir,p.getStr("request"),[&](int size,std::span<const float> rgba){return p.getBool("environment")?publishTextureGraphEnvironment(ws,p.getStr("textureName"),size,rgba):publishTextureGraphRadiance(ws,p.getStr("textureName"),size,rgba);});
        return publishBaked(ws.project.dir,p.getStr("request"),
            [&](const Json& graph,const Json& overrides,const std::vector<render::Image>& images,
                const std::vector<std::string>& backends){
                return publishTextureGraphSet(ws,p.getStr("textureName"),graph,overrides,images,backends);
            },
            [&](const std::string& output,const render::Image& image){
                const auto format=p.getStr("format","png");
                return format=="png"?publishTextureGraphImage(ws,p.getStr("textureName"),output,image):
                    publishTextureGraphCompressed(ws,p.getStr("textureName"),output,image,format,p.getBool("mipmaps",true));
            });
    }
};
} // namespace aiverse::editor
