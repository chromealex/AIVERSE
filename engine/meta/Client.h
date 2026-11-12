#pragma once
#include "Protocol.h"

namespace aiverse::meta {
// Host/UI request lifecycle. Persist the outbox BEFORE sending a mutation. The host
// owns authenticated transport and Backend lifetime; never call from simulation ticks.
class Client {
  public:
    using Persist = std::function<bool(const Json &outbox)>;
    explicit Client(Backend &backend, Persist persist = {}, const Json &outbox = Json::object());
    ~Client() = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    bool begin(ServiceRequest request);
    bool retry(); // same operation and payload after ambiguous failure
    void pump();  // dispatch on owning host thread
    bool pending() const;
    bool inFlight() const;
    ServiceReply reply() const;
    Json save() const; // persist with the matching player/account, never share
  private:
    struct State;
    Backend &backend_;
    std::shared_ptr<State> state_;
    void send();
};
namespace requests {
ServiceRequest view();
ServiceRequest buy(std::string offer, std::string operation);
ServiceRequest claim(std::string track, int64_t tier, std::string operation);
ServiceRequest chatSend(std::string channel, std::string text, std::string operation);
ServiceRequest boardSubmit(std::string board, int64_t score, std::string operation);
} // namespace requests
} // namespace aiverse::meta
