#pragma once
#include "Types.h"
#include "engine/data/Json.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::meta {
// Transport adapter contract: callbacks are dispatched on the owning thread by pump().
// Backends implement authentication, authorization, server policies and durable storage.
// JSON operation names are application protocol, not trusted executable code.
struct ServiceRequest {
    std::string service, action, operation;
    Json arguments = Json::object();
};
struct ServiceReply {
    Result result;
    Json data = Json::object();
    // Local transport signal, never accepted from an untrusted response body.
    // A server Code::Unavailable (e.g. closed event) is a definitive policy reply.
    bool transportUncertain = false;
};
inline ServiceReply transportFailure(const std::string &detail) {
    return {{Code::Unavailable, detail}, Json::object(), true};
}
class Backend {
  public:
    using Callback = std::function<void(ServiceReply)>;
    virtual ~Backend() = default;
    virtual void request(ServiceRequest request, Callback callback) = 0;
    virtual void pump() = 0;
};
class OfflineBackend final : public Backend {
  public:
    void request(ServiceRequest request, Callback callback) override;
    void pump() override;

  private:
    std::vector<std::pair<ServiceRequest, Callback>> pending_;
};
// Native networking adapter. Transport completion is queued by the host on the owning thread.
// Host implements HTTPS/auth and retries with the same operation ID.
class RemoteBackend final : public Backend {
  public:
    using Transport = std::function<void(const ServiceRequest &, Callback)>;
    explicit RemoteBackend(Transport send) : send_(std::move(send)) {}
    void request(ServiceRequest, Callback) override;
    void pump() override;

  private:
    Transport send_;
    struct Pending {
        Callback callback;
        ServiceReply reply;
    };
    std::shared_ptr<std::vector<Pending>> pending_ = std::make_shared<std::vector<Pending>>();
};
} // namespace aiverse::meta
