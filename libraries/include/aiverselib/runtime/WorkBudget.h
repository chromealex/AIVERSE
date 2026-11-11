#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace aiverse::lib::runtime {
struct WorkItem {std::uint64_t id=0,total=0,completed=0;};
struct WorkCursor {std::size_t next=0;};
struct WorkResult {std::size_t operations=0;bool valid=true,blocked=false;};
// One callback = one bounded operation. No wall-clock budget; persist items/cursor for rollback.
// Caller supplies sorted unique IDs; callback must not mutate this span/cursor or its backing store.
template<class Step>
WorkResult runBudget(std::span<WorkItem> items,WorkCursor& cursor,std::size_t budget,Step step){
    for(std::size_t i=0;i<items.size();++i)if(items[i].completed>items[i].total||(i&&items[i-1].id>=items[i].id))return {0,false,false};
    if(items.empty())return {};
    if(cursor.next>=items.size())return {0,false,false};
    WorkResult out;
    std::size_t idle=0;
    while(out.operations<budget&&idle<items.size()){
        auto& item=items[cursor.next];
        if(item.completed<item.total){
            if(!step(item.id,item.completed)){out.blocked=true;return out;}
            ++item.completed;++out.operations;idle=0;
        }else ++idle;
        cursor.next=(cursor.next+1)%items.size();
    }
    return out;
}
}
