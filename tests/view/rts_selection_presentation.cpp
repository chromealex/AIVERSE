// Pure host-adapter regression: RTS selection must not depend on RtsView
// residency, renderer initialization or an editor window.
#include "modules/rts/view/RtsSelectionPresentation.h"
#include "../AlwaysCheck.h"
#include <cmath>
#include <stdexcept>

int main() {
    using namespace aiverse::game;
    UnitType unit;
    unit.structure=false;
    unit.radius=aiverse::Fixed::fromInt(2);
    AIVERSE_TEST_CHECK(std::fabs(view_adapter::selectionRadius(unit)-2.15f)<.0001f);

    UnitType building;
    building.structure=true;
    building.footprintW=4;
    building.footprintH=7;
    AIVERSE_TEST_CHECK(std::fabs(view_adapter::selectionRadius(building)-4.34f)<.0001f);
    building.footprintH=3;
    AIVERSE_TEST_CHECK(std::fabs(view_adapter::selectionRadius(building)-2.48f)<.0001f);

    Rules rules;
    rules.types.push_back(unit);
    rules.types.push_back(building);
    AIVERSE_TEST_CHECK(view_adapter::selectionRadius(rules,0)==view_adapter::selectionRadius(unit));
    AIVERSE_TEST_CHECK(view_adapter::selectionRadius(rules,1)==view_adapter::selectionRadius(building));
    bool rejected=false;
    try { (void)view_adapter::selectionRadius(rules,2); }
    catch(const std::out_of_range&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
}
