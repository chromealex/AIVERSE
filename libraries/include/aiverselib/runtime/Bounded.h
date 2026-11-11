#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
namespace aiverse::lib::runtime {
template<class T,std::size_t N> class ScratchBuffer {
    std::array<T,N> items_{};
    std::size_t size_=0;
public:
    bool push(T value){if(size_==N)return false;items_[size_++]=std::move(value);return true;}
    void clear(){for(std::size_t i=0;i<size_;++i)items_[i]=T{};size_=0;}
    std::span<const T> values()const{return {items_.data(),size_};}
    std::size_t size()const{return size_;}
};
struct PoolHandle {std::size_t index=0;std::uint64_t generation=0;};
// Fixed slots, lowest free index wins. A wrapped slot is retired instead of aliasing stale handles.
template<class T,std::size_t N> class SlotPool {
    std::array<std::optional<T>,N> slots_{};
    std::array<std::uint64_t,N> generations_{};
public:
    std::optional<PoolHandle> acquire(T value){
        for(std::size_t i=0;i<N;++i)if(!slots_[i]&&generations_[i]!=std::numeric_limits<std::uint64_t>::max()){
            slots_[i]=std::move(value);return PoolHandle{i,++generations_[i]};
        }
        return std::nullopt;
    }
    T* get(PoolHandle h){return h.index<N&&h.generation!=0&&generations_[h.index]==h.generation&&slots_[h.index]?&*slots_[h.index]:nullptr;}
    bool release(PoolHandle h){if(!get(h))return false;slots_[h.index].reset();return true;}
};
}
