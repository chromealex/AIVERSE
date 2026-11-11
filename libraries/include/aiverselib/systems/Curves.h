#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
namespace aiverse::lib::systems {
struct CurvePoint {std::int64_t x=0,y=0;};
enum class CurveMode {Step,Linear};
// Nonnegative integer curves; outside the domain clamps to endpoints. Linear rounding is toward the left value.
// Reject unsafe intermediate products instead of platform-dependent overflow; scale domain units accordingly.
inline std::optional<std::int64_t> curve(std::span<const CurvePoint> points,std::int64_t x,CurveMode mode=CurveMode::Linear){
    if(points.empty()||x<0)return {};
    for(std::size_t i=0;i<points.size();++i)if(points[i].x<0||points[i].y<0||(i&&points[i].x<=points[i-1].x))return {};
    if(x<=points.front().x)return points.front().y;
    for(std::size_t i=1;i<points.size();++i)if(x<points[i].x){
        auto a=points[i-1],b=points[i];if(mode==CurveMode::Step)return a.y;
        auto distance=b.x-a.x,offset=x-a.x,delta=b.y>=a.y?b.y-a.y:a.y-b.y;
        if(delta&&offset>std::numeric_limits<std::int64_t>::max()/delta)return {};
        auto change=delta*offset/distance;return b.y>=a.y?a.y+change:a.y-change;
    }
    return points.back().y;
}
}
