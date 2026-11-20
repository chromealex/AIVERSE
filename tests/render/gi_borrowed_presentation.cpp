#include "engine/render/GiBorrowedPresentation.h"
#include "tests/AlwaysCheck.h"

using namespace aiverse::render;

int main() {
    GiBorrowedPresentation presentation;
    int rendererA=0,rendererB=0;
    AIVERSE_TEST_CHECK(presentation.canRetire() && presentation.readers()==0);
    AIVERSE_TEST_CHECK(!presentation.acquire(nullptr));
    AIVERSE_TEST_CHECK(presentation.canRetire());
    AIVERSE_TEST_CHECK(presentation.acquire(&rendererA));
    AIVERSE_TEST_CHECK(!presentation.canRetire() && presentation.readers()==1);
    // Rebinding the SAME Renderer3D from frame to frame without an intervening
    // unbind does not accidentally allow early GPU atlas destruction.
    AIVERSE_TEST_CHECK(presentation.acquire(&rendererA));
    AIVERSE_TEST_CHECK(presentation.readers()==1);
    AIVERSE_TEST_CHECK(presentation.acquire(&rendererB));
    AIVERSE_TEST_CHECK(presentation.readers()==2);
    presentation.release(&rendererA);
    AIVERSE_TEST_CHECK(!presentation.canRetire() && presentation.readers()==1);
    presentation.release(&rendererB);
    AIVERSE_TEST_CHECK(presentation.canRetire() && presentation.readers()==0);
    presentation.release(&rendererB); // idempotent missed/duplicate unbind
    AIVERSE_TEST_CHECK(presentation.canRetire());
}
