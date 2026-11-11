#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace aiverse::lib::app {
using Value = std::variant<bool, std::int64_t, std::string>;
struct Change { std::string key; Value value; };
// Owns a presentation snapshot, never references mutable simulation memory.
class BindingModel {
    std::map<std::string, Value> values_;
    std::map<std::string, Value> dirty_;
public:
    bool set(const std::string& key, Value value) {
        if (key.empty()) return false;
        auto it = values_.find(key);
        if (it != values_.end() && it->second == value) return false;
        values_.insert_or_assign(key, value);
        dirty_.insert_or_assign(key, std::move(value));
        return true;
    }
    const Value* get(const std::string& key) const {
        auto it = values_.find(key); return it == values_.end() ? nullptr : &it->second;
    }
    std::vector<Change> drain() {
        std::vector<Change> out;
        for (const auto& [key, value] : dirty_) out.push_back({key, value});
        dirty_.clear(); return out;
    }
    void invalidate() { dirty_ = values_; }
};
inline std::string displayValue(const Value& v) {
    if (auto p = std::get_if<std::string>(&v)) return *p;
    if (auto p = std::get_if<bool>(&v)) return *p ? "true" : "false";
    return std::to_string(std::get<std::int64_t>(v));
}
inline std::string escapeMarkup(const std::string& text) {
    std::string out;
    for (char c : text) switch (c) {
        case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break; case '\"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break; default: out += c;
    }
    return out;
}
struct Action { std::string id; std::string payload; };
// Bounded queue avoids unbounded input growth while the host is paused.
class Actions {
    std::size_t capacity_;
    std::vector<Action> pending_;
public:
    explicit Actions(std::size_t capacity = 128) : capacity_(capacity) {}
    bool push(Action action) {
        if (action.id.empty() || pending_.size() >= capacity_) return false;
        pending_.push_back(std::move(action)); return true;
    }
    std::vector<Action> drain() { std::vector<Action> out; out.swap(pending_); return out; }
};
}
