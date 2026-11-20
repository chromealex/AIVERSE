#include "engine/render/GiClipmapVolume.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    GiClipmapVolume clip;
    clip.configure({0,0,0},1,4,2,2,{.1f,0,0});
    AIVERSE_TEST_CHECK(clip.size()==16 && clip.pending()==16);
    AIVERSE_TEST_CHECK((clip.originCell()==GiClipmapCell{-2,-1,-1}));
    AIVERSE_TEST_CHECK((clip.ringOffset()==std::array<int,3>{2,1,1}));

    // Physical slots are a permutation of logical world cells, including negatives.
    for(uint32_t id=0;id<16;++id) {
        const auto cell=clip.cell(id);
        AIVERSE_TEST_CHECK(clip.contains(cell));
        AIVERSE_TEST_CHECK(clip.physicalIndex(cell)==id);
        AIVERSE_TEST_CHECK(!clip.valid(id));
    }
    size_t finished=0;
    while(clip.pending()) {
        auto tickets=clip.claim(3,{0,0,0});
        AIVERSE_TEST_CHECK(!tickets.empty() && tickets.size()<=3);
        for(const auto& t:tickets) {
            AIVERSE_TEST_CHECK(clip.complete(t));
            AIVERSE_TEST_CHECK(clip.valid(t.index));
            ++finished;
        }
    }
    AIVERSE_TEST_CHECK(finished==16 && clip.inFlight()==0);
    const auto oldOrigin=clip.originCell();
    AIVERSE_TEST_CHECK(oldOrigin.x==-2);

    // One-cell camera scroll reuses the intersecting 3/4 of the resident volume.
    std::vector<uint32_t> changed;
    auto scroll=clip.scroll({1.2f,0,0},&changed);
    AIVERSE_TEST_CHECK(scroll.changed && !scroll.blocked);
    AIVERSE_TEST_CHECK(scroll.recycled==4 && scroll.reused==12 && changed.size()==4);
    AIVERSE_TEST_CHECK(clip.pending()==4);
    AIVERSE_TEST_CHECK(clip.originCell().x==-1);
    AIVERSE_TEST_CHECK((clip.ringOffset()==std::array<int,3>{3,1,1}));
    for(const uint32_t id:changed) {
        AIVERSE_TEST_CHECK(!clip.valid(id));
        AIVERSE_TEST_CHECK(clip.cell(id).x==2);
    }
    for(uint32_t id=0;id<16;++id) {
        AIVERSE_TEST_CHECK(clip.physicalIndex(clip.cell(id))==id);
    }
    AIVERSE_TEST_CHECK(!clip.scroll({1.4f,0,0}).changed);

    // Do not recycle storage while an old GPU batch might still write to it.
    const auto inflight=clip.claim(2,{2,0,0});
    AIVERSE_TEST_CHECK(inflight.size()==2 && clip.inFlight()==2);
    scroll=clip.scroll({5.1f,0,0});
    AIVERSE_TEST_CHECK(!scroll.changed && scroll.blocked);
    AIVERSE_TEST_CHECK(clip.originCell().x==-1);
    for(const auto& t:inflight)AIVERSE_TEST_CHECK(clip.abort(t));
    AIVERSE_TEST_CHECK(clip.inFlight()==0);
    changed.clear();
    scroll=clip.scroll({5.1f,0,0},&changed);
    AIVERSE_TEST_CHECK(scroll.changed && scroll.recycled==16 && scroll.reused==0);
    AIVERSE_TEST_CHECK(clip.pending()==16);

    // An invalidated in-flight ticket must not publish a stale probe.
    const auto ticket=clip.claim(1,{5,0,0});
    AIVERSE_TEST_CHECK(ticket.size()==1);
    const auto p=ticket[0].position;
    const GiBounds point{p,p};
    AIVERSE_TEST_CHECK(clip.invalidate(point,GiDirtyReason::Lighting)==1);
    AIVERSE_TEST_CHECK(!clip.complete(ticket[0]));
    AIVERSE_TEST_CHECK(!clip.valid(ticket[0].index));
    AIVERSE_TEST_CHECK(clip.inFlight()==0);

    // Negative coordinates and nonzero anchor preserve stable physical-index mapping.
    clip.configure({.25f,.25f,.25f},2,3,3,2,{-21,-7,-9});
    AIVERSE_TEST_CHECK(clip.size()==18);
    for(uint32_t id=0;id<clip.size();++id)
        AIVERSE_TEST_CHECK(clip.physicalIndex(clip.cell(id))==id);
    auto old=clip.originCell();
    scroll=clip.scroll({-19,-7,-9});
    AIVERSE_TEST_CHECK(scroll.changed && scroll.recycled==6);
    AIVERSE_TEST_CHECK(clip.originCell().x==old.x+1);

    bool rejected=false;
    try {clip.configure({},0,4,4,4,{});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try {clip.scroll({INFINITY,0,0});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try {(void)clip.physicalIndex({999,999,999});}
    catch(const std::out_of_range&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
