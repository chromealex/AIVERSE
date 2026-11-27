// LENS prepared sprite and VFX draw-only API surface.
// No device, resource allocation, animation ticking or rendering required.
#include "engine/view/game/Game2DView.h"
#include "engine/view/game/GameView.h"
#include "apps/client/ClientFx.h"
#include "engine/render/CameraClientAdapter.h"
#include <type_traits>

using namespace aiverse;
static_assert(std::is_same_v<
    decltype(&game::Rts2DView::submitPrepared),
    void(game::Rts2DView::*)(render::Renderer3D&)>);
static_assert(std::is_same_v<
    decltype(&game::GameView::drawPreparedSprites),
    void(game::GameView::*)(render::Renderer3D&,uint64_t)>);
static_assert(std::is_same_v<
    decltype(&client::ClientFx::renderPrepared),
    void(client::ClientFx::*)(const render::Camera3D&,const render::mat4&,
                             int,int,const render::Lighting&,render::Renderer3D*)>);
// The existing LDR call remains valid through the default renderer argument.
static_assert(std::is_same_v<decltype(std::declval<client::ClientFx&>().renderPrepared(
    std::declval<const render::Camera3D&>(), std::declval<const render::mat4&>(),
    1, 1, std::declval<const render::Lighting&>())), void>);
int main(){return 0;}
