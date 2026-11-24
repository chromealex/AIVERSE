#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
enum class InputPhase { Press,Release,Repeat,Axis };
struct InputEvent { std::int64_t code=0,modifiers=0,context=0,value=0; InputPhase phase=InputPhase::Press; };
struct Binding { std::int64_t id=0,action=0,code=0,modifiers=0,context=0,priority=0; InputPhase phase=InputPhase::Press; bool enabled=true; };
inline std::optional<std::int64_t> resolveInput(std::span<const Binding> bindings,const InputEvent& e) {
    const Binding* best=nullptr;
    for (const auto& b:bindings) if (b.enabled && b.code==e.code && b.modifiers==e.modifiers && b.context==e.context && b.phase==e.phase)
        if (!best || b.priority>best->priority || (b.priority==best->priority && b.id<best->id)) best=&b;
    return best ? std::optional(best->action) : std::nullopt;
}
inline std::int64_t deadzone(std::int64_t value,std::int64_t threshold) {
    value=std::clamp<std::int64_t>(value,-1000,1000); threshold=std::clamp<std::int64_t>(threshold,0,999);
    const auto magnitude=value<0?-value:value;
    return magnitude<=threshold ? 0 : (value<0?-1:1)*(magnitude-threshold)*1000/(1000-threshold);
}
enum class SelectionMode { Replace,Add,Toggle };
inline void updateSelection(std::vector<std::int64_t>& selected,std::span<const std::int64_t> incoming,SelectionMode mode) {
    if (mode==SelectionMode::Replace) selected.clear();
    std::sort(selected.begin(),selected.end()); selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
    std::vector<std::int64_t> ids(incoming.begin(),incoming.end());
    std::sort(ids.begin(),ids.end()); ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    for(auto id:ids) {
        auto at=std::lower_bound(selected.begin(),selected.end(),id);
        if(at!=selected.end() && *at==id) { if(mode==SelectionMode::Toggle) selected.erase(at); }
        else selected.insert(at,id);
    }
}
class ControlGroups {
public:
    explicit ControlGroups(std::size_t count=10):groups_(std::min<std::size_t>(count,100)){}
    bool assign(std::size_t group,std::span<const std::int64_t> ids){if(group>=groups_.size())return false;updateSelection(groups_[group],ids,SelectionMode::Replace);return true;}
    template<class Alive> std::vector<std::int64_t> recall(std::size_t group,Alive alive)const {
        std::vector<std::int64_t> result;if(group<groups_.size())for(auto id:groups_[group])if(alive(id))result.push_back(id);return result;
    }
private:std::vector<std::vector<std::int64_t>> groups_;
};
struct DragGesture {
    std::int32_t startX=0,startY=0;bool active=false;
    void begin(std::int32_t x,std::int32_t y){startX=x;startY=y;active=true;}
    void cancel(){active=false;}
    bool isDrag(std::int32_t x,std::int32_t y,std::int64_t threshold)const {
        if(!active||threshold<0)return false;
        auto dx=std::int64_t(x)-startX,dy=std::int64_t(y)-startY;
        return dx>threshold||dx< -threshold||dy>threshold||dy< -threshold;
    }
};
}
