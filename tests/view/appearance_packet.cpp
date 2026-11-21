// Stage 107 deferred: typed generic appearance packet preserves all
// material/variant/tint/geometry/graph inputs without any game module.
#include "engine/view/AppearancePacket.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse;
    using namespace aiverse::view;
    render::Model model;
    std::vector<render::Material> materials(2);
    std::vector<int> nodeMaterial{1,0};
    AppearanceSpec spec;
    spec.model=&model;
    spec.materials=&materials;
    spec.nodeMaterial=&nodeMaterial;
    spec.variant=7;
    spec.tint={0.5f,0.6f,0.7f,0.8f};
    spec.team={0.1f,0.2f,0.3f};
    spec.flags=render::kDrawTransparent;
    spec.sourceId=42;spec.generation=3;
    Object packet;
    AIVERSE_TEST_CHECK(buildAppearancePacket(spec,render::mat4::identity(),packet));
    AIVERSE_TEST_CHECK(packet.id==42 && packet.generation==3);
    AIVERSE_TEST_CHECK(packet.draw.model==&model);
    AIVERSE_TEST_CHECK(packet.draw.materials==&materials);
    AIVERSE_TEST_CHECK(packet.draw.nodeMaterial==&nodeMaterial);
    AIVERSE_TEST_CHECK(packet.draw.variant==7);
    AIVERSE_TEST_CHECK(packet.draw.tint.w==0.8f);
    AIVERSE_TEST_CHECK(packet.draw.team.x==0.1f);
    AIVERSE_TEST_CHECK(packet.draw.flags&render::kDrawTransparent);
    AIVERSE_TEST_CHECK(!packet.graph);
    AIVERSE_TEST_CHECK(packet.visible);
    spec.model=nullptr;
    AIVERSE_TEST_CHECK(!buildAppearancePacket(spec,render::mat4::identity(),packet));
}
