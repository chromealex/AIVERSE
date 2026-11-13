#include "BwTutorials.h"
#include "BwWire.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace aiverse::meta::bw {

bool bwCompare(int op, double v, double rt) {
    switch (op) {
    case 0: return rt == v;
    case 1: return rt != v;
    case 2: return rt < v;
    case 3: return rt > v;
    case 4: return rt <= v;
    case 5: return rt >= v;
    default: return false;
    }
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }
std::string lastSegment(const std::string& s) { const size_t d = s.rfind('.'); return d == std::string::npos ? s : s.substr(d + 1); }
// unity_ui importer screen name: class without "Screen", lower case (FindMatchScreen -> findmatch)
std::string screenId(const std::string& cls) {
    std::string s = lastSegment(cls);
    if (s.size() > 6 && s.compare(s.size() - 6, 6, "Screen") == 0) s.resize(s.size() - 6);
    return lower(s);
}
int64_t num(const Json& j, const char* k, int64_t def = 0) { return jsonInt(j[k], def); }
bool flag(const Json& j, const char* k) { return j[k].type() == Json::Type::Bool ? j.getBool(k) : jsonInt(j[k]) != 0; }
// "1.18.9" compare: -1, 0, 1
int versionCmp(const std::string& a, const std::string& b) {
    size_t i = 0, k = 0;
    while (i < a.size() || k < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') x = x * 10 + (a[i++] - '0');
        while (k < b.size() && b[k] != '.') y = y * 10 + (b[k++] - '0');
        if (x != y) return x < y ? -1 : 1;
        if (i < a.size()) ++i;
        if (k < b.size()) ++k;
    }
    return 0;
}
// dd-MM-yyyy -> unix ms (UTC); 0 when malformed
int64_t dateMs(const std::string& s) {
    if (s.size() != 10 || s[2] != '-' || s[5] != '-') return 0;
    const int d = std::atoi(s.substr(0, 2).c_str()), m = std::atoi(s.substr(3, 2).c_str()), y = std::atoi(s.substr(6, 4).c_str());
    if (d < 1 || m < 1 || m > 12 || y < 1970) return 0;
    // days from civil (Howard Hinnant)
    const int yy = y - (m <= 2);
    const int era = yy / 400, yoe = yy - era * 400, mp = (m + 9) % 12;
    const int doy = (153 * mp + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = int64_t(era) * 146097 + doe - 719468;
    return days * 86400000;
}
} // namespace

bool MetaTutorials::load(const Json& doc, std::string* error) {
    defs_.clear();
    runs_.clear();
    const Json& list = doc["tutorials"];
    if (!list.isArray()) { if (error) *error = "bw tutorials: no \"tutorials\" array"; return false; }
    for (auto& t : list.items()) {
        Def d;
        d.name = t.getStr("name");
        d.guid = t.getStr("guid");
        d.forObject = t.getStr("forObject");
        d.startEvent = int(jsonInt(t["startEvent"], 3));
        d.conditions = t["conditions"].isArray() ? t["conditions"] : Json::array();
        d.actions = t["actions"].isArray() ? t["actions"] : Json::array();
        defs_.push_back(std::move(d));
    }
    return true;
}

void MetaTutorials::setKeys(const Json& bin) {
    keys_.clear();
    for (auto& [k, v] : bin.members()) if (v.isNumber() || v.isString()) keys_[k] = jsonInt(v);
}

bool MetaTutorials::windowMatches(const std::string& window, const std::string& forObject) {
    if (forObject.empty()) return true;   // no type restriction
    if (window.empty()) return false;
    if (window == forObject) return true;
    const std::string w = lower(window), seg = lower(lastSegment(forObject));
    return w == seg || w == screenId(forObject) || screenId(window) == screenId(forObject);
}

