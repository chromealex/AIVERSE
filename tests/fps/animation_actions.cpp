#include "modules/fps/view/FpsAnimationEvents.h"
#include <cassert>
using namespace aiverse::fps::view;
int main() {
    ReplicatedAnimationActions actions;
    auto t=actions.observe({50,1,8,0,100});
    assert(!t.fire&&!t.reload&&!t.death);
    t=actions.observe({50,1,8,0,100}); // repeated packet
    assert(!t.fire&&!t.reload&&!t.death);
    t=actions.observe({51,1,9,0,100});
    assert(t.fire&&!t.reload&&!t.death);
    t=actions.observe({52,1,9,62,100});
    assert(t.reload&&!t.fire&&!t.death);
    t=actions.observe({53,1,9,62,100});
    assert(!t.reload);
    t=actions.observe({54,1,9,0,0});
    assert(t.death&&!t.fire);
    t=actions.observe({55,2,0,0,100});
    assert(t.respawn&&!t.fire&&!t.death);
    t=actions.observe({45,2,12,0,100});
    assert(!t.fire); // stale snapshot ignored
    t=actions.observe({56,2,1,0,100});
    assert(t.fire);
    actions.reset();
    t=actions.observe({57,2,8,0,100});
    assert(!t.fire && !t.respawn);
}
