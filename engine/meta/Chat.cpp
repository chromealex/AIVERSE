#include "Social.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace aiverse::meta {
namespace {
bool utf8(const std::string& s) {
    for(size_t i=0;i<s.size();) {
        auto c=static_cast<unsigned char>(s[i++]);
        if(c<128) { if(c<32&&c!='\n'&&c!='\t') return false; if(c==127) return false; continue; }
        uint32_t cp=0, min=0; size_t n=0;
        if(c>=0xc2&&c<=0xdf) { cp=c&31; n=1; min=128; }
        else if(c>=0xe0&&c<=0xef) { cp=c&15; n=2; min=2048; }
        else if(c>=0xf0&&c<=0xf4) { cp=c&7; n=3; min=65536; } else return false;
        if(n>s.size()-i) return false;
        while(n--) { auto b=static_cast<unsigned char>(s[i++]); if((b&0xc0)!=0x80) return false; cp=(cp<<6)|(b&63); }
        if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return false;
    }
    return true;
}
int64_t integer(const Json& j,int64_t low=0,int64_t high=kMaxAmount) {
    int64_t n=0; auto& s=j.numberLiteral(); auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    if(!j.isNumber()||r.ec!=std::errc{}||r.ptr!=s.data()+s.size()||n<low||n>high) throw std::runtime_error("meta chat: invalid integer"); return n;
}
std::string text(const Json& j) { if(!j.isString()) throw std::runtime_error("meta chat: expected string"); return j.str(); }
Json strings(const std::set<std::string>& v) { Json j=Json::array(); for(auto& s:v) j.push(Json::string(s)); return j; }
std::set<std::string> readStrings(const Json& j) {
    if(!j.isArray()) throw std::runtime_error("meta chat: expected array"); std::set<std::string> result;
    for(auto& x:j.items()) { auto s=text(x); if(s.empty()||!result.insert(s).second) throw std::runtime_error("meta chat: duplicate/empty id"); } return result;
}
}
ChatConfig ChatConfig::parse(const Json& j) {
    if(!j.isObject()) throw std::runtime_error("meta chat: expected object"); ChatConfig c;
    const std::set<std::string> known{"maxBytes","maxHistory","maxMessages","cooldown","retention","kinds","metadata"}; std::set<std::string> seen;
    for(auto& [k,v]:j.members()) { (void)v; if(!known.count(k)||!seen.insert(k).second) throw std::runtime_error("meta chat: unknown/duplicate key "+k); }
    if(j.has("metadata")) { if(!j["metadata"].isObject()) throw std::runtime_error("meta chat: metadata must be object"); c.metadata=j["metadata"]; }
    if(j.has("maxBytes")) c.maxBytes=size_t(integer(j["maxBytes"],1,1048576));
    if(j.has("maxHistory")) c.maxHistory=size_t(integer(j["maxHistory"],1,1000000));
    if(j.has("maxMessages")) c.maxMessages=size_t(integer(j["maxMessages"],1,10000000));
    if(j.has("cooldown")) c.cooldown=integer(j["cooldown"]);
    if(j.has("retention")) c.retention=integer(j["retention"]);
    if(j.has("kinds")) c.kinds=readStrings(j["kinds"]);
    if(c.kinds.empty()) throw std::runtime_error("meta chat: empty kinds"); return c;
}
Chat::Chat(ChatConfig c,AuthorizeChat auth,ModerateChat mod):config_(std::move(c)),authorize_(std::move(auth)),moderate_(std::move(mod)) {
    if(!authorize_||!config_.maxBytes||config_.maxBytes>1048576||!config_.maxHistory||config_.maxHistory>1000000||!config_.maxMessages||config_.maxMessages>10000000||config_.cooldown<0||config_.cooldown>kMaxAmount||config_.retention<0||config_.retention>kMaxAmount||config_.kinds.empty()) throw std::runtime_error("meta: invalid chat policy");
}
Result Chat::send(const std::string& player,const std::string& kind,const std::string& content,Time now,const std::string& op) {
    if(player.empty()||op.empty()||op.size()>512||content.empty()||content.size()>config_.maxBytes||!utf8(content)||!config_.kinds.count(kind)||now<0||now>kMaxAmount) return {Code::Invalid,"message"};
    if(!authorize_(player,"send")) return {Code::Denied,"send"};
    Json b=Json::array(); for(auto& x:{player,kind,content}) b.push(Json::string(x)); auto body=b.dump(-1);
    auto receipt=receipts_.find(op); if(receipt!=receipts_.end()) return receipt->second==body?Result{Code::Duplicate,{}}:Result{Code::Conflict,"operation"};
    if(now<lastTime_) return {Code::Conflict,"clock rollback"};
    auto m=muted_.find(player); if(m!=muted_.end()&&now<m->second) return {Code::Denied,"muted"};
    auto l=lastSend_.find(player); if(l!=lastSend_.end()&&now-l->second<config_.cooldown) return {Code::Limit,"rate limit"};
    if(moderate_&&!moderate_(kind,content)) return {Code::Denied,"moderation"};
    // Stop instead of evicting dedupe state: automatic eviction could replay old sends.
    if(receipts_.size()>=config_.maxMessages||next_>=kMaxAmount) return {Code::Limit,"channel capacity; archive and rotate channel"};
    messages_.push_back({next_++,player,kind,content,now,false}); receipts_[op]=body; lastSend_[player]=now; lastTime_=now;
    if(messages_.size()>config_.maxHistory) messages_.erase(messages_.begin(),messages_.begin()+(messages_.size()-config_.maxHistory));
    for(auto i=reports_.begin();i!=reports_.end();) { if(i->first<messages_.front().sequence) i=reports_.erase(i); else ++i; }
    return {};
}
Result Chat::remove(const std::string& actor,int64_t seq) {
    if(actor.empty()||!authorize_(actor,"delete")) return {Code::Denied,"delete"};
    for(auto& m:messages_) if(m.sequence==seq) { if(m.deleted) return {Code::Duplicate,{}}; m.deleted=true; m.text.clear(); return {}; }
    return {Code::Missing,"message"};
}
Result Chat::report(const std::string& actor,int64_t seq) {
    if(actor.empty()||!authorize_(actor,"report")||!authorize_(actor,"read")) return {Code::Denied,"report"};
    for(auto& m:messages_) if(m.sequence==seq) { return reports_[seq].insert(actor).second?Result{}:Result{Code::Duplicate,{}}; }
    return {Code::Missing,"message"};
}
void Chat::block(const std::string& viewer,const std::string& sender,bool blocked) {
    if(viewer.empty()||sender.empty()) return;
    if(blocked) blocked_[viewer].insert(sender); else { auto i=blocked_.find(viewer); if(i!=blocked_.end()) i->second.erase(sender); }
}
void Chat::mute(const std::string& player,Time until) { if(!player.empty()&&until>=0&&until<=kMaxAmount) muted_[player]=until; }
Result Chat::markRead(const std::string& viewer,int64_t sequence) {
    if(viewer.empty()||!authorize_(viewer,"read")) return {Code::Denied,"read"};
    if(sequence<0||sequence>=next_) return {Code::Invalid,"read cursor"};
    read_[viewer]=std::max(read_[viewer],sequence); return {};
}
bool Chat::visible(const std::string& viewer,const Message& m,Time now) const {
    if(now<0||now>kMaxAmount||m.sent>now||(config_.retention&&now-m.sent>=config_.retention)) return false;
    auto b=blocked_.find(viewer); return b==blocked_.end()||!b->second.count(m.sender);
}
std::vector<Message> Chat::history(const std::string& viewer,Time now,int64_t before,size_t limit) const {
    std::vector<Message> result; if(viewer.empty()||!authorize_(viewer,"read")||before<0) return result;
    limit=std::min(limit,config_.maxHistory);
    for(auto i=messages_.rbegin();i!=messages_.rend()&&result.size()<limit;++i)
        if((!before||i->sequence<before)&&visible(viewer,*i,now)) result.push_back(*i);
    std::reverse(result.begin(),result.end()); return result;
}
size_t Chat::unread(const std::string& viewer,Time now) const {
    if(viewer.empty()||!authorize_(viewer,"read")) return 0;
    auto it=read_.find(viewer); int64_t after=it==read_.end()?0:it->second; size_t n=0;
    for(auto& m:messages_) if(m.sequence>after&&!m.deleted&&m.sender!=viewer&&visible(viewer,m,now)) ++n; return n;
}
Json Chat::save() const {
    Json j=Json::object(); j.set("version",Json::number(int64_t(1))); j.set("next",Json::number(next_)); j.set("lastTime",Json::number(lastTime_));
    Json ms=Json::array(); for(auto& m:messages_) {
        Json x=Json::object(); x.set("sequence",Json::number(m.sequence)); x.set("sender",Json::string(m.sender)); x.set("kind",Json::string(m.kind)); x.set("text",Json::string(m.text)); x.set("sent",Json::number(m.sent)); x.set("deleted",Json::fromBool(m.deleted)); ms.push(x);
    } j.set("messages",ms);
    auto numbers=[&](const char* key,const auto& map) { Json x=Json::object(); for(auto& [k,v]:map) x.set(k,Json::number(v)); j.set(key,x); };
    numbers("lastSend",lastSend_); numbers("muted",muted_); numbers("read",read_);
    Json rs=Json::object(); for(auto& [k,v]:receipts_) rs.set(k,Json::string(v)); j.set("receipts",rs);
    Json bs=Json::object(); for(auto& [k,v]:blocked_) bs.set(k,strings(v)); j.set("blocked",bs);
    Json reports=Json::array(); for(auto& [k,v]:reports_) { Json x=Json::object(); x.set("sequence",Json::number(k)); x.set("players",strings(v)); reports.push(x); } j.set("reports",reports);
    return j;
}
Result Chat::restore(const Json& j) {
    try {
        if(!j.isObject()||integer(j["version"])!=1) return {Code::Invalid,"chat snapshot"};
        Chat next(config_,authorize_,moderate_); next.next_=integer(j["next"],1); next.lastTime_=integer(j["lastTime"]);
        if(!j["messages"].isArray()||j["messages"].size()>config_.maxHistory) return {Code::Invalid,"messages"};
        int64_t seq=0; Time time=0;
        for(auto& x:j["messages"].items()) {
            Message m; m.sequence=integer(x["sequence"],1); m.sent=integer(x["sent"]); m.sender=text(x["sender"]); m.kind=text(x["kind"]); m.text=text(x["text"]);
            if(x["deleted"].type()!=Json::Type::Bool) return {Code::Invalid,"deleted"}; m.deleted=x["deleted"].boolean();
            if(m.sequence<=seq||m.sequence>=next.next_||m.sent<time||m.sent>next.lastTime_||m.sender.empty()||!config_.kinds.count(m.kind)||m.text.size()>config_.maxBytes||!utf8(m.text)||(m.deleted&&!m.text.empty())||(!m.deleted&&m.text.empty())) return {Code::Invalid,"message"};
            seq=m.sequence; time=m.sent; next.messages_.push_back(m);
        }
        auto numbers=[&](const char* key,auto& dst,int64_t max) {
            if(!j[key].isObject()) throw std::runtime_error("meta chat: missing "+std::string(key));
            for(auto& [k,v]:j[key].members()) { if(k.empty()||dst.count(k)) throw std::runtime_error("meta chat: duplicate/empty id"); dst[k]=integer(v,0,max); }
        };
        numbers("lastSend",next.lastSend_,next.lastTime_); numbers("muted",next.muted_,kMaxAmount); numbers("read",next.read_,next.next_-1);
        if(!j["receipts"].isObject()||j["receipts"].size()>config_.maxMessages||!j["blocked"].isObject()||!j["reports"].isArray()) return {Code::Invalid,"chat collections"};
        for(auto& [k,v]:j["receipts"].members()) { if(k.empty()||next.receipts_.count(k)) return {Code::Invalid,"receipt"}; next.receipts_[k]=text(v); }
        for(auto& [k,v]:j["blocked"].members()) { if(k.empty()||next.blocked_.count(k)) return {Code::Invalid,"blocked"}; next.blocked_[k]=readStrings(v); }
        for(auto& x:j["reports"].items()) {
            auto s=integer(x["sequence"],1,next.next_-1); if(next.reports_.count(s)) return {Code::Invalid,"report"};
            if(std::none_of(next.messages_.begin(),next.messages_.end(),[&](auto& m){return m.sequence==s;})) return {Code::Invalid,"report target"};
            next.reports_[s]=readStrings(x["players"]);
        }
        *this=std::move(next); return {};
    } catch(const std::exception& e) { return {Code::Invalid,e.what()}; }
}
} // namespace aiverse::meta
