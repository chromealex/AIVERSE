#include "Protocol.h"

namespace aiverse::meta {
void OfflineBackend::request(ServiceRequest r, Callback cb) {
    pending_.emplace_back(std::move(r), std::move(cb));
}
void OfflineBackend::pump() {
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto &[r, cb] : pending)
        if (cb)
            cb({{Code::Unsupported, "online service unavailable: " + r.service}, Json::object()});
}
void RemoteBackend::request(ServiceRequest r, Callback cb) {
    if (!send_) {
        pending_->push_back({std::move(cb), {{Code::Unsupported, "transport"}, Json::object()}});
        return;
    }
    send_(r, [queue = pending_, cb = std::move(cb)](ServiceReply reply) mutable {
        queue->push_back({std::move(cb), std::move(reply)});
    });
}
void RemoteBackend::pump() {
    auto ready = std::move(*pending_);
    pending_->clear();
    for (auto &p : ready)
        if (p.callback)
            p.callback(std::move(p.reply));
}
} // namespace aiverse::meta
