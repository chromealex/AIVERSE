// AIVerse InputFlow editor: author project-owned Input Actions and contexts.
// Do not apply these presentation mappings to the deterministic simulation.
#include "../Editor.h"
#include "engine/platform/InputFlow.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace aiverse::editor {
namespace fs = std::filesystem;
namespace ei = lib::gameplay::inputflow;
namespace ecfg = platform::inputflow_config;

namespace {
const char* kValueNames[]{"Boolean","Axis1D","Axis2D","Axis3D"};
const char* kDeviceNames[]{"Keyboard","Mouse Button","Mouse Axis","Mouse Wheel",
                            "Gamepad Button","Gamepad Axis","Touch"};
const char* kModifierNames[]{"Scale","Negate","Dead Zone","Swizzle","Normalize"};
const char* kTriggerNames[]{"Down","Pressed","Released","Hold","Tap","Chord",
                            "Double Tap","Hold & Release","Pulse","Not Chord"};

bool uniqueName(const std::vector<ei::Action>& actions, const std::string& name) {
    return !name.empty() && std::none_of(actions.begin(),actions.end(),
        [&](const auto& a){return a.name==name;});
}
bool uniqueContext(const std::vector<ecfg::ContextConfig>& contexts, const std::string& name) {
    return !name.empty() && std::none_of(contexts.begin(),contexts.end(),
        [&](const auto& c){return c.context.name==name;});
}
std::optional<platform::Key> capturedKey() {
    using K = platform::Key;
    // Dear ImGui's named key ranges are consecutive; platform::Key matches their letter/number order.
    for (int n=0;n<26;++n)
        if (ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_A)+n)))
            return K(int(K::A)+n);
    for (int n=0;n<10;++n)
        if (ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_0)+n)))
            return K(int(K::Num0)+n);
    for (int n=0;n<12;++n)
        if (ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_F1)+n)))
            return K(int(K::F1)+n);
    for (const auto& [imgui,key] : {
        std::pair{ImGuiKey_Space,K::Space},std::pair{ImGuiKey_Tab,K::Tab},
        std::pair{ImGuiKey_Enter,K::Enter},std::pair{ImGuiKey_Backspace,K::Backspace},
        std::pair{ImGuiKey_Delete,K::Delete},std::pair{ImGuiKey_Insert,K::Insert},
        std::pair{ImGuiKey_LeftArrow,K::Left},std::pair{ImGuiKey_RightArrow,K::Right},
        std::pair{ImGuiKey_UpArrow,K::Up},std::pair{ImGuiKey_DownArrow,K::Down},
        std::pair{ImGuiKey_LeftShift,K::LeftShift},std::pair{ImGuiKey_RightShift,K::RightShift},
        std::pair{ImGuiKey_LeftCtrl,K::LeftCtrl},std::pair{ImGuiKey_RightCtrl,K::RightCtrl},
        std::pair{ImGuiKey_LeftAlt,K::LeftAlt},std::pair{ImGuiKey_RightAlt,K::RightAlt},
        std::pair{ImGuiKey_Home,K::Home},std::pair{ImGuiKey_End,K::End},
        std::pair{ImGuiKey_PageUp,K::PageUp},std::pair{ImGuiKey_PageDown,K::PageDown}
    }) if(ImGui::IsKeyPressed(imgui)) return key;
    return std::nullopt;
}
bool drawModifiers(std::vector<ei::Modifier>& modifiers) {
    bool changed=false;
    for(std::size_t n=0;n<modifiers.size();) {
        ImGui::PushID(int(n));
        auto& mod=modifiers[n];
        int kind=int(mod.kind);
        if(ImGui::Combo("Modifier",&kind,kModifierNames,5)) {
            mod=ei::Modifier{};
            mod.kind=ei::ModifierKind(kind);
            changed=true;
        }
        if(mod.kind==ei::ModifierKind::Scale) {
            float scalars[3]{mod.scalar.x,mod.scalar.y,mod.scalar.z};
            if(ImGui::InputFloat3("XYZ",scalars)) {
                mod.scalar={scalars[0],scalars[1],scalars[2]};changed=true;
            }
        } else if(mod.kind==ei::ModifierKind::DeadZone) {
            changed|=ImGui::SliderFloat("Threshold",&mod.threshold,0.0f,0.999f);
        } else if(mod.kind==ei::ModifierKind::Swizzle) {
            changed|=ImGui::InputInt3("Order",mod.swizzle.data());
        }
        const bool remove=ImGui::SmallButton("Remove modifier");
        ImGui::PopID();
        if(remove){modifiers.erase(modifiers.begin()+std::ptrdiff_t(n));changed=true;}
        else ++n;
    }
    if(ImGui::SmallButton("+ Modifier")){modifiers.push_back(ei::Modifier{});changed=true;}
    return changed;
}
bool drawTriggers(std::vector<ei::Trigger>& triggers,const std::vector<ei::Action>& actions) {
    bool changed=false;
    for(std::size_t n=0;n<triggers.size();) {
        ImGui::PushID(int(n));
        auto& trig=triggers[n];
        int kind=int(trig.kind);
        if(ImGui::Combo("Trigger",&kind,kTriggerNames,10)) {
            trig=ei::Trigger{};trig.kind=ei::TriggerKind(kind);
            if(trig.kind==ei::TriggerKind::Hold||trig.kind==ei::TriggerKind::Tap||
               trig.kind==ei::TriggerKind::DoubleTap||trig.kind==ei::TriggerKind::HoldAndRelease||
               trig.kind==ei::TriggerKind::Pulse)trig.seconds=0.25f;
            if((trig.kind==ei::TriggerKind::Chord||trig.kind==ei::TriggerKind::NotChord)&&
               !actions.empty())trig.chordAction=actions.front().name;
            changed=true;
        }
        if(trig.kind==ei::TriggerKind::Hold||trig.kind==ei::TriggerKind::Tap||
           trig.kind==ei::TriggerKind::DoubleTap||trig.kind==ei::TriggerKind::HoldAndRelease||
           trig.kind==ei::TriggerKind::Pulse)
            changed|=ImGui::SliderFloat("Seconds",&trig.seconds,
                                         trig.kind==ei::TriggerKind::Pulse?0.01f:0.0f,10.0f);
        if(trig.kind==ei::TriggerKind::Chord||trig.kind==ei::TriggerKind::NotChord) {
            if(ImGui::BeginCombo("Requires action",trig.chordAction.c_str())) {
                for(const auto& action:actions)
                    if(ImGui::Selectable(action.name.c_str(),trig.chordAction==action.name)) {
                        trig.chordAction=action.name;changed=true;
                    }
                ImGui::EndCombo();
            }
        }
        const bool remove=ImGui::SmallButton("Remove trigger");
        ImGui::PopID();
        if(remove){triggers.erase(triggers.begin()+std::ptrdiff_t(n));changed=true;}
        else ++n;
    }
    if(ImGui::SmallButton("+ Trigger")){triggers.push_back(ei::Trigger::down());changed=true;}
    return changed;
}
} // namespace

