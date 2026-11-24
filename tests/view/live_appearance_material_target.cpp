#include "engine/view/LiveAppearanceMaterialTarget.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::view;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    const LiveAppearanceMaterialTarget t{9007199254740993ull,17,2};
    check(t.matches(9007199254740993ull,17,3));
    check(!t.matches(9007199254740992ull,17,3));
    check(!t.matches(t.publication,18,3));
    check(!t.matches(t.publication,17,2));
    check(t.nextVersion()==18);
    check(!LiveAppearanceMaterialTarget{}.matches(0,0,3));
    const LiveAppearanceMaterialTarget max{1,UINT64_MAX,0};
    check(!max.matches(1,UINT64_MAX,1)&&max.nextVersion()==0);
    check(parseAppearanceIdentity("9007199254740993")==t.publication);
    check(parseAppearanceIdentity("18446744073709551615")==UINT64_MAX);
    for(auto bad:{"","0","01","-1","+1"," 1","1 ","1.0","1x","18446744073709551616"})
        check(parseAppearanceIdentity(bad)==0);
    std::cout<<"PASS: exact publication identity, stale revision, material range, overflow and decimal transport\n";
}