uint32_t MetaTutorials::random() {   // PCG32
    const uint64_t old = rng_;
    rng_ = old * 6364136223846793005ull + 1442695040888963407ull;
    const uint32_t x = uint32_t(((old >> 18u) ^ old) >> 27u), rot = uint32_t(old >> 59u);
    return (x >> rot) | (x << ((32 - rot) & 31));
}

bool MetaTutorials::evaluate(const Json& c, const std::string& window) {
    const std::string type = c.getStr("type");
    const int op = int(num(c, "conditionValue"));
    const Json& p = profile_;
    if (type == "HasKey") return bwCompare(op, double(num(c, "value")), double(key(c.getStr("key"))));
    if (type == "ExistKey") return (keys_.count(c.getStr("key")) != 0) == flag(c, "result");
    if (type == "HasKeyTimer") {
        if (!bwCompare(op, double(num(c, "value")), double(key(c.getStr("key"))))) return false;
        const int64_t until = key(c.getStr("key") + ".Timer") + num(c, "seconds");
        return flag(c, "result") ? now_ < until : now_ >= until;
    }
    if (type == "HasCounter") { auto it = counters_.find(int(num(c, "id"))); return bwCompare(op, double(num(c, "value")), double(it == counters_.end() ? 0 : it->second)); }
    if (type == "SplitTest") {
        // profile.HasAnySplitTest: the ab block / splitTest string lists the player's test variants
        const std::string want = c.getStr("splitTest");
        bool has = false;
        auto scan = [&](const Json& j, auto& self) -> void {
            if (j.isString()) { const std::string s = j.str(); has |= s == want || ("," + s + ",").find("," + want + ",") != std::string::npos; }
            for (auto& e : j.items()) self(e, self);
            for (auto& [k, v] : j.members()) { if (k == want) has = true; self(v, self); }
        };
        scan(p["splitTest"], scan);
        scan(p["ab"], scan);
        return has == !flag(c, "isNot");
    }
    if (type == "ProfileCurrentHero") {
        const int64_t id = num(c, "heroId");
        if (flag(c, "checkRewards")) { bool r = false; if (host.condition && host.condition(c, r)) return r; unsupported_.insert(type + ".checkRewards"); return false; }
        return id == 0 || num(p, "heroClientId") == id;
    }
    if (type == "IsOfflineMode") return offline_ == flag(c, "offlineMode");
    if (type == "VersionAvailableCheck") {
        const std::string av = p.getStr("versionAvailable");
        if (av.empty() || version_.empty()) return false;
        const int cmp = versionCmp(av, version_);
        bool res = false;
        switch (int(num(c, "op"))) {
        case 0: res = cmp == 0; break;
        case 1: res = cmp != 0; break;
        case 2: res = cmp < 0; break;
        case 3: res = cmp > 0; break;
        case 4: res = cmp <= 0; break;
        case 5: res = cmp >= 0; break;
        }
        return res == flag(c, "result");
    }
    if (type == "HasCountry") {
        std::string cc = country_.empty() ? p.getStr("country") : country_;
        for (auto& ch : cc) ch = char(std::toupper(uint8_t(ch)));
        std::string want = c.getStr("countryCode");
        for (auto& ch : want) ch = char(std::toupper(uint8_t(ch)));
        return !cc.empty() && cc == want;
    }
    if (type == "ProfileRegistrationDate") {
        const int64_t reg = num(p, "createTs"), d = dateMs(c.getStr("dateUtc"));
        return reg > 0 && d > 0 && bwCompare(op, double(d), double(reg));
    }
    if (type == "ProfileActiveDays") return bwCompare(op, double(num(c, "value")), double(num(p, "activeDays")));
    if (type == "HasProfileContent") {
        bool features = true, values = true;
        const Json& fs = c["features"];
        const bool anyF = num(c, "checkType") == 0;
        if (fs.size()) {
            features = !anyF;
            for (auto& f : fs.items()) {
                const bool hero = num(f, "contentType") == 0;
                const Json* found = nullptr;
                for (auto& h : p[hero ? "heroes" : "skills"].items()) if (num(h, "clientId") == num(f, "id")) { found = &h; break; }
                bool r = (found != nullptr) != flag(f, "haveNot");
                if (r && found && !bwCompare(int(num(f, "conditionValue")), double(num(f, "level")), double(num(*found, "level")))) r = flag(f, "haveNot");
                if (anyF && r) features = true;
                if (!anyF && !r) features = false;
            }
        }
        const Json& vs = c["values"];
        const bool anyV = num(c, "valuesCheckType") == 0;
        if (vs.size()) {
            values = !anyV;
            for (auto& v : vs.items()) {
                static const char* field[] = {"leagueRating", nullptr, nullptr, nullptr, "gold", "gems", "runes", nullptr, nullptr, nullptr, nullptr, nullptr, "spareParts"};
                const int64_t kind = num(v, "contentType");
                bool r;
                if (kind >= 0 && kind < 13 && field[kind]) r = bwCompare(int(num(v, "conditionValue")), double(num(v, "value")), double(num(p, field[kind])));
                else { bool hr = false; r = host.condition && host.condition(v, hr) ? hr : false; if (!host.condition) unsupported_.insert("HasProfileContent.value" + std::to_string(kind)); }
                if (anyV && r) values = true;
                if (!anyV && !r) values = false;
            }
        }
        return features && values;
    }
    if (type == "HasProfileHeroSlotIsNull") {   // current hero's skill slot <slotId> empty
        for (auto& h : p["heroes"].items())
            if (num(h, "clientId") == num(p, "heroClientId")) {
                const Json& s = h["skills"];
                const int64_t slot = num(c, "slotId");
                return slot < 0 || size_t(slot) >= s.size() || jsonInt(s[size_t(slot)]) == 0;
            }
        return true;
    }
    if (type == "HasProfileFeatures") {   // feature bit (ProfileResponseTO.features) by feature id when known
        bool r = false;
        if (host.condition && host.condition(c, r)) return r;
        unsupported_.insert(type);
        return false;
    }
    bool r = false;
    if (host.condition && host.condition(c, r)) return r;
    unsupported_.insert(type);
    (void)window;
    return false;
}

