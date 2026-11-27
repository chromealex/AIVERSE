#pragma once
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace aiverse::render {
enum class ImpostorFit { Auto, Full, Trim };

inline ImpostorFit parseImpostorFit(std::string_view name) {
    if (name == "auto") return ImpostorFit::Auto;
    if (name == "full") return ImpostorFit::Full;
    if (name == "trim") return ImpostorFit::Trim;
    throw std::invalid_argument("impostor fit must be auto, full or trim");
}

// Coordinates describe pixel edges. The right and bottom edges are exclusive.
struct ImpostorRect {
    int left, top, right, bottom;
    int area() const { return (right-left)*(bottom-top); }
};

struct ImpostorMaskBounds {
    int left = std::numeric_limits<int>::max(), top = left, right = 0, bottom = 0;
    void include(int x, int y) {
        left = std::min(left,x);
        top = std::min(top,y);
        right = std::max(right,x+1);
        bottom = std::max(bottom,y+1);
    }
    ImpostorRect select(int width, int height, ImpostorFit fit) const {
        const ImpostorRect full{0,0,width,height};
        if (!right || fit == ImpostorFit::Full) return full;
        constexpr int border = 2;
        const ImpostorRect padded{std::max(0,left-border),std::max(0,top-border),
                                  std::min(width,right+border),std::min(height,bottom+border)};
        // Auto requires at least 20% fewer covered texels for the same quad.
        return fit == ImpostorFit::Auto && 5LL*padded.area()>4LL*full.area() ? full : padded;
    }
};
}
