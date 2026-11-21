// Stage 105 deferred compile-time contract: generic authored ModelDraw
// portrait consumer never requires the calling native project to own
// RtsView or create an additional animation evaluation World.
#include "modules/rts/view/RtsPortrait.h"
#include <type_traits>
#include <utility>

int main() {
    using namespace aiverse;
    using Fn=bool (game::Portrait::*)(
        const render::ModelDraw&,const render::RTSCamera&,
        render::float4);
    static_assert(std::is_same_v<
        decltype(&game::Portrait::renderAppearance),Fn>);
    static_assert(std::is_same_v<
        decltype(std::declval<const game::Portrait&>().modelResources()),
        const render::ModelResourceCache&>);
    static_assert(std::is_same_v<
        decltype(std::declval<game::Portrait&>().renderStaticModel(
            std::declval<render::ModelAssetProvider&>(),
            std::declval<const std::string&>(),0.5f)),bool>);
}
