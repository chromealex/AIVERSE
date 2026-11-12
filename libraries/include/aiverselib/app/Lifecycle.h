#pragma once
#include <cstdint>
namespace aiverse::lib::app {
enum class AppPhase { Boot, Menu, Loading, Playing, Paused, Results, Error, Stopped };
enum class AppEvent { Ready, Start, Loaded, Pause, Resume, Finish, Back, Fail, Retry, Quit };
struct Lifecycle {
    AppPhase phase=AppPhase::Boot;
    std::uint64_t generation=0;
    bool send(AppEvent event) {
        AppPhase next=phase;
        if (phase==AppPhase::Stopped) return false;
        if (event==AppEvent::Quit) next=AppPhase::Stopped;
        else if (event==AppEvent::Fail) next=AppPhase::Error;
        else switch (phase) {
            case AppPhase::Boot:
                if(event==AppEvent::Ready) next=AppPhase::Menu;
                break;
            case AppPhase::Menu: if(event==AppEvent::Start) next=AppPhase::Loading; break;
            case AppPhase::Loading: if(event==AppEvent::Loaded) next=AppPhase::Playing; else if(event==AppEvent::Back) next=AppPhase::Menu; break;
            case AppPhase::Playing: if(event==AppEvent::Pause) next=AppPhase::Paused; else if(event==AppEvent::Finish) next=AppPhase::Results; break;
            case AppPhase::Paused: if(event==AppEvent::Resume) next=AppPhase::Playing; else if(event==AppEvent::Back) next=AppPhase::Menu; else if(event==AppEvent::Finish) next=AppPhase::Results; break;
            case AppPhase::Results: if(event==AppEvent::Back) next=AppPhase::Menu; break;
            case AppPhase::Error: if(event==AppEvent::Retry) next=AppPhase::Boot; else if(event==AppEvent::Back) next=AppPhase::Menu; break;
            case AppPhase::Stopped: break;
        }
        if(next==phase) return false;
        phase=next; ++generation; return true;
    }
    // Capture generation before asynchronous loads; reject completions from cancelled loads.
    bool loaded(std::uint64_t ticket) { return ticket==generation && send(AppEvent::Loaded); }
};
}
