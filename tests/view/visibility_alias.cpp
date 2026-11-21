#include "engine/view/VisibilityField.h"
#include "modules/rts/view/RtsFog.h"
#include <type_traits>
#include <span>
#include <utility>
#include "../AlwaysCheck.h"
int main() {
    static_assert(std::is_same_v<aiverse::view::VisibilityField,
                  aiverse::game::FogOfWar>);
    static_assert(std::is_same_v<aiverse::view::VisibilityLook,
                  aiverse::game::FogLook>);
    using Field=aiverse::view::VisibilityField;
    static_assert(std::is_same_v<
        decltype(std::declval<Field&>().update(
            std::declval<std::span<const uint8_t>>(),1,1,0.0)),bool>);
    aiverse::view::VisibilityLook look;
    AIVERSE_TEST_CHECK(look.show);
}
