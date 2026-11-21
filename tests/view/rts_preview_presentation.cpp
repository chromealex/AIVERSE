#include "modules/rts/view/RtsPreviewRenderProvider.h"
#include "../AlwaysCheck.h"
#include <cmath>

int main() {
    using namespace aiverse::game::view_adapter;
    const auto valid=makeGhostPresentation(17,{2,3,4},true,0.0);
    AIVERSE_TEST_CHECK(valid.type==17 && valid.owner==1);
    AIVERSE_TEST_CHECK(valid.position.x==2 && valid.position.y==3 && valid.position.z==4);
    AIVERSE_TEST_CHECK(valid.tint.x==.45f && valid.tint.y==1.f && valid.tint.z==.55f);
    AIVERSE_TEST_CHECK(std::fabs(valid.tint.w-.4f)<.00001f);
    const auto invalid=makeGhostPresentation(17,{2,3,4},false,0.0);
    AIVERSE_TEST_CHECK(invalid.tint.x==1.f && invalid.tint.y==.35f);
    AIVERSE_TEST_CHECK(std::fabs(invalid.tint.w-valid.tint.w)<.00001f);
    const auto pulsed=makeGhostPresentation(17,{2,3,4},true,
                                            3.141592653589793/10.);
    AIVERSE_TEST_CHECK(std::fabs(pulsed.tint.w-.48f)<.00001f);

    const auto preview=makeModelPreviewPresentation(23,5,0.f);
    AIVERSE_TEST_CHECK(preview.type==23 && preview.owner==5);
    AIVERSE_TEST_CHECK(preview.sourceKey==((uint64_t{1}<<62)|(uint64_t{5}<<16)|23u));
    AIVERSE_TEST_CHECK(preview.facing.x==1.f && preview.facing.y==0.f && preview.facing.z==0.f);
    const auto turned=makeModelPreviewPresentation(23,5,3.14159265f*.5f);
    AIVERSE_TEST_CHECK(std::fabs(turned.facing.x)<.00001f);
    AIVERSE_TEST_CHECK(std::fabs(turned.facing.z+1.f)<.00001f);

    AIVERSE_TEST_CHECK(previewFrameDt(5.0,-1.)==0.f);
    AIVERSE_TEST_CHECK(std::fabs(previewFrameDt(5.05,5.)-.05f)<.00001f);
    AIVERSE_TEST_CHECK(std::fabs(previewFrameDt(9.,5.)-.1f)<.00001f);
    AIVERSE_TEST_CHECK(previewFrameDt(4.,5.)==0.f);
    // A separate host can render the same authored preview without linking
    // against RtsView, its model loaders, or genre-specific renderer code.
    int readyCalls=0,ghostDraws=0,previewDraws=0;
    bool submitted=RtsPreviewRenderProvider::submitGhost(
        17,{2,3,4},false,0.,
        [&](uint16_t type){++readyCalls;return type==17;},
        [&](const GhostPresentation& request){
            ++ghostDraws;
            AIVERSE_TEST_CHECK(request.type==17 && request.owner==1);
            AIVERSE_TEST_CHECK(request.tint.x==1.f);
        });
    AIVERSE_TEST_CHECK(submitted && readyCalls==1 && ghostDraws==1);
    submitted=RtsPreviewRenderProvider::submitGhost(
        18,{2,3,4},true,0.,
        [&](uint16_t){++readyCalls;return false;},
        [&](const GhostPresentation&){++ghostDraws;});
    AIVERSE_TEST_CHECK(!submitted && readyCalls==2 && ghostDraws==1);
    int clockReads=0;
    submitted=RtsPreviewRenderProvider::submitModelPreview(
        23,5,6.0,0.5f,
        [&](uint16_t type){return type==23;},
        [&](const ModelPreviewPresentation& request){
            ++clockReads;
            AIVERSE_TEST_CHECK(request.sourceKey==preview.sourceKey);
            return 5.95;
        },
        [&](const ModelPreviewPresentation& request,float dt){
            ++previewDraws;
            AIVERSE_TEST_CHECK(request.owner==5);
            AIVERSE_TEST_CHECK(std::fabs(dt-.05f)<.00001f);
        });
    AIVERSE_TEST_CHECK(submitted && previewDraws==1 && clockReads==1);
    submitted=RtsPreviewRenderProvider::submitModelPreview(
        24,5,6.0,0.5f,
        [&](uint16_t){return false;},
        [&](const ModelPreviewPresentation&){++clockReads;return 0.;},
        [&](const ModelPreviewPresentation&,float){++previewDraws;});
    AIVERSE_TEST_CHECK(!submitted && previewDraws==1 && clockReads==1);

}