bool MetaTutorials::conditionsHold(size_t d, const std::string& window) {
    for (auto& c : defs_[d].conditions.items())
        if (!c.isObject() || !evaluate(c, window)) return c.isObject() ? false : true;   // a null condition counts as true (BW)
    return true;
}

void MetaTutorials::start(size_t d, const std::string& window) {
    for (auto& r : runs_) if (r.def == d && !r.done) return;   // one instance at a time
    Run r;
    r.def = d;
    r.window = window;
    runs_.push_back(r);
    advance(runs_.back());   // deque: references to other runs stay valid
    sweep();
}

void MetaTutorials::sweep() {
    if (depth_ == 0) runs_.erase(std::remove_if(runs_.begin(), runs_.end(), [](const Run& x) { return x.done; }), runs_.end());
}

int MetaTutorials::windowEvent(const std::string& window, TutorialEvent ev) {
    int started = 0;
    if (ev == TutorialEvent::OnWindowInactive)   // WindowSystemInput.ResetWaitForClickOnElement
        for (auto& r : runs_) if (r.window == window && r.wait == Wait::Click) r.wait = Wait::None;
    for (size_t d = 0; d < defs_.size(); ++d) {
        if (defs_[d].startEvent != int(ev) || !windowMatches(window, defs_[d].forObject)) continue;
        if (!conditionsHold(d, window)) continue;
        start(d, window);
        ++started;
    }
    return started;
}

bool MetaTutorials::run(const std::string& id, const std::string& window) {
    const std::string l = lower(id);
    for (size_t d = 0; d < defs_.size(); ++d)
        if (defs_[d].guid == id || lower(defs_[d].name) == l) {
            if (!conditionsHold(d, window)) return false;
            start(d, window);
            return true;
        }
    return false;
}

