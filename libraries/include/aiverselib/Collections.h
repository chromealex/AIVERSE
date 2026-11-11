#pragma once
#include <algorithm>
#include <cstddef>
#include <deque>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <type_traits>
#include <vector>

namespace aiverse::lib {
// Transient storage; persist gameplay queues through the host's state container.
template<class T> class Queue {
public:
    void push(T value) { items_.push_back(std::move(value)); }
    std::optional<T> pop() {
        if (items_.empty()) return std::nullopt;
        T value=std::move(items_.front()); items_.pop_front(); return value;
    }
    bool empty() const { return items_.empty(); }
private:
    std::deque<T> items_;
};
// Return indices; ties preserve input order. Score must be pure and deterministic.
template<class T, class Score>
std::vector<std::size_t> topIndices(std::span<const T> items, std::size_t count, Score score) {
    using Value=std::remove_cvref_t<decltype(score(std::declval<const T&>()))>;
    static_assert(!std::is_floating_point_v<Value>,"simulation scores must be integer or Fixed");
    std::vector<std::size_t> indices(items.size());
    std::iota(indices.begin(),indices.end(),0);
    std::stable_sort(indices.begin(),indices.end(),[&](auto a,auto b){ return score(items[a])>score(items[b]); });
    indices.resize(std::min(count,indices.size())); return indices;
}
}
