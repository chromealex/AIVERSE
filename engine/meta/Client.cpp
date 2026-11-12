#include "Client.h"
#include <stdexcept>

namespace aiverse::meta {
namespace {
Json encode(const ServiceRequest &r) {
    auto j = Json::object();
    j.set("service", Json::string(r.service));
    j.set("action", Json::string(r.action));
    j.set("operation", Json::string(r.operation));
    j.set("arguments", r.arguments);
    return j;
}
bool valid(const ServiceRequest &r) {
    bool read = r.service == "meta" && r.action == "view";
    return !r.service.empty() && r.service.size() <= 128 && !r.action.empty() && r.action.size() <= 128 &&
           (read || (!r.operation.empty() && r.operation.size() <= 128)) && r.arguments.isObject() &&
           encode(r).dump(-1).size() <= 65536;
}
} // namespace
struct Client::State {
    uint64_t attempt = 0;
    Persist persist;
    ServiceRequest request;
    ServiceReply reply;
    bool pending = false, inFlight = false;
};
Client::Client(Backend &backend, Persist persist, const Json &outbox)
    : backend_(backend), state_(std::make_shared<State>()) {
    state_->persist = std::move(persist);
    if (!outbox.isObject())
        throw std::runtime_error("meta client: invalid outbox");
    if (outbox.size()) {
        if (outbox.size() != 4 || !outbox["service"].isString() || !outbox["action"].isString() ||
            !outbox["operation"].isString())
            throw std::runtime_error("meta client: invalid outbox fields");
        ServiceRequest r{outbox.getStr("service"), outbox.getStr("action"), outbox.getStr("operation"),
                         outbox["arguments"]};
        if (!valid(r))
            throw std::runtime_error("meta client: invalid restored request");
        state_->request = std::move(r);
        state_->pending = true;
    }
}
bool Client::begin(ServiceRequest request) {
    if (state_->pending || !valid(request))
        return false;
    if (state_->persist && !state_->persist(encode(request)))
        return false;
    state_->request = std::move(request);
    state_->pending = true;
    send();
    return true;
}
void Client::send() {
    state_->inFlight = true;
    const auto attempt = ++state_->attempt;
    std::weak_ptr<State> weak = state_;
    backend_.request(state_->request, [weak, attempt](ServiceReply reply) {
        auto state = weak.lock();
        if (!state || state->attempt != attempt || !state->inFlight)
            return;
        state->inFlight = false;
        // Only transport uncertainty keeps the request pending; policy Unavailable is terminal.
        if (!reply.transportUncertain) {
            if (state->persist && !state->persist(Json::object())) {
                state->reply = transportFailure("outbox acknowledgement not persisted");
                return;
            }
            state->pending = false;
        }
        state->reply = std::move(reply);
    });
}
bool Client::retry() {
    if (!state_->pending || state_->inFlight)
        return false;
    send();
    return true;
}
void Client::pump() {
    backend_.pump();
}
bool Client::pending() const {
    return state_->pending;
}
bool Client::inFlight() const {
    return state_->inFlight;
}
ServiceReply Client::reply() const {
    return state_->reply;
}
Json Client::save() const {
    return state_->pending ? encode(state_->request) : Json::object();
}
namespace requests {
ServiceRequest view() {
    return {"meta", "view", {}, Json::object()};
}
ServiceRequest buy(std::string offer, std::string operation) {
    auto a = Json::object();
    a.set("id", Json::string(offer));
    return {"economy", "buy", std::move(operation), a};
}
ServiceRequest claim(std::string track, int64_t tier, std::string operation) {
    auto a = Json::object();
    a.set("id", Json::string(track));
    a.set("tier", Json::number(tier));
    return {"economy", "claim", std::move(operation), a};
}
ServiceRequest chatSend(std::string channel, std::string text, std::string operation) {
    auto a = Json::object();
    a.set("id", Json::string(channel));
    a.set("text", Json::string(text));
    return {"chat", "send", std::move(operation), a};
}
ServiceRequest boardSubmit(std::string board, int64_t score, std::string operation) {
    auto a = Json::object();
    a.set("id", Json::string(board));
    a.set("score", Json::number(score));
    return {"board", "submit", std::move(operation), a};
}
} // namespace requests
} // namespace aiverse::meta