void MetaTutorials::advance(Run& r) {
    if (++depth_ > 32) { --depth_; r.done = true; return; }   // RunTutorial loops
    const Json& acts = defs_[r.def].actions;
    r.wait = Wait::None;
    while (r.index < acts.size() && r.wait == Wait::None && !r.done) {
        const Json& a = acts[r.index++];
        const std::string type = a.getStr("type");
        if (type == "Wait") { r.wait = Wait::Time; r.timer = a["seconds"].isNumber() ? std::strtod(a["seconds"].numberLiteral().c_str(), nullptr) : 0; }
        else if (type == "WaitForClick") r.wait = Wait::Click;
        else if (type == "WaitButtonClick") { r.wait = Wait::Button; r.tag = a.getStr("tutorialTag"); }
        else if (type == "ShowHideComponents") { if (host.showHide) host.showHide(a.getStr("tutorialTag"), num(a, "state") == 0, a); }
        else if (type == "LockElement") { if (host.lockElement) host.lockElement(a.getStr("tutorialTag"), a); }
        else if (type == "SetKey" || type == "SetKeyRandom") {
            const std::string k = a.getStr("key");
            int64_t v = key(k);
            if (type == "SetKeyRandom") {
                const int64_t lo = num(a, "fromValue"), hi = num(a, "toValue");
                v = hi > lo ? lo + int64_t(random() % uint32_t(hi - lo)) : lo;
            } else {
                const int64_t x = num(a, "value");
                const int64_t o = num(a, "operation");
                v = o == 0 ? x : o == 1 ? v + x : v - x;
            }
            keys_[k] = v;
            keys_[k + ".Timer"] = now_;
            const bool save = flag(a, "save");
            if (host.keyChanged) host.keyChanged(k, v, save);
            if (save) r.wait = Wait::Save;
        }
        else if (type == "CounterAdd") counters_[int(num(a, "id"))] += num(a, "value", 1);
        else if (type == "CounterSet") counters_[int(num(a, "id"))] = num(a, "value");
        else if (type == "RunTutorial") {
            const std::string g = a.getStr("nextTutorialGuid", a["nextTutorial"].getStr("guid"));
            run(g, r.window);
            if (flag(a, "breakExecution")) r.done = true;
        }
        else if (type == "StopAllWaits" || type == "ResetWaitForClick") {
            for (auto& o : runs_) if (&o != &r && (type == "StopAllWaits" ? o.wait != Wait::None : o.wait == Wait::Click)) o.wait = Wait::None;
        }
        else if (host.action) host.action(a, r.window);
    }
    if (r.index >= acts.size() && r.wait == Wait::None) r.done = true;
    --depth_;
}

void MetaTutorials::update(double seconds) {
    for (size_t i = 0; i < runs_.size(); ++i) {
        Run& r = runs_[i];
        if (r.wait == Wait::Time) { r.timer -= seconds; if (r.timer <= 0) { r.wait = Wait::None; advance(runs_[i]); } }
        else if (r.wait == Wait::None && !r.done) advance(runs_[i]);   // released by StopAllWaits
    }
    sweep();
}

void MetaTutorials::click() {
    for (size_t i = 0; i < runs_.size(); ++i) if (runs_[i].wait == Wait::Click) advance(runs_[i]);
    sweep();
}

void MetaTutorials::buttonClick(const std::string& tag) {
    for (size_t i = 0; i < runs_.size(); ++i) if (runs_[i].wait == Wait::Button && runs_[i].tag == tag) advance(runs_[i]);
    sweep();
}

void MetaTutorials::keySaved() {
    for (size_t i = 0; i < runs_.size(); ++i) if (runs_[i].wait == Wait::Save) advance(runs_[i]);
    sweep();
}

std::vector<std::string> MetaTutorials::runningNames() const {
    std::vector<std::string> out;
    for (auto& r : runs_) if (!r.done) out.push_back(defs_[r.def].name);
    return out;
}

} // namespace aiverse::meta::bw
