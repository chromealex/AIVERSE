// AIVerse InputFlow: shared, presentation-side semantic RTS shortcuts. Input rules are independent of ECS and Commands.
// The UI owns text/modal capture; no raw key state may enter the deterministic simulation.
#pragma once
#include <aiverselib/gameplay/InputFlow.h>
#include "engine/platform/Platform.h"
#include "engine/platform/InputFlow.h"
#include <string>
#include <vector>

namespace aiverse::client {
namespace ei = aiverse::lib::gameplay::inputflow;

class ClientInputRouting {
public:
    ClientInputRouting() {
        const auto preset=platform::inputflow_presets::rtsReference();
        for(const auto& action:preset.actions) input_.defineAction(action);
        for(const auto& item:preset.contexts){
            input_.registerContext(item.context);
            if(item.active) input_.activateContext(item.context.name,item.priority);
        }
    }

    ei::InputFlow& system() { return input_; }
    const ei::InputFlow& system() const { return input_; }

    // Call for every platform Event *after* UI capture is resolved; pass false for menus,
    // modal UI and text fields. A captured key is never left held when the UI takes focus.
    std::vector<std::string> onEvent(const platform::Event& e,bool gameplayAllowed) {
        if(e.type==platform::EventType::FocusLost) {
            adapter_.focusLost();
            return {};
        }
        if(!gameplayAllowed) {
            // A pointer over RmlUi must not clear an unrelated held keyboard
            // camera Action. Full FocusLost/modal lifecycle is handled above
            // and by GameClient::frame for text capture.
            switch(e.type) {
            case platform::EventType::KeyDown:case platform::EventType::KeyUp:
                adapter_.cancelKeyboard();break;
            case platform::EventType::MouseDown:case platform::EventType::MouseUp:
            case platform::EventType::MouseMove:case platform::EventType::MouseWheel:
                adapter_.cancelPointer();break;
            case platform::EventType::TouchBegin:case platform::EventType::TouchMove:
            case platform::EventType::TouchEnd:case platform::EventType::TouchCancel:
                adapter_.cancelTouch();break;
            default:break;
            }
            input_.update(0.0f); // settle canceled states without dispatching gameplay
            return {};
        }
        // Route all pointer/keyboard events after UI capture so authored semantic
        // actions can rebind clicks and zoom without altering selection gestures.
        adapter_.onEvent(e);
        std::vector<std::string> actions;
        for(const auto& event:input_.update(0.0f))
            if(event.phase==ei::Phase::Triggered)actions.push_back(event.action);
        return actions;
    }
    // Advance time-based Hold/Pulse/DoubleTap triggers once per presentation frame.
    // The adapter handles OS events immediately; only time advances here.
    std::vector<std::string> advance(float dt) {
        std::vector<std::string> actions;
        for(const auto& event:input_.update(dt))
            if(event.phase==ei::Phase::Triggered)actions.push_back(event.action);
        return actions;
    }
    // A script-defined hotkey takes precedence over generic input Actions.
    // Suppressing its held physical control also prevents later Hold/Pulse.
    void suppressKey(platform::Key key) {
        if(key==platform::Key::Unknown)return;
        input_.setControl({ei::Device::Keyboard,int(key)},0);
        input_.update(0.0f);
    }
    // Top-down possession is a higher-priority, optional context.
    // A project-specific action asset can omit it; in that case legacy buttons
    // remain usable but semantic customization requires a matching Action.
    void setTopDownEnabled(bool enabled) {
        for(const char* context:{"RTS.TopDown","RTS.TopDownAbilities"}) {
            if(!input_.context(context))continue;
            if(enabled && !input_.isContextActive(context))
                input_.activateContext(context,100);
            else if(!enabled && input_.isContextActive(context))
                input_.deactivateContext(context);
        }
    }
    void setPlanetEnabled(bool enabled) { setOptionalContext("RTS.Planet",enabled,80); }
    void setPossessionEnabled(bool enabled) { setOptionalContext("RTS.Possession",enabled,100); }

    // The world may still need pointer coordinates/drag sequences, but the
    // initiating physical mouse button must respect project-owned Select and
    // Command Action mappings. Return -1 when an unassigned button is pressed.
    int logicalMouseButton(int physical) const {
        const auto* ctx=input_.context("RTS.Pointer");
        if(!ctx || !input_.isContextActive("RTS.Pointer"))return physical;
        bool select=false,command=false;
        for(const auto& mapping:ctx->mappings) {
            if(mapping.control.device!=ei::Device::MouseButton ||
               mapping.control.code!=physical || mapping.control.deviceIndex!=0)continue;
            if(mapping.action=="Select")select=true;
            if(mapping.action=="Command")command=true;
        }
        if(select)return 0; // explicit tie-break; conflicts() reports duplicates
        if(command)return 1;
        if(physical==2)return 2; // independent camera pan
        return -1;
    }
    void reset() {adapter_.focusLost();}
private:
    void setOptionalContext(const char* name,bool enabled,int priority) {
        if(!input_.context(name))return;
        if(enabled && !input_.isContextActive(name))
            input_.activateContext(name,priority);
        else if(!enabled && input_.isContextActive(name))
            input_.deactivateContext(name);
    }
    ei::InputFlow input_;
    platform::InputFlowAdapter adapter_{input_};
};
} // namespace aiverse::client
