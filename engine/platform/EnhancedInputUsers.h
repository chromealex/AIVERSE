// Multi-local-player presentation routing for AIVerse InputFlow.
// Each local player owns an independent InputFlow and input lifecycles.
// One platform event or gamepad sample is delivered only to its assigned owner.
// No game genre, simulation state, network command or ECS dependencies.
#pragma once

#include "EnhancedInputAdapter.h"
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::platform {

class InputFlowUsers {
public:
    using UserId = std::uint32_t;
    using Core = lib::gameplay::inputflow::InputFlow;

    bool addUser(UserId id, Core scheme = {}) {
        if(users_.contains(id))return false;
        auto user=std::make_unique<User>();
        user->input=std::move(scheme);
        users_.emplace(id,std::move(user));
        return true;
    }
    bool removeUser(UserId id) {
        if(!users_.contains(id))return false;
        users_.at(id)->adapter.focusLost();
        for(auto it=gamepadOwner_.begin();it!=gamepadOwner_.end();)
            if(it->second==id)it=gamepadOwner_.erase(it);
            else ++it;
        if(keyboardOwner_==id)keyboardOwner_=kUnassigned;
        if(pointerOwner_==id)pointerOwner_=kUnassigned;
        if(touchOwner_==id)touchOwner_=kUnassigned;
        users_.erase(id);
        return true;
    }

    Core* input(UserId id) {
        auto it=users_.find(id);
        return it==users_.end()?nullptr:&it->second->input;
    }
    const Core* input(UserId id) const {
        auto it=users_.find(id);
        return it==users_.end()?nullptr:&it->second->input;
    }

    bool setKeyboardOwner(UserId id) { return moveOwner(keyboardOwner_,id,OwnerKind::Keyboard); }
    bool setPointerOwner(UserId id) { return moveOwner(pointerOwner_,id,OwnerKind::Pointer); }
    bool setTouchOwner(UserId id) { return moveOwner(touchOwner_,id,OwnerKind::Touch); }
    void clearKeyboardOwner() { clearOwner(keyboardOwner_,OwnerKind::Keyboard); }
    void clearPointerOwner() { clearOwner(pointerOwner_,OwnerKind::Pointer); }
    void clearTouchOwner() { clearOwner(touchOwner_,OwnerKind::Touch); }
    UserId keyboardOwner() const { return keyboardOwner_; }
    UserId pointerOwner() const { return pointerOwner_; }
    UserId touchOwner() const { return touchOwner_; }

    // Each physical gamepad may belong to one local user; each user accepts one
    // gamepad, mapped to logical deviceIndex 0 in that user's project scheme.
    // This makes standard FPS/RTS presets reusable in split-screen games.
    bool assignGamepad(int pad,UserId id) {
        if(pad<0 || pad>=kMaxGamepads || !users_.contains(id))return false;
        for(const auto& [assigned,owner]:gamepadOwner_)
            if(owner==id && assigned!=pad)return false;
        auto previous=gamepadOwner_.find(pad);
        if(previous!=gamepadOwner_.end()){
            if(previous->second==id)return true;
            if(auto old=users_.find(previous->second); old!=users_.end())
                old->second->input.cancelDevices(
                    {lib::gameplay::inputflow::Device::GamepadButton,
                     lib::gameplay::inputflow::Device::GamepadAxis},0);
        }
        users_.at(id)->input.cancelDevices(
            {lib::gameplay::inputflow::Device::GamepadButton,
             lib::gameplay::inputflow::Device::GamepadAxis},0);
        gamepadOwner_[pad]=id;
        return true;
    }
    void unassignGamepad(int pad) {
        auto it=gamepadOwner_.find(pad);
        if(it==gamepadOwner_.end())return;
        if(auto player=users_.find(it->second);player!=users_.end())
            player->second->input.cancelDevices(
                {lib::gameplay::inputflow::Device::GamepadButton,
                 lib::gameplay::inputflow::Device::GamepadAxis},0);
        gamepadOwner_.erase(it);
    }
    UserId gamepadOwner(int pad) const {
        const auto it=gamepadOwner_.find(pad);
        return it==gamepadOwner_.end()?kUnassigned:it->second;
    }

    // Call for OS events after UI capture resolution. The target owner's held
    // controls are cancelled when a modal UI captures input, avoiding stuck keys.
    void onEvent(const Event& e,bool gameplayAllowed=true) {
        if(e.type==EventType::FocusLost){cancelAll();return;}
        UserId owner=kUnassigned;
        switch(e.type) {
        case EventType::KeyDown:case EventType::KeyUp:owner=keyboardOwner_;break;
        case EventType::MouseDown:case EventType::MouseUp:
        case EventType::MouseMove:case EventType::MouseWheel:owner=pointerOwner_;break;
        case EventType::TouchBegin:case EventType::TouchMove:
        case EventType::TouchEnd:case EventType::TouchCancel:owner=touchOwner_;break;
        default:return;
        }
        const auto it=users_.find(owner);
        if(it==users_.end())return;
        if(gameplayAllowed)it->second->adapter.onEvent(e);
        else {
            switch(e.type) {
            case EventType::KeyDown:case EventType::KeyUp:
                it->second->adapter.cancelKeyboard();break;
            case EventType::MouseDown:case EventType::MouseUp:
            case EventType::MouseMove:case EventType::MouseWheel:
                it->second->adapter.cancelPointer();break;
            case EventType::TouchBegin:case EventType::TouchMove:
            case EventType::TouchEnd:case EventType::TouchCancel:
                it->second->adapter.cancelTouch();break;
            default:break;
            }
        }
    }

    // Poll the physical backend ONCE per frame; unlike one adapter per user,
    // this cannot accidentally broadcast every controller to every player.
    void pollGamepads() {
        using namespace lib::gameplay::inputflow;
        ::aiverse::platform::pollGamepads();
        for(const auto& [pad,owner]:gamepadOwner_) {
            auto user=users_.find(owner);
            if(user==users_.end())continue;
            const auto& state=::aiverse::platform::gamepad(pad);
            auto& input=user->second->input;
            for(int button=0;button<int(PadButton::Count);++button)
                input.setControl({Device::GamepadButton,button,0},
                    state.connected && state.down(PadButton(button))?1.0f:0.0f);
            for(int axis=0;axis<int(PadAxis::Count);++axis)
                input.setControl({Device::GamepadAxis,axis,0},
                    state.connected?state.axis(PadAxis(axis)):0.0f);
        }
    }

    // Returned events are owned by each user's InputFlow until that user
    // receives another update/cancel. Call after onEvent/pollGamepads.
    std::span<const lib::gameplay::inputflow::ActionEvent> update(UserId id,float dt) {
        auto* system=input(id);
        return system?system->update(dt):std::span<const lib::gameplay::inputflow::ActionEvent>{};
    }
    void cancelAll() {
        for(auto& [id,user]:users_){(void)id;user->adapter.focusLost();}
    }

    static constexpr UserId kUnassigned=~UserId{0};
private:
    enum class OwnerKind : std::uint8_t { Keyboard, Pointer, Touch };
    struct User {
        Core input;
        InputFlowAdapter adapter{input};
    };
    void cancelOwnedDevice(UserId owner,OwnerKind kind) {
        const auto it=users_.find(owner);
        if(it==users_.end())return;
        switch(kind) {
        case OwnerKind::Keyboard:it->second->adapter.cancelKeyboard();break;
        case OwnerKind::Pointer:it->second->adapter.cancelPointer();break;
        case OwnerKind::Touch:it->second->adapter.cancelTouch();break;
        }
    }
    void clearOwner(UserId& target,OwnerKind kind) {
        cancelOwnedDevice(target,kind);
        target=kUnassigned;
    }
    bool moveOwner(UserId& target,UserId id,OwnerKind kind) {
        if(!users_.contains(id))return false;
        if(target==id)return true;
        cancelOwnedDevice(target,kind);
        cancelOwnedDevice(id,kind);
        target=id;
        return true;
    }

    std::map<UserId,std::unique_ptr<User>> users_;
    std::map<int,UserId> gamepadOwner_;
    UserId keyboardOwner_=kUnassigned, pointerOwner_=kUnassigned, touchOwner_=kUnassigned;
};

// Source compatibility for callers using the original local-user routing type.
using EnhancedInputUsers = InputFlowUsers;
} // namespace aiverse::platform
