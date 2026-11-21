// Stage 106 deferred: expired resource owner registrations never dereference
// stale cache pointers, including when the group outlives a scoped owner.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "../AlwaysCheck.h"
#include <cstdint>
#include <memory>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    RenderResourceOwnerGroup group;
    {
        auto transient=std::make_unique<ModelResourceCache>();
        transient->insertIfAbsent("model.glb",std::make_unique<Model>());
        group.add(*transient);
        group.add(*transient);
        AIVERSE_TEST_CHECK(group.size()==1);
        const auto live=group.snapshotOwners();
        AIVERSE_TEST_CHECK(live.uniqueOwners==1 && live.residentModels==1);
        AIVERSE_TEST_CHECK(live.expiredRegistrations==0);
    }
    // The weak marker expired during ModelResourceCache destruction.
    // The pointer address is stale but MUST NOT be dereferenced.
    AIVERSE_TEST_CHECK(group.size()==0);
    const auto expired=group.snapshotOwners();
    AIVERSE_TEST_CHECK(expired.uniqueOwners==0 && expired.residentModels==0);
    AIVERSE_TEST_CHECK(expired.expiredRegistrations==1);
    {
        ModelResourceCache replacement;
        replacement.insertIfAbsent("next.glb",std::make_unique<Model>());
        group.add(replacement);
        AIVERSE_TEST_CHECK(group.size()==1);
        AIVERSE_TEST_CHECK(group.snapshotOwners().residentModels==1);
        AIVERSE_TEST_CHECK(group.snapshotOwners().expiredRegistrations==1);
    }
    AIVERSE_TEST_CHECK(group.size()==0);
    AIVERSE_TEST_CHECK(group.snapshotOwners().expiredRegistrations==2);
    group.clear();
    AIVERSE_TEST_CHECK(group.snapshotOwners().expiredRegistrations==0);
}
