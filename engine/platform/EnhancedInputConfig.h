// Enhanced Input assets (Unreal Enhanced Input-inspired, not UE asset-format compatible).
// Optional presentation-side JSON config and user rebind profile. Never include from sim logic.
#pragma once
#include "Platform.h"
#include "Gamepad.h"
#include "input/InputFlowCore.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::platform::enhanced_config {
namespace ei = aiverse::lib::gameplay::inputflow;

struct ContextConfig {
    ei::MappingContext context;
    int priority = 0;
    bool active = true;
};
struct Asset {
    std::vector<ei::Action> actions;
    std::vector<ContextConfig> contexts;
};
struct Rebind {
    std::string context;
    std::uint64_t mappingId = 0;
    ei::Control control;
};
struct Profile {
    std::vector<Rebind> rebindings;
};

namespace detail {
inline bool fail(std::string* error, const std::string& path, const std::string& detail) {
    if (error) *error = path + ": " + detail;
    return false;
}
inline bool fields(const Json& object, std::initializer_list<std::string_view> allowed,
                   const std::string& path, std::string* error) {
    if (!object.isObject()) return fail(error,path,"expected object");
    for (const auto& [key,value] : object.members()) {
        (void)value;
        if (std::find(allowed.begin(),allowed.end(),std::string_view(key)) == allowed.end())
            return fail(error,path+"."+key,"unknown field");
    }
    return true;
}
inline bool text(const Json& value, std::string& out,
                 const std::string& path, std::string* error) {
    if (!value.isString() || value.str().empty() || value.str().size()>128)
        return fail(error,path,"expected nonempty string (max 128 chars)");
    out=value.str();return true;
}
inline bool integer(const Json& value,std::int64_t& out,std::int64_t min,std::int64_t max,
                    const std::string& path,std::string* error) {
    if (!value.isNumber()) return fail(error,path,"expected integer");
    const std::int64_t n=value.integer(std::numeric_limits<std::int64_t>::min());
    if (n<min||n>max) return fail(error,path,"out of range integer");
    out=n;return true;
}
inline bool number(const Json& value,float& out,float min,float max,
                   const std::string& path,std::string* error) {
    if (!value.isNumber()) return fail(error,path,"expected number");
    const double d=value.toDouble();
    if (!std::isfinite(d)||d<min||d>max) return fail(error,path,"number out of range");
    out=float(d);return true;
}
inline bool flag(const Json& object,const char* field,bool& out,
                 const std::string& path,std::string* error) {
    if (!object.has(field)) return true;
    if (object[field].type()!=Json::Type::Bool) return fail(error,path+"."+field,"expected boolean");
    out=object[field].boolean();
    return true;
}
inline const std::array<const char*,static_cast<std::size_t>(Key::Count)> kKeyNames={{
    "Unknown","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T",
    "U","V","W","X","Y","Z","Num0","Num1","Num2","Num3","Num4","Num5","Num6","Num7","Num8","Num9",
    "Escape","Enter","Tab","Backspace","Delete","Insert","Space","Left","Right","Up","Down","Home",
    "End","PageUp","PageDown","LeftShift","RightShift","LeftCtrl","RightCtrl","LeftAlt","RightAlt",
    "LeftSuper","RightSuper","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12",
    "Minus","Equal","LeftBracket","RightBracket","Semicolon","Apostrophe","Comma","Period","Slash",
    "Backslash","GraveAccent"}};
inline const std::array<const char*,static_cast<std::size_t>(PadButton::Count)> kPadButtons={{
    "A","B","X","Y","LB","RB","Back","Start","LStick","RStick","Up","Down",
    "Left","Right","Guide"}};
inline const std::array<const char*,static_cast<std::size_t>(PadAxis::Count)> kPadAxes={{
    "LX","LY","RX","RY","LT","RT"}};
inline std::string deviceName(ei::Device d) {
    switch(d) {
    case ei::Device::Keyboard:return "keyboard";
    case ei::Device::MouseButton:return "mouseButton";
    case ei::Device::MouseAxis:return "mouseAxis";
    case ei::Device::MouseWheel:return "mouseWheel";
    case ei::Device::GamepadButton:return "gamepadButton";
    case ei::Device::GamepadAxis:return "gamepadAxis";
    case ei::Device::Touch:return "touch";
    }
    return "";
}
inline bool deviceFromName(const std::string& name,ei::Device& out) {
    for (ei::Device device : {ei::Device::Keyboard,ei::Device::MouseButton,ei::Device::MouseAxis,
                               ei::Device::MouseWheel,ei::Device::GamepadButton,
                               ei::Device::GamepadAxis,ei::Device::Touch})
        if (deviceName(device)==name) {out=device;return true;}
    return false;
}
template<std::size_t N>
inline bool findName(const std::string& name,const std::array<const char*,N>& names,int& out) {
    for (std::size_t i=0;i<N;++i)
        if (name==names[i]) {out=int(i);return true;}
    return false;
}
inline bool readControl(const Json& value,ei::Control& out,
                        const std::string& path,std::string* error) {
    if (!fields(value,{"device","code","deviceIndex"},path,error))return false;
    if (!value["device"].isString()||!deviceFromName(value["device"].str(),out.device))
        return fail(error,path+".device","unknown input device");
    int code=-1;
    if (value["code"].isString()) {
        const auto name=value["code"].str();
        switch(out.device) {
        case ei::Device::Keyboard:
            if(!findName(name,kKeyNames,code)||code==0)return fail(error,path+".code","unknown key");
            break;
        case ei::Device::MouseButton:
            if(name=="left")code=0;else if(name=="right")code=1;
            else if(name=="middle")code=2;else if(name=="x1")code=3;else if(name=="x2")code=4;
            else return fail(error,path+".code","unknown mouse button");
            break;
        case ei::Device::MouseAxis:case ei::Device::MouseWheel:
            if(name=="x")code=0;else if(name=="y")code=1;
            else return fail(error,path+".code","axis must be x or y");
            break;
        case ei::Device::GamepadButton:
            if(!findName(name,kPadButtons,code))return fail(error,path+".code","unknown pad button");
            break;
        case ei::Device::GamepadAxis:
            if(!findName(name,kPadAxes,code))return fail(error,path+".code","unknown pad axis");
            break;
        case ei::Device::Touch:
            return fail(error,path+".code","touch code must be integer");
        }
    } else {
        std::int64_t parsed=0;
        if(!integer(value["code"],parsed,0,4096,path+".code",error))return false;
        code=int(parsed);
    }
    int maxCode=0;
    switch(out.device) {
    case ei::Device::Keyboard:maxCode=int(Key::Count)-1;break;
    case ei::Device::MouseButton:maxCode=4;break;
    case ei::Device::MouseAxis:case ei::Device::MouseWheel:maxCode=1;break;
    case ei::Device::GamepadButton:maxCode=int(PadButton::Count)-1;break;
    case ei::Device::GamepadAxis:maxCode=int(PadAxis::Count)-1;break;
    case ei::Device::Touch:maxCode=16;break;
    }
    if(code>maxCode || (out.device==ei::Device::Keyboard&&code==0))
        return fail(error,path+".code","control code out of range");
    out.code=code;
    std::int64_t index=0;
    if(value.has("deviceIndex")&&!integer(value["deviceIndex"],index,0,31,path+".deviceIndex",error))
        return false;
    const int indexLimit=out.device==ei::Device::Touch?31:
        (out.device==ei::Device::GamepadButton||out.device==ei::Device::GamepadAxis?3:0);
    if(index>indexLimit) return fail(error,path+".deviceIndex","invalid device index");
    out.deviceIndex=int(index);
    return true;
}
inline Json writeControl(const ei::Control& control) {
    Json j=Json::object();
    j.set("device",Json::string(deviceName(control.device)));
    std::string name;
    switch(control.device) {
    case ei::Device::Keyboard:
        if(control.code>=0&&control.code<int(Key::Count))name=kKeyNames[std::size_t(control.code)];
        break;
    case ei::Device::MouseButton: {
        static constexpr const char* labels[]={"left","right","middle","x1","x2"};
        if(control.code>=0&&control.code<5)name=labels[control.code];
        break;
    }
    case ei::Device::MouseAxis:case ei::Device::MouseWheel:
        if(control.code==0)name="x";else if(control.code==1)name="y";
        break;
    case ei::Device::GamepadButton:
        if(control.code>=0&&control.code<int(PadButton::Count))name=kPadButtons[std::size_t(control.code)];
        break;
    case ei::Device::GamepadAxis:
        if(control.code>=0&&control.code<int(PadAxis::Count))name=kPadAxes[std::size_t(control.code)];
        break;
    case ei::Device::Touch:break;
    }
    if(name.empty())j.set("code",Json::number(std::int64_t(control.code)));
    else j.set("code",Json::string(name));
    if(control.deviceIndex)j.set("deviceIndex",Json::number(std::int64_t(control.deviceIndex)));
    return j;
}

inline bool readModifiers(const Json& value,std::vector<ei::Modifier>& out,
                         const std::string& path,std::string* error) {
    if(value.isNull())return true;
    if(!value.isArray()||value.size()>16)return fail(error,path,"expected array (max 16)");
    for(std::size_t i=0;i<value.size();++i) {
        const Json& item=value[i];const std::string at=path+"["+std::to_string(i)+"]";
        if(!fields(item,{"type","x","y","z","threshold","order"},at,error))return false;
        const std::string kind=item.getStr("type");
        ei::Modifier modifier;
        if(kind=="scale"){
            float x=1,y=1,z=1;
            if(item.has("x")&&!number(item["x"],x,-10000,10000,at+".x",error))return false;
            if(item.has("y")&&!number(item["y"],y,-10000,10000,at+".y",error))return false;
            if(item.has("z")&&!number(item["z"],z,-10000,10000,at+".z",error))return false;
            modifier=ei::Modifier::scale(x,y,z);
        }else if(kind=="negate")modifier=ei::Modifier::negate();
        else if(kind=="deadzone"){
            float threshold=0.1f;
            if(item.has("threshold")&&!number(item["threshold"],threshold,0,0.999,at+".threshold",error))return false;
            modifier=ei::Modifier::deadZone(threshold);
        }else if(kind=="swizzle"){
            const Json& order=item["order"];
            if(!order.isArray()||order.size()!=3)return fail(error,at+".order","expected three indices");
            for(int n=0;n<3;++n){
                std::int64_t axis=0;
                if(!integer(order[std::size_t(n)],axis,0,2,at+".order["+std::to_string(n)+"]",error))return false;
                modifier.swizzle[std::size_t(n)]=int(axis);
            }
            modifier.kind=ei::ModifierKind::Swizzle;
        }else if(kind=="normalize")modifier=ei::Modifier::normalize();
        else return fail(error,at+".type","unknown modifier");
        out.push_back(std::move(modifier));
    }
    return true;
}
inline Json writeModifiers(std::span<const ei::Modifier> modifiers) {
    Json result=Json::array();
    for(const auto& m:modifiers){
        Json item=Json::object();
        switch(m.kind) {
        case ei::ModifierKind::Scale:
            item.set("type",Json::string("scale"));
            item.set("x",Json::number(double(m.scalar.x)));
            item.set("y",Json::number(double(m.scalar.y)));
            item.set("z",Json::number(double(m.scalar.z)));
            break;
        case ei::ModifierKind::Negate:item.set("type",Json::string("negate"));break;
        case ei::ModifierKind::DeadZone:
            item.set("type",Json::string("deadzone"));
            item.set("threshold",Json::number(double(m.threshold)));break;
        case ei::ModifierKind::Swizzle: {
            item.set("type",Json::string("swizzle"));
            Json order=Json::array();
            for(int idx:m.swizzle)order.push(Json::number(std::int64_t(idx)));
            item.set("order",std::move(order));break;
        }
        case ei::ModifierKind::Normalize:item.set("type",Json::string("normalize"));break;
        }
        result.push(std::move(item));
    }
    return result;
}
inline bool readTriggers(const Json& value,std::vector<ei::Trigger>& out,
                         const std::string& path,std::string* error) {
    if(value.isNull())return true;
    if(!value.isArray()||value.size()>16)return fail(error,path,"expected array (max 16)");
    for(std::size_t i=0;i<value.size();++i){
        const Json& j=value[i];const auto at=path+"["+std::to_string(i)+"]";
        if(!fields(j,{"type","seconds","action"},at,error))return false;
        const auto kind=j.getStr("type");
        ei::Trigger t;
        if(kind=="down")t=ei::Trigger::down();
        else if(kind=="pressed")t=ei::Trigger::pressed();
        else if(kind=="released")t=ei::Trigger::released();
        else if(kind=="hold"||kind=="tap"||kind=="doubleTap"||
                kind=="holdAndRelease"||kind=="pulse"){
            float seconds=0;
            if(!number(j["seconds"],seconds,kind=="pulse"?0.01f:0.0f,
                       120,at+".seconds",error))return false;
            if(kind=="hold")t=ei::Trigger::hold(seconds);
            else if(kind=="tap")t=ei::Trigger::tap(seconds);
            else if(kind=="doubleTap")t=ei::Trigger::doubleTap(seconds);
            else if(kind=="holdAndRelease")t=ei::Trigger::holdAndRelease(seconds);
            else t=ei::Trigger::pulse(seconds);
        }else if(kind=="chord"||kind=="notChord"){
            std::string action;
            if(!text(j["action"],action,at+".action",error))return false;
            t=kind=="chord"?ei::Trigger::chord(std::move(action)):ei::Trigger::notChord(std::move(action));
        }else return fail(error,at+".type","unknown trigger");
        out.push_back(std::move(t));
    }
    return true;
}
inline Json writeTriggers(std::span<const ei::Trigger> triggers) {
    Json result=Json::array();
    for(const auto& trigger:triggers){
        Json item=Json::object();
        std::string type;
        switch(trigger.kind) {
        case ei::TriggerKind::Down:type="down";break;
        case ei::TriggerKind::Pressed:type="pressed";break;
        case ei::TriggerKind::Released:type="released";break;
        case ei::TriggerKind::Hold:type="hold";break;
        case ei::TriggerKind::Tap:type="tap";break;
        case ei::TriggerKind::Chord:type="chord";break;
        case ei::TriggerKind::NotChord:type="notChord";break;
        case ei::TriggerKind::DoubleTap:type="doubleTap";break;
        case ei::TriggerKind::HoldAndRelease:type="holdAndRelease";break;
        case ei::TriggerKind::Pulse:type="pulse";break;
        }
        item.set("type",Json::string(type));
        if(trigger.kind==ei::TriggerKind::Hold||trigger.kind==ei::TriggerKind::Tap||
           trigger.kind==ei::TriggerKind::DoubleTap||
           trigger.kind==ei::TriggerKind::HoldAndRelease||trigger.kind==ei::TriggerKind::Pulse)
            item.set("seconds",Json::number(double(trigger.seconds)));
        if(trigger.kind==ei::TriggerKind::Chord||trigger.kind==ei::TriggerKind::NotChord)
            item.set("action",Json::string(trigger.chordAction));
        result.push(std::move(item));
    }
    return result;
}
inline std::string typeName(ei::ValueType t) {
    switch(t) {
    case ei::ValueType::Boolean:return "bool";
    case ei::ValueType::Axis1D:return "axis1D";
    case ei::ValueType::Axis2D:return "axis2D";
    case ei::ValueType::Axis3D:return "axis3D";
    }
    return "";
}
inline bool readType(const std::string& value,ei::ValueType& type) {
    for(const auto v:{ei::ValueType::Boolean,ei::ValueType::Axis1D,
                      ei::ValueType::Axis2D,ei::ValueType::Axis3D})
        if(typeName(v)==value){type=v;return true;}
    return false;
}
inline bool checkChords(const std::vector<ei::Trigger>& triggers,
                       const std::set<std::string>& names,const std::string& ownName,
                       const std::string& at,std::string* error) {
    for(const auto& t:triggers)
        if((t.kind==ei::TriggerKind::Chord||t.kind==ei::TriggerKind::NotChord)&&
           (t.chordAction==ownName||!names.contains(t.chordAction)))
            return fail(error,at,"chord references missing or same action: "+t.chordAction);
    return true;
}
} // namespace detail

