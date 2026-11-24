// Opaque, genre-free named animation event edges. No gameplay authority.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace aiverse::lib::animation {
struct AnimationActionFrame {
    uint64_t revision=0,generation=0;
    std::map<std::string,uint64_t> counters;
    std::map<std::string,bool> flags;
};
struct AnimationActionChanges {
    bool generationChanged=false;
    std::vector<std::string> increased,activated,deactivated;
    bool increasedCounter(std::string_view key) const {
        return std::find(increased.begin(),increased.end(),key)!=increased.end();
    }
    bool becameActive(std::string_view key) const {
        return std::find(activated.begin(),activated.end(),key)!=activated.end();
    }
    bool becameInactive(std::string_view key) const {
        return std::find(deactivated.begin(),deactivated.end(),key)!=deactivated.end();
    }
};
class AnimationActionEdges {
public:
    AnimationActionChanges observe(const AnimationActionFrame& source) {
        AnimationActionChanges events;
        if(!ready_ || source.generation!=last_.generation) {
            events.generationChanged=ready_;
            last_=source;ready_=true;
            return events;
        }
        if(source.revision<last_.revision)return events;
        for(const auto& [key,value]:source.counters) {
            const auto prior=last_.counters.find(key);
            if(prior!=last_.counters.end() && value>prior->second)
                events.increased.push_back(key);
        }
        for(const auto& [key,value]:source.flags) {
            const auto prior=last_.flags.find(key);
            const bool was=prior!=last_.flags.end() && prior->second;
            if(!was && value)events.activated.push_back(key);
            if(was && !value)events.deactivated.push_back(key);
        }
        last_=source;return events;
    }
    void reset(){ready_=false;last_={};}
    bool initialized() const {return ready_;}
private:
    AnimationActionFrame last_;
    bool ready_=false;
};
} // namespace aiverse::lib::animation
