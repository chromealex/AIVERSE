// Premultiplied ARGB pixels for the Windows layered window above the Vulkan swapchain.
#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::studio {
struct DeviceMaskRect {
    double x, y, width, height, radius;

    // Distance from the left and right edges to the rounded boundary at this scanline.
    double inset(double row) const {
        const double r=std::min({radius,width/2,height/2});
        const double dy=std::max({y+r-row,row-(y+height-r),0.0});
        return r-std::sqrt(std::max(0.0,r*r-dy*dy));
    }
};

inline void drawDeviceMask(uint32_t* pixels, int width, int height, double dpiScale, const Json& device) {
    const auto& chrome=device["chrome"];
    const double logicalWidth=device["width"].toDouble(), logicalHeight=device["height"].toDouble();
    if (width<=0 || height<=0) return;
    if (logicalWidth<=0 || logicalHeight<=0) {
        std::fill_n(pixels,size_t(width)*height,0u);
        return;
    }
    const double sx=width/logicalWidth, sy=height/logicalHeight;
    const DeviceMaskRect screen{0,0,double(width),double(height),chrome["corner"].toDouble()*sx};
    const auto& cut=chrome["cutout"];
    const DeviceMaskRect island{cut["x"].toDouble()*sx,cut["y"].toDouble()*sy,
        cut["width"].toDouble()*sx,cut["height"].toDouble()*sy,cut["radius"].toDouble()*sx};
    const auto& in=chrome["insets"];
    const double left=in["left"].toDouble()*sx, right=width-in["right"].toDouble()*sx;
    const double top=in["top"].toDouble()*sy, bottom=height-in["bottom"].toDouble()*sy;
    const bool safe=device.getBool("safeArea") && (left>0 || right<width || top>0 || bottom<height);
    const double stroke=1.5*dpiScale;
    const auto column=[width](double x){return std::clamp(int(std::ceil(x-.5)),0,width);};
    // Premultiplied values for 27% red shade and 90% gold guide.
    constexpr uint32_t shade=0x46461310, guide=0xe6e6bd48, bezel=0xff0c0c0e;
    for(int y=0;y<height;++y) {
        uint32_t* row=pixels+size_t(y)*width;
        const auto span=[&](double x0,double x1,uint32_t color){
            if(x1>x0) std::fill(row+column(x0),row+column(x1),color);
        };
        const double cy=y+.5;
        std::fill_n(row,width,safe?shade:0u);
        if(safe && cy>=top && cy<bottom) {
            span(left,right,guide);
            if(cy>=top+stroke && cy<bottom-stroke) span(left+stroke,right-stroke,0);
        }
        const double inset=screen.inset(cy);
        span(0,inset,bezel);span(width-inset,width,bezel);
        if(cy>=island.y && cy<island.y+island.height) {
            const double dx=island.inset(cy);
            span(island.x+dx,island.x+island.width-dx,0xff000000);
        }
    }
}
} // namespace aiverse::studio