inline bool readAsset(const Json& json,Asset& out,std::string* error=nullptr) {
    using namespace detail;
    if(!fields(json,{"version","actions","contexts"},"input",error))return false;
    std::int64_t version=0;
    if(!integer(json["version"],version,1,1,"input.version",error))return false;
    if(!json["actions"].isArray()||json["actions"].size()>512)
        return fail(error,"input.actions","expected array (max 512)");
    if(!json["contexts"].isArray()||json["contexts"].size()>64)
        return fail(error,"input.contexts","expected array (max 64)");
    Asset result;
    std::set<std::string> names,contexts;
    for(std::size_t i=0;i<json["actions"].size();++i) {
        const Json& j=json["actions"][i];const auto at="input.actions["+std::to_string(i)+"]";
        if(!fields(j,{"name","type","modifiers","triggers"},at,error))return false;
        ei::Action action;
        if(!text(j["name"],action.name,at+".name",error))return false;
        if(!names.insert(action.name).second)return fail(error,at+".name","duplicate action");
        if(!readType(j.getStr("type"),action.type))return fail(error,at+".type","unknown value type");
        if(!readModifiers(j["modifiers"],action.modifiers,at+".modifiers",error))return false;
        if(!readTriggers(j["triggers"],action.triggers,at+".triggers",error))return false;
        result.actions.push_back(std::move(action));
    }
    std::size_t totalMappings=0;
    for(std::size_t i=0;i<json["contexts"].size();++i) {
        const Json& j=json["contexts"][i];const auto at="input.contexts["+std::to_string(i)+"]";
        if(!fields(j,{"name","priority","active","mappings"},at,error))return false;
        ContextConfig config;
        if(!text(j["name"],config.context.name,at+".name",error))return false;
        if(!contexts.insert(config.context.name).second)return fail(error,at+".name","duplicate context");
        std::int64_t priority=0;
        if(j.has("priority")&&!integer(j["priority"],priority,-10000,10000,at+".priority",error))return false;
        config.priority=int(priority);
        if(!flag(j,"active",config.active,at,error))return false;
        const Json& mappings=j["mappings"];
        if(!mappings.isArray()||mappings.size()>2048)
            return fail(error,at+".mappings","expected array (max 2048)");
        totalMappings+=mappings.size();
        if(totalMappings>8192)return fail(error,"input.contexts","too many mappings (max 8192)");
        std::set<std::uint64_t> ids;
        for(std::size_t n=0;n<mappings.size();++n){
            const Json& binding=mappings[n];
            const auto bt=at+".mappings["+std::to_string(n)+"]";
            if(!fields(binding,{"id","action","control","modifiers","triggers","consume"},bt,error))return false;
            ei::Mapping mapping;
            if(!text(binding["action"],mapping.action,bt+".action",error))return false;
            if(!names.contains(mapping.action))return fail(error,bt+".action","unknown input action");
            std::int64_t id=0;
            if(binding.has("id")&&!integer(binding["id"],id,1,1000000000,bt+".id",error))return false;
            mapping.id=std::uint64_t(id);
            if(mapping.id&&!ids.insert(mapping.id).second)return fail(error,bt+".id","duplicate mapping id");
            if(!readControl(binding["control"],mapping.control,bt+".control",error))return false;
            if(!readModifiers(binding["modifiers"],mapping.modifiers,bt+".modifiers",error))return false;
            if(!readTriggers(binding["triggers"],mapping.triggers,bt+".triggers",error))return false;
            if(!flag(binding,"consume",mapping.consume,bt,error))return false;
            config.context.mappings.push_back(std::move(mapping));
        }
        result.contexts.push_back(std::move(config));
    }
    for(const auto& a:result.actions)
        if(!checkChords(a.triggers,names,a.name,"input.actions."+a.name,error))return false;
    for(const auto& c:result.contexts)
        for(const auto& m:c.context.mappings)
            if(!checkChords(m.triggers,names,m.action,"input.contexts."+c.context.name,error))return false;
    // Check transitive dependencies (including NotChord and cross-context
    // mappings) after all forward references have been resolved. Reject cycles
    // transactionally before touching the caller's Asset.
    std::map<std::string,ei::Action> graphActions;
    std::map<std::string,ei::MappingContext> graphContexts;
    for(const auto& action:result.actions)graphActions.emplace(action.name,action);
    for(const auto& context:result.contexts)
        graphContexts.emplace(context.context.name,context.context);
    if(!aiverse::lib::gameplay::enhanced::detail::chordGraphAcyclic(graphActions,graphContexts))
        return fail(error,"input","cyclic chord/notChord dependency");
    out=std::move(result);
    return true;
}
inline bool installAsset(ei::InputFlow& input,const Asset& asset,std::string* error=nullptr) {
    // Construct off to the side: invalid assets must not destroy the caller's current mapping.
    ei::InputFlow replacement;
    for(const auto& action:asset.actions)
        if(!replacement.defineAction(action))return detail::fail(error,"input","invalid action name");
    for(const auto& c:asset.contexts)
        if(!replacement.registerContext(c.context))return detail::fail(error,"input","invalid context or mapping id");
    for(const auto& c:asset.contexts)
        if(c.active&&!replacement.activateContext(c.context.name,c.priority))
            return detail::fail(error,"input","failed activating context");
    input=std::move(replacement);
    return true;
}
inline Json writeAsset(const Asset& asset) {
    Json root=Json::object();root.set("version",Json::number(std::int64_t(1)));
    Json actions=Json::array(),contexts=Json::array();
    for(const auto& a:asset.actions){
        Json j=Json::object();
        j.set("name",Json::string(a.name));
        j.set("type",Json::string(detail::typeName(a.type)));
        if(!a.modifiers.empty())j.set("modifiers",detail::writeModifiers(a.modifiers));
        if(!a.triggers.empty())j.set("triggers",detail::writeTriggers(a.triggers));
        actions.push(std::move(j));
    }
    for(const auto& c:asset.contexts){
        Json j=Json::object();j.set("name",Json::string(c.context.name));
        j.set("priority",Json::number(std::int64_t(c.priority)));
        j.set("active",Json::fromBool(c.active));
        Json bindings=Json::array();
        for(const auto& m:c.context.mappings){
            Json binding=Json::object();
            if(m.id)binding.set("id",Json::number(std::int64_t(m.id)));
            binding.set("action",Json::string(m.action));
            binding.set("control",detail::writeControl(m.control));
            if(!m.modifiers.empty())binding.set("modifiers",detail::writeModifiers(m.modifiers));
            if(!m.triggers.empty())binding.set("triggers",detail::writeTriggers(m.triggers));
            binding.set("consume",Json::fromBool(m.consume));
            bindings.push(std::move(binding));
        }
        j.set("mappings",std::move(bindings));contexts.push(std::move(j));
    }
    root.set("actions",std::move(actions));root.set("contexts",std::move(contexts));
    return root;
}
inline bool readProfile(const Json& json,Profile& out,std::string* error=nullptr) {
    using namespace detail;
    if(!fields(json,{"version","rebindings"},"inputProfile",error))return false;
    std::int64_t version=0;
    if(!integer(json["version"],version,1,1,"inputProfile.version",error))return false;
    const Json& entries=json["rebindings"];
    if(!entries.isArray()||entries.size()>8192)
        return fail(error,"inputProfile.rebindings","expected array (max 8192)");
    Profile result;
    std::set<std::pair<std::string,std::uint64_t>> unique;
    for(std::size_t i=0;i<entries.size();++i){
        const Json& j=entries[i];const auto at="inputProfile.rebindings["+std::to_string(i)+"]";
        if(!fields(j,{"context","id","control"},at,error))return false;
        Rebind entry;
        if(!text(j["context"],entry.context,at+".context",error))return false;
        std::int64_t id=0;
        if(!integer(j["id"],id,1,1000000000,at+".id",error))return false;
        entry.mappingId=std::uint64_t(id);
        if(!unique.insert({entry.context,entry.mappingId}).second)
            return fail(error,at,"duplicate rebinding");
        if(!readControl(j["control"],entry.control,at+".control",error))return false;
        result.rebindings.push_back(std::move(entry));
    }
    out=std::move(result);return true;
}
inline Json writeProfile(const Profile& profile) {
    Json root=Json::object();
    root.set("version",Json::number(std::int64_t(1)));
    Json list=Json::array();
    for(const auto& entry:profile.rebindings){
        Json item=Json::object();
        item.set("context",Json::string(entry.context));
        item.set("id",Json::number(std::int64_t(entry.mappingId)));
        item.set("control",detail::writeControl(entry.control));
        list.push(std::move(item));
    }
    root.set("rebindings",std::move(list));
    return root;
}
inline bool applyProfile(ei::InputFlow& input,const Profile& profile,std::string* error=nullptr) {
    // Validate every override before mutation. Unknown IDs are reported, never partially applied.
    for(const auto& override:profile.rebindings) {
        const auto* context=input.context(override.context);
        if(!context)return detail::fail(error,"inputProfile."+override.context,"unknown context");
        bool found=false;
        for(const auto& mapping:context->mappings)
            if(mapping.id==override.mappingId){found=true;break;}
        if(!found)return detail::fail(error,"inputProfile."+override.context,"unknown mapping id");
    }
    for(const auto& override:profile.rebindings)
        if(!input.rebind(override.context,override.mappingId,override.control))
            return detail::fail(error,"inputProfile","rebinding failed");
    return true;
}
} // namespace aiverse::platform::enhanced_config
