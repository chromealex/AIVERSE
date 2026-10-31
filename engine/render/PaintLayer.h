#pragma once
#include "RenderTexture.h"
#include <map>
#include <memory>
namespace rts::render {
// CPU command description; scripts own this, the renderer owns all GPU objects.
struct PaintLayer {
    int width=512,height=512;
    float x=0,z=0,w=1,h=1,lift=0.08f,opacity=0.45f,border=0.06f;
    std::array<float,4> color{1,1,1,1};
    int territoryOwner=0; // optional RTS source adapter; zero means manual brush canvas
    bool landOnly=true,clear=true,surface=true;
    int textureId=0,copyFrom=0;
    std::vector<RenderTexture::Stroke> strokes;
};
struct PaintLayers { std::map<int,PaintLayer> layers; int nextId=1; };
}
