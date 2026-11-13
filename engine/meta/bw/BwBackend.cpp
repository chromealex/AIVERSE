#include "BwBackend.h"
#include "BwWire.h"

namespace aiverse::meta::bw {

Code mapCode(int c) {
    switch (ApiCode(c)) {
    case ApiCode::Ok: case ApiCode::NotModified: return Code::Ok;
    case ApiCode::NotEnoughMoney: return Code::Insufficient;
    case ApiCode::NotAvailable: case ApiCode::Maintenance: case ApiCode::Timeout: case ApiCode::TransportError:
    case ApiCode::InternalError: case ApiCode::Unknown: case ApiCode::NoCache: return Code::Unavailable;
    case ApiCode::AlreadyHave: return Code::Conflict;
    case ApiCode::NotFound: return Code::Missing;
    case ApiCode::NotAuthorized: case ApiCode::WrongAuth: case ApiCode::Banned: case ApiCode::UpdateAuth:
    case ApiCode::InvalidSecureLink: case ApiCode::CustomKicked: return Code::Denied;
    case ApiCode::NotImplemented: case ApiCode::NeedClientUpgrade: return Code::Unsupported;
    default: return Code::Invalid;   // VALIDATION, BAD_REQUEST, promo codes, custom room errors
    }
}

namespace {
Json errorJson(const ApiError& e) {
    Json j = Json::object();
    j.set("code", Json::number(int64_t(e.code)));
    if (!e.key.empty()) j.set("key", Json::string(e.key));
    if (!e.text.empty()) j.set("text", Json::string(e.text));
    if (!e.obj.empty()) j.set("obj", Json::string(e.obj));
    // Unity client localization key: "Errors|<key with . as |>" or "Errors|<CODE>"
    std::string loc = e.key;
    for (auto& ch : loc) if (ch == '.') ch = '|';
    j.set("loc", Json::string("Errors|" + (loc.empty() ? std::to_string(e.code) : loc)));
    return j;
}
// Stable positive chunk request id from the operation: a retry of the same operation reaches the
// server's chunk cache (ProfileRequestChunkKey = profile + request id) instead of executing twice.
int64_t requestIdFor(const std::string& operation) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : operation) { h ^= c; h *= 1099511628211ull; }
    return int64_t(h & 0x1fffffffffffffull) | 1;   // 53 bits: survives JSON readers using doubles
}
void copyIf(Json& to, const Json& from, const char* key, const char* as = nullptr) {
    if (from.has(key) && !from[key].isNull()) to.set(as ? as : key, from[key]);
}
} // namespace

Json projectView(const Json& p, int64_t revision) {
    Json out = Json::object();
    const std::string player = std::to_string(jsonInt(p["userId"]));
    out.set("source", Json::string("bunkerwars"));
    out.set("revision", Json::number(revision));
    out.set("player", Json::string(player));
    Json profile = Json::object();
    profile.set("player", Json::string(player));
    Json balances = Json::object();
    for (auto key : {"gold", "gems", "runes", "spareParts"})
        if (p.has(key)) balances.set(key, Json::number(jsonInt(p[key])));
    for (auto& c : p["currencies"].items()) {   // CurrencyTO {clientId, count, subtype}
        std::string id = "currency." + std::to_string(jsonInt(c["clientId"]));
        if (jsonInt(c["subtype"])) id += "." + std::to_string(jsonInt(c["subtype"]));
        balances.set(id, Json::number(jsonInt(c["count"])));
    }
    profile.set("balances", balances);
    Json attributes = Json::object();
    for (auto key : {"name", "exp", "avatarIndex", "avatarBorderIndex", "leagueIndex", "leaguePlace", "leagueRating", "bestLeagueIndex",
                     "eloRating", "premiumFlag", "heroClientId", "platform", "country", "createTs", "activeDays", "renamePrice",
                     "chatBanned", "features", "binDataVersion", "mpLockSeconds", "versionAvailable"})
        copyIf(attributes, p, key);
    profile.set("attributes", attributes);
    out.set("profile", profile);
    // Engine view keys the reference screens read; BW offers come as OffersTO.
    Json offers = Json::array();
    for (auto& o : p["offers"]["offers"].items()) offers.push(o);
    out.set("offers", offers);
    out.set("tags", Json::array());
    out.set("liveops", Json::object());
    copyIf(out, p, "heroes");
    copyIf(out, p, "skills");
    copyIf(out, p, "things", "inventory");
    copyIf(out, p, "userStorage", "storage");
    copyIf(out, p, "cargoes");
    copyIf(out, p, "profileClanData", "clan");
    copyIf(out, p, "challenges");
    copyIf(out, p, "quests");
    copyIf(out, p, "forge");
    copyIf(out, p, "premium");
    copyIf(out, p, "passive");
    copyIf(out, p, "stat");
    copyIf(out, p, "drops");
    copyIf(out, p, "levelUps");
    out.set("raw", p);
    return out;
}

