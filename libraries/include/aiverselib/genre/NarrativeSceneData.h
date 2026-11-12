#pragma once
// JSON for narrative scenes (narrative-graphs 0.2.0). Localization keys only; integer IDs; strict: unknown keys,
// wrong types, duplicate IDs, unknown characters/facts/flags/chapters/nodes/consequences, broken edges and lines
// that cannot reach an ending throw JsonError("narrative.<path>: reason"). JSON paths use array indexes
// (nodes[0].choices[1]); definition checks after parsing use IDs (nodes[id=4].choices[id=2]).
// The legacy story format (examples/libraries/genre/story.json: nodes with {key,minimum,maximum} conditions and
// {key,value,add} changes) is accepted unchanged: without a "facts" section every referenced key becomes a
// persistent int fact over the full int64 range starting at 0, and without "scenes" scene 1 enters the first node.
#include "NarrativeScenes.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace scene_json {
[[noreturn]] inline void fail(const std::string& path,const std::string& what){throw JsonError("narrative"+(path.empty()?std::string():"."+path)+": "+what);}
inline std::string idx(const std::string& path,std::size_t i){return path+"["+std::to_string(i)+"]";}
inline std::string sub(const std::string& path,const char* key){return path.empty()?std::string(key):path+"."+key;}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())fail(path,"expected object");
    for(const auto& [key,value]:j.members()){(void)value;if(std::find(names.begin(),names.end(),std::string_view(key))==names.end())fail(path,"unknown key '"+key+"'");}
}
inline int64_t integer(const Json& j,const std::string& path){
    if(!j.isNumber())fail(path,"expected integer");
    const auto& text=j.numberLiteral();
    int64_t value=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())fail(path,"invalid integer");return value;
}
inline int64_t integer(const Json& parent,const char* key,const std::string& path,int64_t fallback){return parent.has(key)?integer(parent[key],sub(path,key)):fallback;}
inline int64_t id(const Json& parent,const std::string& path,const char* key="id"){if(!parent.has(key))fail(sub(path,key),"required");auto v=integer(parent[key],sub(path,key));if(v<=0)fail(sub(path,key),"must be positive");return v;}
inline uint32_t ticks(const Json& parent,const char* key,const std::string& path,uint32_t fallback){
    auto v=integer(parent,key,path,fallback);if(v<0||v>int64_t(kSceneMaxTicks))fail(sub(path,key),"expected 0..10000000 ticks");return uint32_t(v);
}
inline bool boolean(const Json& parent,const char* key,const std::string& path,bool fallback){
    if(!parent.has(key))return fallback;
    if(parent[key].type()!=Json::Type::Bool)fail(sub(path,key),"expected bool");
    return parent[key].boolean();
}
inline std::string text(const Json& parent,const char* key,const std::string& path,bool required=true){
    if(!parent.has(key)){if(required)fail(sub(path,key),"required localization key");return {};}
    const auto& j=parent[key];if(!j.isString()||j.str().empty())fail(sub(path,key),"expected nonempty localization key");return j.str();
}
inline const std::vector<Json>& array(const Json& parent,const char* key,const std::string& path,std::size_t maximum){
    static const std::vector<Json> none;if(!parent.has(key))return none;const auto& j=parent[key];
    if(!j.isArray()||j.size()>maximum)fail(sub(path,key),"expected array of at most "+std::to_string(maximum));
    return j.items();
}
inline std::vector<int64_t> ids(const Json& parent,const char* key,const std::string& path,bool allowZero){
    std::vector<int64_t> out;const auto& list=array(parent,key,path,kSceneMaxParticipants);
    for(std::size_t i=0;i<list.size();++i){auto v=integer(list[i],idx(sub(path,key),i));if(v<0||(!allowZero&&v==0))fail(idx(sub(path,key),i),"invalid character id");out.push_back(v);}
    return out;
}
struct Reader {
    NarrativeWorld& w;bool legacy=false;
    const SceneFactDef& fact(int64_t key,const std::string& path){
        auto it=w.facts.find(key);if(it!=w.facts.end())return it->second;
        if(!legacy||key<=0)fail(path,"unknown fact "+std::to_string(key));
        SceneFactDef f;f.id=key;f.minimum=std::numeric_limits<int64_t>::min();f.maximum=std::numeric_limits<int64_t>::max();f.persistent=true;return w.facts.emplace(key,f).first->second;
    }
    int64_t factValue(const SceneFactDef& f,const Json& j,const std::string& path){
        if(j.type()==Json::Type::Bool){if(f.type!=SceneFactType::Bool)fail(path,"bool value for a non-bool fact");return j.boolean()?1:0;}
        if(j.isString()){
            if(f.type!=SceneFactType::Enum)fail(path,"named value for a non-enum fact");
            auto it=std::find(f.values.begin(),f.values.end(),j.str());
            if(it==f.values.end())fail(path,"unknown enum value '"+j.str()+"'");
            return int64_t(it-f.values.begin());
        }
        auto v=integer(j,path);if(v<f.minimum||v>f.maximum)fail(path,"outside the fact's domain");return v;
    }
    static SocialMeter meter(const Json& j,const std::string& path){
        if(j.isString()){if(j.str()=="affinity")return SocialMeter::Affinity;if(j.str()=="trust")return SocialMeter::Trust;if(j.str()=="fear")return SocialMeter::Fear;}
        fail(path,"expected affinity, trust or fear");
    }
    static SceneCompare op(const Json& parent,const std::string& path,SceneCompare fallback){
        if(!parent.has("op"))return fallback;
        const auto& j=parent["op"];
        static constexpr std::pair<std::string_view,SceneCompare> ops[]{{"eq",SceneCompare::Eq},{"ne",SceneCompare::Ne},{"lt",SceneCompare::Lt},{"le",SceneCompare::Le},
            {"gt",SceneCompare::Gt},{"ge",SceneCompare::Ge},{"between",SceneCompare::Between}};
        if(j.isString())for(auto [name,value]:ops)if(name==j.str())return value;
        fail(sub(path,"op"),"expected eq, ne, lt, le, gt, ge or between");
    }
    // value / min+max for a comparison; `read` parses one operand.
    template<class Read> void operands(const Json& j,const std::string& path,SceneCondition& c,std::optional<int64_t> fallback,Read read){
        if(c.op==SceneCompare::Between){
            if(j.has("value")||!j.has("min")||!j.has("max"))fail(path,"between needs min and max");
            c.value=read(j["min"],sub(path,"min"));
            c.value2=read(j["max"],sub(path,"max"));
            if(c.value>c.value2)fail(path,"min exceeds max");
            return;
        }
        if(j.has("min")||j.has("max"))fail(path,"min/max need op between");
        if(j.has("value"))c.value=read(j["value"],sub(path,"value"));else if(fallback)c.value=*fallback;else fail(sub(path,"value"),"required");
    }
    SceneCondition condition(const Json& j,const std::string& path,std::size_t depth){
        using K=SceneConditionKind;if(!j.isObject())fail(path,"expected condition object");if(depth>kSceneMaxDepth)fail(path,"nested deeper than 16");
        static constexpr std::string_view subjects[]{"all","any","not","fact","meter","knows","flag","visited","chapter","act","chose","key"};
        std::string_view subject;int found=0;for(auto s:subjects)if(j.has(std::string(s))){subject=s;++found;}
        if(found!=1)fail(path,"expected exactly one of all, any, not, fact, meter, knows, flag, visited, chapter, act, chose");
        SceneCondition c;auto plain=[&](const Json& v,const std::string& p){return integer(v,p);};
        if(subject=="all"||subject=="any"){
            keys(j,path,{subject});c.kind=subject=="all"?K::All:K::Any;const auto& list=array(j,std::string(subject).c_str(),path,kSceneMaxTerms);
            if(list.empty())fail(sub(path,std::string(subject).c_str()),"expected at least one term");
            for(std::size_t i=0;i<list.size();++i)c.children.push_back(condition(list[i],idx(sub(path,std::string(subject).c_str()),i),depth+1));
        }else if(subject=="not"){keys(j,path,{"not"});c.kind=K::Not;c.children.push_back(condition(j["not"],sub(path,"not"),depth+1));}
        else if(subject=="key"){
            keys(j,path,{"key","minimum","maximum"});c.kind=K::Fact;c.op=SceneCompare::Between;c.subject=integer(j["key"],sub(path,"key"));fact(c.subject,sub(path,"key"));
            c.value=integer(j["minimum"],sub(path,"minimum"));c.value2=integer(j["maximum"],sub(path,"maximum"));if(c.value>c.value2)fail(path,"minimum exceeds maximum");
        }else if(subject=="fact"){
            keys(j,path,{"fact","op","value","min","max"});c.kind=K::Fact;c.subject=integer(j["fact"],sub(path,"fact"));const auto& f=fact(c.subject,sub(path,"fact"));c.op=op(j,path,SceneCompare::Eq);
            if(f.type!=SceneFactType::Int&&c.op!=SceneCompare::Eq&&c.op!=SceneCompare::Ne)fail(sub(path,"op"),"bool and enum facts allow eq/ne only");
            operands(j,path,c,f.type==SceneFactType::Bool?std::optional<int64_t>(1):std::nullopt,[&](const Json& v,const std::string& p){return factValue(f,v,p);});
        }else if(subject=="meter"){
            keys(j,path,{"meter","character","op","value","min","max"});c.kind=K::Meter;c.meter=meter(j["meter"],sub(path,"meter"));c.character=id(j,path,"character");
            c.op=op(j,path,SceneCompare::Ge);operands(j,path,c,std::nullopt,plain);
        }else if(subject=="knows"){keys(j,path,{"knows","character"});c.kind=K::Knows;c.subject=id(j,path,"knows");c.character=integer(j,"character",path,0);}
        else if(subject=="flag"){keys(j,path,{"flag"});c.kind=K::Flag;c.subject=id(j,path,"flag");}
        else if(subject=="visited"){keys(j,path,{"visited","op","value","min","max"});c.kind=K::Visited;c.subject=id(j,path,"visited");c.op=op(j,path,SceneCompare::Ge);operands(j,path,c,1,plain);}
        else if(subject=="chapter"||subject=="act"){
            keys(j,path,{subject,"op"});c.kind=subject=="chapter"?K::Chapter:K::Act;c.value=id(j,path,std::string(subject).c_str());c.op=op(j,path,SceneCompare::Eq);
            if(c.op==SceneCompare::Between)fail(sub(path,"op"),"between is not supported here");
        }else{keys(j,path,{"chose","node"});c.kind=K::Chose;c.value=id(j,path,"chose");c.subject=id(j,path,"node");}
        return c;
    }
    std::vector<SceneCondition> conditions(const Json& parent,const std::string& path){
        std::vector<SceneCondition> out;const auto& list=array(parent,"conditions",path,kSceneMaxTerms);
        for(std::size_t i=0;i<list.size();++i)out.push_back(condition(list[i],idx(sub(path,"conditions"),i),1));
        return out;
    }
    SceneEffect effect(const Json& j,const std::string& path){
        using K=SceneEffectKind;if(!j.isObject())fail(path,"expected effect object");SceneEffect e;
        if(j.has("meter")){
            keys(j,path,{"meter","character","add","set"});e.meter=meter(j["meter"],sub(path,"meter"));e.character=id(j,path,"character");
            if(j.has("add")==j.has("set"))fail(path,"expected exactly one of add, set");
            e.kind=j.has("add")?K::AddMeter:K::SetMeter;
            e.value=integer(j[j.has("add")?"add":"set"],sub(path,j.has("add")?"add":"set"));return e;
        }
        if(j.has("key")){
            keys(j,path,{"key","value","add"});e.subject=integer(j["key"],sub(path,"key"));const auto& f=fact(e.subject,sub(path,"key"));
            e.kind=boolean(j,"add",path,false)?K::AddFact:K::SetFact;e.value=e.kind==K::AddFact?integer(j["value"],sub(path,"value")):factValue(f,j["value"],sub(path,"value"));return e;
        }
        static constexpr std::string_view subjects[]{"set","add","reveal","forget","flag","chapter","schedule","cancel"};
        std::string_view subject;int found=0;for(auto s:subjects)if(j.has(std::string(s))){subject=s;++found;}
        if(found!=1)fail(path,"expected exactly one of set, add, meter, reveal, forget, flag, chapter, schedule, cancel");
        auto name=std::string(subject);
        if(subject=="set"||subject=="add"){
            keys(j,path,{subject,"value"});e.kind=subject=="set"?K::SetFact:K::AddFact;e.subject=integer(j[name],sub(path,name.c_str()));const auto& f=fact(e.subject,sub(path,name.c_str()));
            if(!j.has("value"))fail(sub(path,"value"),"required");
            if(e.kind==K::AddFact){if(f.type!=SceneFactType::Int)fail(sub(path,"add"),"only int facts can be added to");e.value=integer(j["value"],sub(path,"value"));}
            else e.value=factValue(f,j["value"],sub(path,"value"));
        }else if(subject=="reveal"||subject=="forget"){
            const char* who=subject=="reveal"?"to":"from";keys(j,path,{subject,who});e.kind=subject=="reveal"?K::Reveal:K::Forget;e.subject=id(j,path,name.c_str());
            fact(e.subject,sub(path,name.c_str()));if(!j.has(who))fail(sub(path,who),"required list of characters (0 = player)");e.characters=ids(j,who,path,true);
        }else if(subject=="flag"){keys(j,path,{"flag"});e.kind=K::SetFlag;e.subject=id(j,path,"flag");}
        else if(subject=="chapter"){keys(j,path,{"chapter"});e.kind=K::SetChapter;e.subject=id(j,path,"chapter");}
        else if(subject=="schedule"){
            keys(j,path,{"schedule","afterTicks","afterScenes"});e.kind=K::Schedule;e.subject=id(j,path,"schedule");
            if(j.has("afterTicks")==j.has("afterScenes"))fail(path,"expected exactly one of afterTicks, afterScenes");
            e.byScene=j.has("afterScenes");e.delay=ticks(j,e.byScene?"afterScenes":"afterTicks",path,0);if(!e.delay)fail(sub(path,e.byScene?"afterScenes":"afterTicks"),"must be at least 1");
        }else{keys(j,path,{"cancel"});e.kind=K::Cancel;e.subject=id(j,path,"cancel");}
        return e;
    }
    std::vector<SceneEffect> effects(const Json& parent,const char* key,const std::string& path){
        std::vector<SceneEffect> out;const auto& list=array(parent,key,path,kSceneMaxList);
        for(std::size_t i=0;i<list.size();++i)out.push_back(effect(list[i],idx(sub(path,key),i)));
        return out;
    }
    static std::vector<SceneCue> cues(const Json& parent,const std::string& path){
        std::vector<SceneCue> out;const auto& list=array(parent,"cues",path,kNarrativeSceneMaxCues);
        for(std::size_t i=0;i<list.size();++i){
            auto p=idx(sub(path,"cues"),i);SceneCue c;
            if(list[i].isObject()){keys(list[i],p,{"id","delay"});c.id=id(list[i],p);c.delay=ticks(list[i],"delay",p,0);}
            else{c.id=integer(list[i],p);if(c.id<=0)fail(p,"cue id must be positive");}
            out.push_back(c);
        }return out;
    }
};
template<class Map,class Value> void insertUnique(Map& map,Value value,const std::string& path,const char* what){
    auto key=value.id;if(!map.emplace(key,std::move(value)).second)fail(sub(path,"id"),std::string("duplicate ")+what+" "+std::to_string(key));
}
}
inline NarrativeWorld readNarrativeWorld(const Json& j,const NarrativeConfig& defaults={}){
    using namespace scene_json;
    keys(j,"",{"facts","characters","flags","chapters","initialChapter","scenes","nodes","consequences","barks"});
    NarrativeWorld w;Reader r{w,!j.has("facts")};
    const auto& facts=array(j,"facts","",kSceneMaxFacts);
    for(std::size_t i=0;i<facts.size();++i){
        const auto& x=facts[i];auto p=idx("facts",i);keys(x,p,{"id","type","min","max","initial","persistent","values"});SceneFactDef f;f.id=id(x,p);
        auto type=x.has("type")?x["type"].str():std::string("int");f.persistent=boolean(x,"persistent",p,false);
        if(type=="int"){
            if(x.has("values"))fail(sub(p,"values"),"only enum facts have values");
            f.minimum=integer(x,"min",p,0);f.maximum=integer(x,"max",p,1000000000);if(f.minimum>f.maximum)fail(p,"min exceeds max");
            f.initial=integer(x,"initial",p,f.minimum<=0&&f.maximum>=0?0:f.minimum);if(f.initial<f.minimum||f.initial>f.maximum)fail(sub(p,"initial"),"outside min..max");
        }else if(type=="bool"){
            if(x.has("min")||x.has("max")||x.has("values"))fail(p,"bool facts have no min/max/values");
            f.type=SceneFactType::Bool;
            f.maximum=1;
            f.initial=boolean(x,"initial",p,false);
        }else if(type=="enum"){
            if(x.has("min")||x.has("max"))fail(p,"enum facts have no min/max");
            f.type=SceneFactType::Enum;
            const auto& values=array(x,"values",p,kSceneMaxEnumValues);
            if(values.empty())fail(sub(p,"values"),"expected 1..256 names");
            for(std::size_t k=0;k<values.size();++k){
                if(!values[k].isString()||values[k].str().empty())fail(idx(sub(p,"values"),k),"expected nonempty name");
                if(std::find(f.values.begin(),f.values.end(),values[k].str())!=f.values.end())fail(idx(sub(p,"values"),k),"duplicate value '"+values[k].str()+"'");
                f.values.push_back(values[k].str());
            }
            f.maximum=int64_t(f.values.size())-1;f.initial=x.has("initial")?r.factValue(f,x["initial"],sub(p,"initial")):0;
        }else fail(sub(p,"type"),"expected int, bool or enum");
        insertUnique(w.facts,std::move(f),p,"fact");
    }
    const auto& characters=array(j,"characters","",kSceneMaxCharacters);
    for(std::size_t i=0;i<characters.size();++i){
        const auto& x=characters[i];auto p=idx("characters",i);keys(x,p,{"id","nameKey","meters"});SceneCharacter c;c.id=id(x,p);c.nameKey=text(x,"nameKey",p);
        c.meters.fill(defaults.meter);
        if(x.has("meters")){
            keys(x["meters"],sub(p,"meters"),{"affinity","trust","fear"});
            for(auto [name,m]:{std::pair{"affinity",SocialMeter::Affinity},std::pair{"trust",SocialMeter::Trust},std::pair{"fear",SocialMeter::Fear}}){
                if(!x["meters"].has(name))continue;
                auto q=sub(sub(p,"meters"),name);
                const auto& raw=x["meters"][name];
                keys(raw,q,{"min","max","initial"});
                auto& range=c.meters[uint8_t(m)];
                range.minimum=integer(raw,"min",q,range.minimum);range.maximum=integer(raw,"max",q,range.maximum);range.initial=integer(raw,"initial",q,std::clamp(range.initial,range.minimum,std::max(range.minimum,range.maximum)));
                if(!meterRangeOk(range))fail(q,"requires min <= initial <= max within +-1000000000");
            }
        }
        insertUnique(w.characters,std::move(c),p,"character");
    }
    const auto& flags=array(j,"flags","",kSceneMaxFlags);
    for(std::size_t i=0;i<flags.size();++i){auto p=idx("flags",i);keys(flags[i],p,{"id","persistent"});insertUnique(w.flags,SceneFlagDef{id(flags[i],p),boolean(flags[i],"persistent",p,true)},p,"flag");}
    const auto& chapters=array(j,"chapters","",kSceneMaxChapters);
    for(std::size_t i=0;i<chapters.size();++i){
        const auto& x=chapters[i];auto p=idx("chapters",i);keys(x,p,{"id","act","titleKey"});SceneChapter c{id(x,p),integer(x,"act",p,1),text(x,"titleKey",p)};
        if(c.act<1||c.act>1000000)fail(sub(p,"act"),"must be 1..1000000");
        insertUnique(w.chapters,std::move(c),p,"chapter");
    }
    if(j.has("initialChapter"))w.initialChapter=id(j,"","initialChapter");
    if(!j.has("nodes")&&!j.has("barks"))fail("nodes","required");
    const auto& nodes=array(j,"nodes","",kSceneMaxNodes);int64_t first=0;
    for(std::size_t i=0;i<nodes.size();++i){
        const auto& x=nodes[i];auto p=idx("nodes",i);keys(x,p,{"id","textKey","speaker","participants","cues","onEnter","choices","next","autoTicks"});
        SceneNode n;n.id=id(x,p);n.textKey=text(x,"textKey",p);n.speaker=integer(x,"speaker",p,0);if(n.speaker<0)fail(sub(p,"speaker"),"invalid character id");
        n.participants=ids(x,"participants",p,false);n.cues=Reader::cues(x,p);n.onEnter=r.effects(x,"onEnter",p);n.autoTicks=ticks(x,"autoTicks",p,0);
        if(x.has("choices")==x.has("next"))fail(p,"expected exactly one of choices (a choice line) or next (a linear line; 0 ends the scene)");
        if(x.has("next")){n.linear=true;n.next=integer(x["next"],sub(p,"next"));if(n.next<0)fail(sub(p,"next"),"must be a node id or 0");}
        else{
            const auto& choices=array(x,"choices",p,kSceneMaxChoices);if(choices.empty())fail(sub(p,"choices"),"expected 1..128 choices");
            for(std::size_t k=0;k<choices.size();++k){
                const auto& y=choices[k];auto q=idx(sub(p,"choices"),k);keys(y,q,{"id","textKey","next","once","conditions","effects","changes","cues"});
                SceneChoice c;c.id=id(y,q);c.textKey=text(y,"textKey",q);if(!y.has("next"))fail(sub(q,"next"),"required (0 ends the scene)");
                c.next=integer(y["next"],sub(q,"next"));if(c.next<0)fail(sub(q,"next"),"must be a node id or 0");c.once=boolean(y,"once",q,false);
                c.conditions=r.conditions(y,q);c.effects=r.effects(y,"effects",q);for(auto& e:r.effects(y,"changes",q))c.effects.push_back(std::move(e));c.cues=Reader::cues(y,q);
                for(const auto& other:n.choices)if(other.id==c.id)fail(sub(q,"id"),"duplicate choice "+std::to_string(c.id));
                n.choices.push_back(std::move(c));
            }
        }
        if(!first)first=n.id;
        insertUnique(w.nodes,std::move(n),p,"node");
    }
    const auto& scenes=array(j,"scenes","",kNarrativeMaxScenes);
    for(std::size_t i=0;i<scenes.size();++i){
        const auto& x=scenes[i];auto p=idx("scenes",i);keys(x,p,{"id","entry","titleKey","chapter"});
        insertUnique(w.scenes,NarrativeSceneDef{id(x,p),id(x,p,"entry"),integer(x,"chapter",p,0),text(x,"titleKey",p,false)},p,"scene");
    }
    if(!j.has("scenes")&&first)w.scenes.emplace(1,NarrativeSceneDef{1,first,0,{}});
    const auto& consequences=array(j,"consequences","",kSceneMaxConsequences);
    for(std::size_t i=0;i<consequences.size();++i){
        const auto& x=consequences[i];auto p=idx("consequences",i);keys(x,p,{"id","conditions","effects","cues"});
        insertUnique(w.consequences,SceneConsequence{id(x,p),r.conditions(x,p),r.effects(x,"effects",p),Reader::cues(x,p)},p,"consequence");
    }
    const auto& barks=array(j,"barks","",kSceneMaxBarks);
    for(std::size_t i=0;i<barks.size();++i){
        const auto& x=barks[i];auto p=idx("barks",i);keys(x,p,{"id","trigger","speaker","textKey","priority","cooldown","once","conditions","cues"});
        SceneBark b;b.id=id(x,p);b.trigger=id(x,p,"trigger");b.speaker=integer(x,"speaker",p,0);b.textKey=text(x,"textKey",p);b.priority=integer(x,"priority",p,0);
        b.cooldown=x.has("cooldown")?int64_t(ticks(x,"cooldown",p,0)):-1;b.once=boolean(x,"once",p,false);b.conditions=r.conditions(x,p);b.cues=Reader::cues(x,p);
        insertUnique(w.barks,std::move(b),p,"bark");
    }
    if(auto e=narrativeWorldError(w);!e.empty())throw JsonError("narrative."+e);
    return w;
}
}
