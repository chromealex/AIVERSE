// Portable GPU buffer -> ordinary RGBA8 texture copy layout. No GPU calls.
#pragma once
#include <cstddef>
#include <limits>

namespace aiverse::gfx {
inline constexpr std::size_t rgba8CopyAlignment=256;
struct BufferTextureCopy2D {
    int width=0,height=0;
    std::size_t sourceOffset=0,rowPitchBytes=0;
};
struct Rgba8CopyLayout {
    std::size_t rowPitchBytes=0,bytes=0;
    constexpr explicit operator bool()const noexcept{return bytes!=0;}
};
// A common conservative row alignment works for Metal and Vulkan. Limit the
// extent to the portable 2D resource range; the device still checks support.
inline constexpr Rgba8CopyLayout rgba8CopyLayout(int width,int height)noexcept{
    if(width<=0||height<=0||width>16384||height>16384)return {};
    const auto row=std::size_t(width)*4;
    const auto pitch=(row+rgba8CopyAlignment-1)&~(rgba8CopyAlignment-1);
    if(std::size_t(height)>std::numeric_limits<std::size_t>::max()/pitch)return {};
    return {pitch,pitch*std::size_t(height)};
}
// Require the entire padded source allocation, including the last row's
// padding. Overflow-safe even for an untrusted offset or source size.
inline constexpr bool validBufferTextureCopy2D(const BufferTextureCopy2D& c,
        std::size_t sourceBytes,int dstWidth,int dstHeight)noexcept{
    const auto layout=rgba8CopyLayout(c.width,c.height);
    return bool(layout)&&c.width==dstWidth&&c.height==dstHeight&&
        c.rowPitchBytes==layout.rowPitchBytes&&c.sourceOffset%rgba8CopyAlignment==0&&
        c.sourceOffset<=sourceBytes&&layout.bytes<=sourceBytes-c.sourceOffset;
}
} // namespace aiverse::gfx