BwBackend::BwBackend(ApiClient& api, int64_t profileMask) : api_(api), mask_(profileMask) {}

namespace {
constexpr uint16_t kChatOnMsg = 0, kChatOnMember = 1, kChatOnHistory = 2, kChatGetHistory = 3, kChatOnMemberList = 4, kChatSetState = 5,
                   kChatRegister = 6, kChatOnStateList = 7, kChatGetRooms = 8, kChatOnRooms = 9, kChatNotifyBase = 0x10, kChatDoRead = 0x20;
void keepLast(Json& arr, size_t n) { while (arr.size() > n) arr.eraseAt(0); }
} // namespace

void BwBackend::onChatPacket(uint16_t type, const std::string& text) {
    Json j;
    try { j = Json::parse(text); } catch (const std::exception&) { return; }
    if (!chat_.isObject()) chat_ = Json::object();
    auto store = [&](const Json& msg) {
        Json& rooms = chat_.member("rooms");
        const std::string room = msg.getStr("room", "global");
        Json* list = rooms.find(room);
        if (!list) { rooms.set(room, Json::array()); list = rooms.find(room); }
        list->push(msg);
        keepLast(*list, 100);
    };
    if (type == kChatOnMsg) store(j);
    else if (type == kChatOnHistory) for (auto& m : j["list"].items()) store(m);
    else if (type == kChatOnMember || type == kChatOnMemberList || type == kChatOnStateList) {
        Json& members = chat_.member("members");
        auto put = [&](const Json& m) { members.set(std::to_string(jsonInt(m["senderId"])), m); };
        if (j["list"].isArray()) for (auto& m : j["list"].items()) put(m); else put(j);
    } else if (type == kChatOnRooms) chat_.set("roomList", j["list"]);
    else if (type >= kChatNotifyBase && type < kChatDoRead) {   // friend / game / spectator / clan notifications
        Json n = j;
        n.set("type", Json::number(int64_t(type)));
        Json* list = chat_.find("notifications");
        if (!list) { chat_.set("notifications", Json::array()); list = chat_.find("notifications"); }
        list->push(n);
        keepLast(*list, 100);
    } else {
        Json n = j;   // quests, clan events, customer support, profile update
        n.set("type", Json::number(int64_t(type)));
        Json* list = chat_.find("events");
        if (!list) { chat_.set("events", Json::array()); list = chat_.find("events"); }
        list->push(n);
        keepLast(*list, 100);
    }
    const uint16_t answers = type == kChatOnHistory ? kChatGetHistory : type == kChatOnRooms ? kChatGetRooms
                           : type == kChatOnStateList ? kChatRegister : 0xffff;
    for (auto it = chatWaiting_.begin(); it != chatWaiting_.end(); ++it)
        if (it->first == answers) {
            Callback cb = std::move(it->second);
            chatWaiting_.erase(it);
            ServiceReply reply;
            reply.data = j.isObject() ? j : Json::object();
            if (cb) cb(std::move(reply));
            break;
        }
}