class InputWindow final : public EditorWindow {
public:
    const char* id() const override {return "input";}
    const char* title() const override {return "InputFlow Actions";}
    const char* group() const override {return "logic";}
    const char* dockArea() const override {return "right";}
    int order() const override {return 45;}
    bool openByDefault() const override {return false;}
    bool captureGlobalShortcuts() const override {return capture_.has_value();}
    void update(Editor& ed,double) override {
        if(!open || !ed.isWindowInFront(this)) capture_.reset();
    }

    void draw(Editor& ed) override {
        const std::string dir=ed.ws().project.dir;
        if(dir.empty()){ImGui::TextUnformatted("Откройте проект для настройки ввода.");return;}
        if(dir!=loadedDir_) load(dir);
        ImGui::TextWrapped("Project: input/actions.json. These are project-owned mappings; "
                           "player rebindings are saved separately in the user's profile.");
        if(ed.ws().project.isFps())
            ImGui::TextWrapped("FPS: this asset REPLACES ALL built-in actions, not just edited keys. "
                               "Save only a complete FPS action scheme.");
        if(ImGui::Button("Use FPS defaults")) {
            asset_=platform::inputflow_presets::fpsReference();dirty_=true;error_.clear();capture_.reset();
        }
        ImGui::SameLine();
        if(ImGui::Button("Use RTS defaults")) {
            asset_=platform::inputflow_presets::rtsReference();dirty_=true;error_.clear();capture_.reset();
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Replaces the mappings in this editor buffer; Save to persist.");
        if(ImGui::Button("Reload"))load(dir);
        ImGui::SameLine();
        if(ImGui::Button(dirty_?"Save changes *":"Save changes"))save(dir);
        ImGui::SameLine();
        ImGui::TextDisabled("%s",exists_?"Asset exists":"No asset yet");
        int policy=int(capturePolicy_);
        if(ImGui::Combo("On capture conflict",&policy,
                        "Allow duplicate\0Reject duplicate\0Swap keys\0\0"))
            capturePolicy_=ei::RebindPolicy(policy);
        if(!error_.empty())ImGui::TextWrapped("Input: %s",error_.c_str());
        ImGui::Separator();
        actions();
        ImGui::Separator();
        contexts();
        // Report conflicts in the actual context ordering, including shadows
        // between multiple active contexts, not only adjacent bindings.
        ei::InputFlow inspector;
        std::string validation;
        if(ecfg::installAsset(inspector,asset_,&validation)){
            const auto warnings=inspector.conflicts();
            if(!warnings.empty()&&ImGui::CollapsingHeader("Control conflicts")) {
                for(const auto& c:warnings){
                    ImGui::TextWrapped("%s:%s (%llu) / %s:%s (%llu) — %s",
                        c.higherContext.c_str(),c.higherAction.c_str(),
                        static_cast<unsigned long long>(c.higherMappingId),
                        c.lowerContext.c_str(),c.lowerAction.c_str(),
                        static_cast<unsigned long long>(c.lowerMappingId),
                        c.shadowed?"later mapping shadowed":"shared control");
                }
            }
        }
        if(capture_) {
            ImGui::Separator();
            ImGui::TextUnformatted("Press a key to bind; Escape cancels.");
            if(ImGui::IsKeyPressed(ImGuiKey_Escape))capture_.reset();
            else if(auto key=capturedKey()) {
                const auto [context,mapping]=*capture_;
                if(context<asset_.contexts.size() && mapping<asset_.contexts[context].context.mappings.size()) {
                    auto& mappings=asset_.contexts[context].context.mappings;
                    const ei::Control requested{ei::Device::Keyboard,int(*key),0};
                    std::vector<std::size_t> duplicates;
                    for(std::size_t k=0;k<mappings.size();++k)
                        if(k!=mapping&&mappings[k].control==requested)duplicates.push_back(k);
                    if(capturePolicy_==ei::RebindPolicy::Reject&&!duplicates.empty())
                        error_="That key is already used in this context.";
                    else if(capturePolicy_==ei::RebindPolicy::Swap&&duplicates.size()>1)
                        error_="Cannot swap a key assigned to multiple mappings.";
                    else {
                        const ei::Control previous=mappings[mapping].control;
                        if(capturePolicy_==ei::RebindPolicy::Swap&&!duplicates.empty())
                            mappings[duplicates.front()].control=previous;
                        mappings[mapping].control=requested;
                        error_.clear();dirty_=true;
                    }
                }
                capture_.reset();
            }
        }
    }
private:
    void load(const std::string& dir) {
        loadedDir_=dir;asset_={};exists_=false;dirty_=false;error_.clear();capture_.reset();
        const fs::path path=fs::path(dir)/"input"/"actions.json";
        std::error_code ec;
        if(!fs::exists(path,ec))return;
        exists_=true;
        ecfg::Asset value;
        if(ecfg::loadAssetFile(path,value,&error_))asset_=std::move(value);
    }
    void save(const std::string& dir) {
        ecfg::Asset checked;
        if(!ecfg::readAsset(ecfg::writeAsset(asset_),checked,&error_))return;
        if(checked.actions.empty()||checked.contexts.empty()){
            error_="Add at least one Action and one Mapping Context before saving";return;
        }
        const fs::path path=fs::path(dir)/"input"/"actions.json";
        if(!ecfg::saveAssetFile(path,checked,&error_))return;
        asset_=std::move(checked);dirty_=false;exists_=true;error_.clear();
    }
    void actions() {
        if(!ImGui::CollapsingHeader("InputFlow Actions",ImGuiTreeNodeFlags_DefaultOpen))return;
        ImGui::InputText("New action",actionName_,sizeof actionName_);
        ImGui::SameLine();
        if(ImGui::Button("Add action")&&uniqueName(asset_.actions,actionName_)){
            asset_.actions.push_back({actionName_,ei::ValueType::Boolean});actionName_[0]=0;dirty_=true;
        }
        for(std::size_t i=0;i<asset_.actions.size();) {
            ImGui::PushID(int(i));
            auto& action=asset_.actions[i];
            ImGui::SeparatorText(action.name.c_str());
            int type=int(action.type);
            if(ImGui::Combo("Value type",&type,kValueNames,4)){action.type=ei::ValueType(type);dirty_=true;}
            if(ImGui::TreeNode("Action modifiers")){dirty_|=drawModifiers(action.modifiers);ImGui::TreePop();}
            if(ImGui::TreeNode("Action triggers")){dirty_|=drawTriggers(action.triggers,asset_.actions);ImGui::TreePop();}
            const bool remove=ImGui::SmallButton("Delete action");
            const std::string removed=action.name;
            ImGui::PopID();
            if(remove){
                asset_.actions.erase(asset_.actions.begin()+std::ptrdiff_t(i));
                for(auto& context:asset_.contexts) {
                    auto& mappings=context.context.mappings;
                    std::erase_if(mappings,[&](const auto& m){return m.action==removed;});
                }
                dirty_=true;
            }else ++i;
        }
    }
    void contexts() {
        if(!ImGui::CollapsingHeader("Mapping Contexts",ImGuiTreeNodeFlags_DefaultOpen))return;
        ImGui::InputText("New context",contextName_,sizeof contextName_);
        ImGui::SameLine();
        if(ImGui::Button("Add context")&&uniqueContext(asset_.contexts,contextName_)){
            ecfg::ContextConfig context;
            context.context.name=contextName_;
            asset_.contexts.push_back(std::move(context));contextName_[0]=0;dirty_=true;
        }
        for(std::size_t i=0;i<asset_.contexts.size();) {
            auto& context=asset_.contexts[i];
            ImGui::PushID(int(i));
            const bool expanded=ImGui::TreeNode(context.context.name.c_str());
            if(expanded) {
                dirty_|=ImGui::Checkbox("Active",&context.active);
                dirty_|=ImGui::InputInt("Priority",&context.priority);
                if(ImGui::SmallButton("+ Binding")&&!asset_.actions.empty()){
                    std::uint64_t next=1;
                    for(const auto& mapping:context.context.mappings)
                        next=std::max(next,mapping.id+1);
                    ei::Mapping mapping;
                    mapping.id=next;
                    mapping.action=asset_.actions.front().name;
                    mapping.control={ei::Device::Keyboard,int(platform::Key::Space),0};
                    context.context.mappings.push_back(std::move(mapping));dirty_=true;
                }
                if(asset_.actions.empty())ImGui::TextDisabled("Define an action before adding bindings.");
                for(std::size_t j=0;j<context.context.mappings.size();) {
                    ImGui::PushID(int(j));
                    auto& mapping=context.context.mappings[j];
                    ImGui::Separator();
                    ImGui::Text("Mapping ID: %llu",static_cast<unsigned long long>(mapping.id));
                    if(ImGui::BeginCombo("Action",mapping.action.c_str())) {
                        for(const auto& action:asset_.actions)
                            if(ImGui::Selectable(action.name.c_str(),mapping.action==action.name)){
                                mapping.action=action.name;dirty_=true;
                            }
                        ImGui::EndCombo();
                    }
                    int device=int(mapping.control.device);
                    if(ImGui::Combo("Device",&device,kDeviceNames,7)){
                        mapping.control.device=ei::Device(device);mapping.control.code=0;
                        mapping.control.deviceIndex=0;dirty_=true;
                    }
                    dirty_|=ImGui::InputInt("Code",&mapping.control.code);
                    if(mapping.control.device==ei::Device::GamepadAxis||
                       mapping.control.device==ei::Device::GamepadButton||
                       mapping.control.device==ei::Device::Touch)
                        dirty_|=ImGui::InputInt("Device index",&mapping.control.deviceIndex);
                    if(ImGui::SmallButton("Capture keyboard key"))capture_=std::pair{i,j};
                    dirty_|=ImGui::Checkbox("Consume",&mapping.consume);
                    if(ImGui::TreeNode("Mapping modifiers")){dirty_|=drawModifiers(mapping.modifiers);ImGui::TreePop();}
                    if(ImGui::TreeNode("Mapping triggers")){dirty_|=drawTriggers(mapping.triggers,asset_.actions);ImGui::TreePop();}
                    for(std::size_t k=0;k<j;++k)
                        if(context.context.mappings[k].control==mapping.control)
                            ImGui::TextDisabled("Conflict: this control is already mapped in this context.");
                    const bool remove=ImGui::SmallButton("Delete binding");
                    ImGui::PopID();
                    if(remove){context.context.mappings.erase(context.context.mappings.begin()+std::ptrdiff_t(j));capture_.reset();dirty_=true;}
                    else ++j;
                }
                ImGui::TreePop();
            }
            const bool remove=ImGui::SmallButton("Delete context");
            ImGui::PopID();
            if(remove){asset_.contexts.erase(asset_.contexts.begin()+std::ptrdiff_t(i));capture_.reset();dirty_=true;}
            else ++i;
        }
    }
    std::string loadedDir_,error_;
    ecfg::Asset asset_;
    bool exists_=false,dirty_=false;
    char actionName_[96]{},contextName_[96]{};
    std::optional<std::pair<std::size_t,std::size_t>> capture_;
    ei::RebindPolicy capturePolicy_=ei::RebindPolicy::Swap;
};
AIVERSE_EDITOR_WINDOW(InputWindow);
} // namespace aiverse::editor
