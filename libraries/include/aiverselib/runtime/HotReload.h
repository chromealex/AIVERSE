#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
namespace aiverse::lib::runtime {
// Presentation/tool settings only. Gameplay config changes must be replicated Commands.
template<class T> class HotReload {
    T live_;
    std::optional<T> pending_;
    std::uint64_t revision_=0;
public:
    explicit HotReload(T initial):live_(std::move(initial)){}
    const T& current()const{return live_;}
    std::uint64_t revision()const{return revision_;}
    template<class Parse,class Validate,class Input>
    bool stage(const Input& input,Parse parse,Validate validate){
        pending_.reset();
        auto candidate=parse(input);
        if(!candidate||!validate(*candidate)){pending_.reset();return false;}
        pending_=std::move(*candidate);return true;
    }
    bool commit(bool safePoint,std::uint64_t expectedRevision){
        if(!safePoint||expectedRevision!=revision_||!pending_||revision_==std::numeric_limits<std::uint64_t>::max())return false;
        live_=std::move(*pending_);pending_.reset();++revision_;return true;
    }
    void cancel(){pending_.reset();}
};
}
