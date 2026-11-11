#include "Meta.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aiverse::meta {
namespace {
Amount value(const Bundle& b, const std::string& k) { auto i=b.find(k); return i==b.end()?0:i->second; }
std::string payload(const std::string& kind, const std::string& id, const Bundle& cost={}, const Bundle& reward={}) {
    Json j=Json::array(); j.push(Json::string(kind)); j.push(Json::string(id));
    for(const auto* b:{&cost,&reward}) { Json x=Json::object(); for(auto& [k,v]:*b) x.set(k,Json::number(v)); j.push(x); }
    return j.dump(-1);
}
std::string key(const std::string& id, int64_t bucket) { return "@meta/"+payload("bucket",id,{{"n",bucket}}); }
bool validTime(Time t) { return t>=0 && t<=kMaxAmount; }
}
bool Condition::matches(const Context& c) const {
    for(auto& [k,v]:minimum) { auto i=c.values.find(k); if(i==c.values.end()||i->second<v) return false; }
    for(auto& [k,v]:maximum) { auto i=c.values.find(k); if(i==c.values.end()||i->second>v) return false; }
    for(auto& t:requiredTags) if(!c.tags.count(t)) return false;
    for(auto& t:excludedTags) if(c.tags.count(t)) return false;
    for(auto& x:all) if(!x.matches(c)) return false;
    for(auto& x:none) if(x.matches(c)) return false;
    return any.empty() || std::any_of(any.begin(),any.end(),[&](auto& x){return x.matches(c);});
}
bool Schedule::active(Time now) const {
    return validTime(now) && now>=start && (!end||now<end) && (!period||!duration||(now-start)%period<duration);
}
int64_t Schedule::bucket(Time now) const { return now<start?-1:period?(now-start)/period:0; }
Economy::Economy(Config config, Profile profile, Commit commit):config_(std::move(config)),profile_(std::move(profile)),commit_(std::move(commit)) {
    config_=Config::parse(config_.save()); // Typed and JSON inputs share one validator.
    profile_=Profile::load(profile_.save(),config_);
    if(profile_.revision<0) throw std::runtime_error("meta: negative revision");
    Profile empty;
    if(!apply(empty,{},profile_.balances)) throw std::runtime_error("meta: invalid profile balances");
}
Result Economy::replay(const std::string& op, const std::string& body) const {
    if(op.empty()||op.size()>512) return {Code::Invalid,"operation id"};
    auto i=profile_.receipts.find(op);
    if(i==profile_.receipts.end()) return {};
    return i->second==body?Result{Code::Duplicate,{}}:Result{Code::Conflict,"operation reused with different payload"};
}
Result Economy::apply(Profile& next, const Bundle& cost, const Bundle& reward) const {
    for(const auto* b:{&cost,&reward}) for(auto& [id,n]:*b)
        if(!config_.assets.count(id)||n<0||n>kMaxAmount) return {Code::Invalid,"asset or amount: "+id};
    Bundle result=next.balances;
    for(auto& [id,n]:cost) { if(value(result,id)<n) return {Code::Insufficient,id}; result[id]-=n; }
    for(auto& [id,n]:reward) {
        Amount cap=config_.assets.at(id).cap;
        if(n>cap || value(result,id)>cap-n) return {Code::Limit,id};
        result[id]+=n;
    }
    next.balances=std::move(result); return {};
}
Result Economy::publish(Profile next) {
    if(profile_.revision==std::numeric_limits<int64_t>::max()) return {Code::Limit,"revision"};
    next.revision=profile_.revision+1;
    if(commit_&&!commit_(profile_.revision,next.save())) return {Code::Conflict,"storage commit"};
    profile_=std::move(next); return {};
}
Result Economy::transact(const std::string& op,const Bundle& cost,const Bundle& reward) {
    auto body=payload("transaction","",cost,reward); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto next=profile_; r=apply(next,cost,reward); if(!r) return r;
    next.receipts[op]=body; return publish(std::move(next));
}
std::vector<std::string> Economy::visibleOffers(const Context& c) const {
    std::vector<std::string> ids;
    for(auto& [id,o]:config_.offers) {
        if(!o.schedule.active(c.now)||!o.condition.matches(c)) continue;
        auto i=profile_.uses.find(key(id,o.schedule.bucket(c.now)));
        if(i!=profile_.uses.end()&&((o.limit&&i->second.count>=o.limit)||c.now<i->second.last||c.now-i->second.last<o.cooldown)) continue;
        ids.push_back(id);
    }
    std::stable_sort(ids.begin(),ids.end(),[&](auto& a,auto& b){return config_.offers.at(a).priority>config_.offers.at(b).priority;});
    return ids;
}
Result Economy::buy(const std::string& id,const Context& c,const std::string& op) { return purchase(id,c,op,false); }
Result Economy::fulfill(const std::string& id,const Context& c,const std::string& token) { return purchase(id,c,"iap:"+token,true); }
Result Economy::purchase(const std::string& id,const Context& c,const std::string& op,bool paid) {
    auto body=payload(paid?"iap":"buy",id); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto i=config_.offers.find(id); if(i==config_.offers.end()) return {Code::Missing,id};
    const auto& o=i->second;
    if(paid!=!o.product.empty() || (paid&&op.size()==4)) return {Code::Denied,"purchase authority"};
    if(!profile_.player.empty()&&profile_.player!=c.player) return {Code::Denied,"player"};
    auto visible=visibleOffers(c);
    if(!paid && std::find(visible.begin(),visible.end(),id)==visible.end()) return {Code::Unavailable,id};
    auto next=profile_; r=apply(next,o.cost,o.reward); if(!r) return r;
    auto& use=next.uses[key(id,o.schedule.bucket(c.now))];
    if(use.count==kMaxAmount) return {Code::Limit,"purchase count"};
    ++use.count; use.last=c.now; next.receipts[op]=body;
    return publish(std::move(next));
}
Result Economy::reserveOrder(const std::string& id,const Context& c,const std::string& order) {
    if(order.empty()||order.size()>512) return {Code::Invalid,"order id"};
    if(!profile_.player.empty()&&profile_.player!=c.player) return {Code::Denied,"player"};
    auto found=profile_.orders.find(order);
    if(found!=profile_.orders.end()) return found->second.offer==id&&found->second.state!=OrderState::Cancelled?Result{Code::Duplicate,{}}:Result{Code::Conflict,"order reused or cancelled"};
    auto it=config_.offers.find(id); if(it==config_.offers.end()) return {Code::Missing,id}; const auto& o=it->second;
    if(o.product.empty()) return {Code::Invalid,"not a real-money offer"};
    auto visible=visibleOffers(c); if(std::find(visible.begin(),visible.end(),id)==visible.end()) return {Code::Unavailable,id};
    auto next=profile_; const auto useKey=key(id,o.schedule.bucket(c.now)); auto& use=next.uses[useKey];
    if(use.count==kMaxAmount) return {Code::Limit,"orders"}; ++use.count; use.last=c.now;
    next.orders[order]={id,o.product,useKey,o.reward,OrderState::Pending}; return publish(std::move(next));
}
Result Economy::fulfillOrder(const std::string& id,const std::string& token) {
    if(token.empty()||token.size()>508) return {Code::Invalid,"verified token"};
    auto body=payload("order",id); auto op="iap:"+token; auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto it=profile_.orders.find(id); if(it==profile_.orders.end()) return {Code::Missing,id};
    if(it->second.state==OrderState::Fulfilled) return {Code::Conflict,"order already paid with another token"};
    if(it->second.state!=OrderState::Pending) return {Code::Conflict,"cancelled order"};
    auto next=profile_; r=apply(next,{},it->second.reward); if(!r) return r;
    next.orders[id].state=OrderState::Fulfilled; next.receipts[op]=body; return publish(std::move(next));
}
Result Economy::cancelOrder(const std::string& id) {
    auto it=profile_.orders.find(id); if(it==profile_.orders.end()) return {Code::Missing,id};
    if(it->second.state==OrderState::Cancelled) return {Code::Duplicate,{}};
    if(it->second.state==OrderState::Fulfilled) return {Code::Conflict,"paid order"};
    auto next=profile_; auto& count=next.uses[it->second.useKey].count;
    if(count<=0) return {Code::Conflict,"reservation counter"};
    --count; next.orders[id].state=OrderState::Cancelled; return publish(std::move(next));
}
Result Economy::record(const std::string& metric,Amount delta,const std::string& op) {
    if(metric.empty()||metric.rfind("@meta/",0)==0||delta<0||delta>kMaxAmount) return {Code::Invalid,"metric"};
    auto body=payload("record",metric,{{"delta",delta}}); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto next=profile_; auto& n=next.progress[metric]; if(n>kMaxAmount-delta) return {Code::Limit,metric};
    n+=delta; next.receipts[op]=body; return publish(std::move(next));
}
Result Economy::recordEvent(const std::string& metric,Amount delta,const Context& c,const std::string& op) {
    if(metric.empty()||metric.rfind("@meta/",0)==0||delta<0||delta>kMaxAmount||!validTime(c.now)) return {Code::Invalid,"event"};
    if(!profile_.player.empty()&&profile_.player!=c.player) return {Code::Denied,"player"};
    // Event timestamp belongs to the immutable event, not to the retry wall clock.
    auto body=payload("event",metric,{{"delta",delta},{"time",c.now}}); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto next=profile_; std::vector<std::string> metrics{metric};
    for(auto& [id,t]:config_.tracks) if(t.metric==metric&&t.schedule.period&&t.schedule.active(c.now)) metrics.push_back(key(id,t.schedule.bucket(c.now)));
    for(auto& m:metrics) { auto& n=next.progress[m]; if(n>kMaxAmount-delta) return {Code::Limit,m}; n+=delta; }
    next.receipts[op]=body; return publish(std::move(next));
}
Result Economy::claim(const std::string& id,size_t tier,const Context& c,const std::string& op) {
    if(!profile_.player.empty()&&profile_.player!=c.player) return {Code::Denied,"player"};
    auto body=payload("claim",id+"/"+std::to_string(tier)); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto it=config_.tracks.find(id); if(it==config_.tracks.end()) return {Code::Missing,id};
    const auto& t=it->second;
    if(tier>=t.thresholds.size()||tier>=t.rewards.size()) return {Code::Invalid,"tier"};
    if(!t.schedule.active(c.now)||!t.condition.matches(c)||(!t.premiumTag.empty()&&!c.tags.count(t.premiumTag))) return {Code::Unavailable,id};
    const auto metric=t.schedule.period?key(id,t.schedule.bucket(c.now)):t.metric;
    if(value(profile_.progress,metric)<t.thresholds[tier]) return {Code::Insufficient,t.metric};
    auto claimKey=key(id,t.schedule.bucket(c.now))+"/"+std::to_string(tier);
    if(profile_.claims.count(claimKey)) return {Code::Duplicate,{}};
    auto next=profile_; r=apply(next,{},t.rewards[tier]); if(!r) return r;
    next.claims.insert(claimKey); next.receipts[op]=body; return publish(std::move(next));
}
Result Economy::claimDaily(const std::string& id,Time now,const std::string& op) {
    auto body=payload("daily",id); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto i=config_.daily.find(id); if(i==config_.daily.end()) return {Code::Missing,id};
    const auto& d=i->second;
    if(!validTime(now)||now<d.start||d.period<=0||d.rewards.empty()) return {Code::Unavailable,id};
    auto next=profile_; auto& state=next.daily[id]; auto bucket=(now-d.start)/d.period;
    if(bucket<=state.bucket) return {Code::Unavailable,"already claimed or clock rollback"};
    if(d.resetOnMiss&&state.bucket>=0&&bucket>state.bucket+1) state.streak=0;
    if(!d.loop&&uint64_t(state.streak)>=d.rewards.size()) return {Code::Limit,id};
    size_t index=size_t(state.streak)%d.rewards.size(); r=apply(next,{},d.rewards[index]); if(!r) return r;
    if(state.streak==kMaxAmount) return {Code::Limit,"daily streak"};
    ++state.streak; state.bucket=bucket; next.receipts[op]=body; return publish(std::move(next));
}
Result Economy::regenerate(const std::string& id,Time now) {
    auto it=config_.energy.find(id); if(it==config_.energy.end()) return {Code::Missing,id};
    const auto& e=it->second;
    if(!validTime(now)||e.interval<=0||e.amount<=0||e.cap<0) return {Code::Invalid,id};
    auto next=profile_; auto i=next.energy.find(id);
    if(i==next.energy.end()) { next.energy[id]=now; return publish(std::move(next)); }
    if(now<i->second) return {Code::Conflict,"clock rollback"};
    auto& balance=next.balances[e.asset]; Amount room=std::max<Amount>(0,e.cap-balance);
    if(!room) { if(i->second==now) return {}; i->second=now; return publish(std::move(next)); }
    Amount ticks=(now-i->second)/e.interval;
    if(!ticks) return {};
    Amount needed=room/e.amount+(room%e.amount!=0);
    Amount grant=ticks>=needed?room:ticks*e.amount;
    auto r=apply(next,{},{{e.asset,grant}}); if(!r) return r;
    i->second=ticks>=needed?now:i->second+ticks*e.interval;
    return publish(std::move(next));
}
Result Economy::deliver(const Mail& mail) {
    if(mail.id.empty()||mail.claimed||!validTime(mail.expires)) return {Code::Invalid,"mail"};
    auto i=profile_.mail.find(mail.id);
    if(i!=profile_.mail.end()) return i->second.reward==mail.reward&&i->second.expires==mail.expires&&i->second.metadata==mail.metadata?Result{Code::Duplicate,{}}:Result{Code::Conflict,"mail id"};
    Profile check; auto r=apply(check,{},mail.reward); if(!r) return r;
    auto next=profile_; next.mail[mail.id]=mail; return publish(std::move(next));
}
Result Economy::claimMail(const std::string& id,Time now,const std::string& op) {
    auto body=payload("mail",id); auto r=replay(op,body); if(r.code!=Code::Ok) return r;
    auto i=profile_.mail.find(id); if(i==profile_.mail.end()) return {Code::Missing,id};
    if(i->second.claimed) return {Code::Duplicate,{}};
    if(!validTime(now)||(i->second.expires&&now>=i->second.expires)) return {Code::Unavailable,id};
    auto next=profile_; r=apply(next,{},i->second.reward); if(!r) return r;
    next.mail[id].claimed=true; next.receipts[op]=body; return publish(std::move(next));
}
size_t cohort(const std::string& player,const std::string& experiment,const std::vector<uint32_t>& weights) {
    uint64_t total=0; for(auto w:weights) total+=w;
    if(!total) return weights.size();
    uint64_t h=14695981039346656037ULL;
    auto s=payload(player,experiment); for(unsigned char c:s) { h^=c; h*=1099511628211ULL; }
    auto n=h%total; for(size_t i=0;i<weights.size();++i) { if(n<weights[i]) return i; n-=weights[i]; }
    return weights.size();
}
namespace {
void merge(Json& dst,const Json& src) {
    if(!src.isObject()) { dst=src; return; }
    if(!dst.isObject()) dst=Json::object();
    for(auto& [k,v]:src.members()) { if(v.isNull()) dst.erase(k); else merge(dst.member(k),v); }
}
}
Json resolveSettings(Json base,const Context& c,std::vector<Variant> variants) {
    std::stable_sort(variants.begin(),variants.end(),[](auto& a,auto& b){return a.priority<b.priority;});
    for(auto& v:variants) if(v.condition.matches(c)) merge(base,v.patch);
    return base;
}
} // namespace aiverse::meta
