// Pure Fixed regeneration is shared by the RTS Unit ECS system and the
// generic ECS energy system. Test legacy arithmetic identity including
// non-integer fractions, negative rates and clamped caps.
#include "engine/mechanics/FixedRegeneration.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

static void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
int main() {
    constexpr Fixed dt=Fixed::fromRatio(1,20);
    for(int current=-3;current<=11;++current)
        for(int cap=1;cap<=10;++cap)
            for(int rate=-50;rate<=50;rate+=5) {
                const Fixed original=Fixed::fromRatio(current*10+3,10);
                const Fixed maximum=Fixed::fromInt(cap);
                const Fixed speed=Fixed::fromRatio(rate*10+1,10);
                const Fixed expected=fxMin(maximum,original+speed*dt);
                const Fixed actual=lg::stepFixedResourceRegeneration(
                    original,maximum,speed,dt);
                check(actual.raw==expected.raw,"fixed regen legacy parity changed");
            }
    Fixed energy=Fixed::fromRatio(1,3);
    const Fixed limit=7_fx;
    const Fixed rate=Fixed::fromRatio(13,2);
    for(int tick=0;tick<200;++tick)
        energy=lg::stepFixedResourceRegeneration(energy,limit,rate,dt);
    check(energy==limit,"fractional regen never reached cap");
    const Fixed draining=lg::stepFixedResourceRegeneration(
        Fixed::fromInt(3),Fixed::fromInt(10),-4_fx,dt);
    check(draining<3_fx&&draining>Fixed{},"negative energy rate was discarded");
    std::cout<<"PASS: shared Fixed ECS regen policy matches legacy RTS arithmetic\n";
}