void BwBackend::chatRequest(const ServiceRequest& r, Callback cb) {
    auto reply = [this, cb](Code code, std::string detail) {
        api_.defer([cb, code, detail]() { if (cb) cb({{code, detail}, Json::object()}); });
    };
    const Json& a = r.arguments;
    Json to = Json::object();
    uint16_t type = 0xffff, expect = 0xffff;
    const std::string& act = r.action;
    if (act == "send") {
        if (a.getStr("msg").empty()) { reply(Code::Invalid, "chat.send: msg"); return; }
        type = kChatOnMsg;
        to.set("messageType", Json::number(a.getInt("messageType", 0)));
        to.set("ts", Json::number(int64_t(0)));
        to.set("room", Json::string(a.getStr("room", "global")));
        to.set("senderId", Json::number(chatUser_));
        to.set("sender", Json::string(chatName_));
        to.set("msg", Json::string(a.getStr("msg")));
    } else if (act == "history") {
        type = kChatGetHistory; expect = kChatGetHistory;
        to.set("room", Json::string(a.getStr("room")));
        to.set("historyCount", Json::number(a.getInt("count", 50)));
    } else if (act == "rooms") {
        type = kChatGetRooms; expect = kChatGetRooms;
    } else if (act == "state") {
        type = kChatSetState;
        to.set("whereIs", Json::number(a.getInt("whereIs")));
        to.set("state", Json::number(a.getInt("state")));
    } else if (act == "watch") {
        type = kChatRegister; expect = kChatRegister;
        to.set("senderIds", a["senderIds"].isArray() ? a["senderIds"] : Json::array());
    } else if (act == "notifyRead" || act == "notifyAccept" || act == "notifyDecline" || act == "notifyRemove") {
        type = uint16_t(kChatDoRead + (act == "notifyRead" ? 0 : act == "notifyAccept" ? 1 : act == "notifyDecline" ? 2 : 3));
        to.set("reqId", Json::number(jsonInt(a["reqId"], jsonInt(a["id"]))));
        to.set("id", Json::number(jsonInt(a["id"])));
    } else {
        reply(Code::Unsupported, "bunkerwars: no chat action " + act);
        return;
    }
    if (!chatSend || !chatSend(type, to.dump(-1))) { reply(Code::Unavailable, "chat: not connected"); return; }
    if (expect != 0xffff) chatWaiting_.push_back({expect, std::move(cb)});
    else reply(Code::Ok, {});
}

void BwBackend::request(ServiceRequest r, Callback cb) {
    if (r.service == "chat") { chatRequest(r, std::move(cb)); return; }
    const bool view = r.service == "meta" && r.action == "view";
    const Route* route = view ? nullptr : findRoute(r.service, r.action);
    if (!view && !route) {
        // Delivered from pump like every other reply.
        api_.defer([cb, s = r.service, a = r.action]() {
            if (cb) cb({{Code::Unsupported, "bunkerwars: no route for " + s + "." + a}, Json::object()});
        });
        return;
    }
    std::string path = view ? "v/profile/get" : route->path;
    const unsigned flags = view ? (kMutates | kChunk) : route->flags;
    CallOptions opt;
    opt.chunked = (flags & kChunk) != 0;
    opt.cacheable = (flags & kCache) != 0;
    opt.staticData = (flags & kStatic) != 0;
    opt.noAuthRetry = (flags & kNoAuth) != 0;
    if ((flags & kMutates) && !r.operation.empty()) opt.requestId = requestIdFor(r.operation);
    Json args = r.arguments.isObject() ? r.arguments : Json::object();
    if (view && !args.has("updateMask")) args.set("updateMask", Json::number(mask_));
    const bool mutates = (flags & kMutates) != 0;
    auto done = [this, cb, view, mutates, chunked = opt.chunked && api_.chunks()](ApiResult res) {
        ServiceReply reply;
        reply.result.code = res.ok() ? Code::Ok : mapCode(res.error.code);
        if (!res.ok() && reply.result.code == Code::Ok) reply.result.code = Code::Unavailable;   // 304 without a cached copy
        if (!res.ok()) {
            reply.result.detail = "bunkerwars " + std::to_string(res.error.code) + (res.error.key.empty() ? "" : " " + res.error.key) +
                                  (res.error.text.empty() ? "" : ": " + res.error.text);
            reply.data.set("error", errorJson(res.error));
            // Only a request the server can deduplicate (chunk request id) may be resent after an
            // uncertain failure; other BW mutations would apply twice, so they end here and the UI
            // refreshes the view instead.
            reply.transportUncertain = res.transport && (!mutates || chunked);
            if (res.transport && mutates && !chunked) reply.data.set("refresh", Json::fromBool(true));
        } else if (view) {
            view_ = projectView(res.data, ++revision_);
            view_.set("chat", chat_);
            if (!chatUser_) chatUser_ = jsonInt(res.data["userId"]);
            if (chatName_.empty()) chatName_ = res.data.getStr("name");
            reply.data = view_;
        } else {
            reply.data = res.data.isObject() ? res.data : Json::object();
            if (!res.data.isObject() && !res.data.isNull()) reply.data.set("value", res.data);
            if (res.notModified) reply.data.set("notModified", Json::fromBool(true));
            if (res.raw["offers"].isObject()) reply.data.set("offers", res.raw["offers"]);   // ResponseData.offers (deprecated)
        }
        if (cb) cb(std::move(reply));
    };
    if (r.service == "auth" && r.action == "login") {
        api_.auth(std::move(args), done);
        return;
    }
    api_.call(path, std::move(args), done, opt);
}

} // namespace aiverse::meta::bw
