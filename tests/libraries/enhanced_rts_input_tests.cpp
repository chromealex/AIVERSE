#include "apps/client/ClientInputRouting.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace p = aiverse::platform;
namespace ei = aiverse::lib::gameplay::inputflow;
using aiverse::client::ClientInputRouting;
#define CHECK(x) do { if(!(x)){std::cerr<<"RTS input test line "<<__LINE__<<": " #x "\n";std::abort();} } while(false)
static bool contains(const std::vector<std::string>& actions,const char* key) {
    return std::find(actions.begin(),actions.end(),key)!=actions.end();
}
static p::Event key(p::Key keyCode,bool down=true) {
    p::Event e;
    e.type=down?p::EventType::KeyDown:p::EventType::KeyUp;
    e.key=keyCode;
    return e;
}
int main() {
    ClientInputRouting route;
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    CHECK(route.system().state("IdleCameraPan")->value.x==-1.0f);
    CHECK(!contains(route.onEvent(key(p::Key::A),true),"AttackMove")); // key repeat
    CHECK(!contains(route.onEvent(key(p::Key::A,false),true),"AttackMove"));
    CHECK(contains(route.onEvent(key(p::Key::P),true),"Patrol"));
    route.onEvent(key(p::Key::P,false),true);
    CHECK(contains(route.onEvent(key(p::Key::R),true),"Repair"));
    route.onEvent(key(p::Key::R,false),true);
    CHECK(contains(route.onEvent(key(p::Key::E),true),"Reclaim"));
    route.onEvent(key(p::Key::E,false),true);
    CHECK(contains(route.onEvent(key(p::Key::S),true),"Stop"));
    route.onEvent(key(p::Key::S,false),true);
    CHECK(contains(route.onEvent(key(p::Key::H),true),"Hold"));
    route.onEvent(key(p::Key::H,false),true);
    CHECK(contains(route.onEvent(key(p::Key::Space),true),"FocusCamera"));
    route.onEvent(key(p::Key::Space,false),true);
    CHECK(route.system().registeredContexts().size()==7);
    CHECK(!route.system().state("CameraPan")->active);
    route.onEvent(key(p::Key::Up),true);
    CHECK(route.system().state("CameraPan")->value.y==1.0f);
    route.onEvent(key(p::Key::Right),true);
    CHECK(route.system().state("CameraPan")->value.x==1.0f);
    route.onEvent(key(p::Key::Up,false),true);
    route.onEvent(key(p::Key::Right,false),true);

    p::Event wheel;wheel.type=p::EventType::MouseWheel;wheel.wheelY=2.0f;
    auto zoom=route.onEvent(wheel,true);
    CHECK(contains(zoom,"CameraZoom"));
    CHECK(route.system().state("CameraZoom")->value.x==2.0f);
    CHECK(route.onEvent(wheel,false).empty()); // UI owns this wheel
    p::Event click;click.type=p::EventType::MouseDown;click.button=0;
    CHECK(contains(route.onEvent(click,true),"Select"));
    click.type=p::EventType::MouseUp;route.onEvent(click,true);
    // World pointer gestures remain generic, but their initiating buttons
    // respect authored Input Actions instead of the hardcoded LMB/RMB roles.
    CHECK(route.logicalMouseButton(0)==0);
    CHECK(route.logicalMouseButton(1)==1);
    CHECK(route.logicalMouseButton(2)==2);
    CHECK(route.system().rebind("RTS.Pointer",1,{ei::Device::MouseButton,3}));
    CHECK(route.logicalMouseButton(0)==-1);
    CHECK(route.logicalMouseButton(3)==0);
    CHECK(route.system().rebind("RTS.Pointer",1,{ei::Device::MouseButton,0}));
    // Default top-down/gesture contexts are genre-neutral and do not
    // accidentally enable hero combat while playing ordinary RTS.
    CHECK(!route.system().isContextActive("RTS.TopDown"));
    CHECK(route.system().isContextActive("RTS.Gestures"));
    route.setTopDownEnabled(true);
    CHECK(route.system().isContextActive("RTS.TopDown"));
    route.setTopDownEnabled(false);
    CHECK(!route.system().isContextActive("RTS.TopDown"));
    CHECK(!route.system().isContextActive("RTS.Planet"));
    CHECK(!route.system().isContextActive("RTS.Possession"));
    route.setPlanetEnabled(true);
    CHECK(route.system().isContextActive("RTS.Planet"));
    CHECK(contains(route.onEvent(key(p::Key::Tab),true),"PlanetNextBody"));
    route.onEvent(key(p::Key::Tab,false),true);
    route.setPlanetEnabled(false);
    route.setPossessionEnabled(true);
    CHECK(route.system().isContextActive("RTS.Possession"));
    CHECK(route.onEvent(key(p::Key::W),true).empty() ||
          route.system().state("PossessedMove")->active);
    CHECK(route.system().state("PossessedMove")->value.y>0.0f);
    route.onEvent(key(p::Key::W,false),true);
    route.setPossessionEnabled(false);
    // Stage 52: a pointer event captured by a HUD panel must not cancel a
    // still-held keyboard camera Action. Captured keyboard events cancel keys.
    route.onEvent(key(p::Key::W),true);
    CHECK(route.system().state("IdleCameraPan")->active);
    p::Event hudMove; hudMove.type=p::EventType::MouseMove; hudMove.deltaX=4.0f;
    CHECK(route.onEvent(hudMove,false).empty());
    CHECK(route.system().state("IdleCameraPan")->active);
    route.onEvent(key(p::Key::W,false),true);
    CHECK(!route.system().state("IdleCameraPan")->active);
    // An active UI/modal context must prevent keys from leaking into gameplay.
    CHECK(route.onEvent(key(p::Key::A),false).empty());
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    p::Event lost;lost.type=p::EventType::FocusLost;
    CHECK(route.onEvent(lost,false).empty());
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    route.onEvent(key(p::Key::A,false),true);
    // Higher-priority context wins on the same control, as in Unreal Mapping Contexts.
    CHECK(route.system().defineAction({"ModalAccept",ei::ValueType::Boolean}));
    ei::MappingContext modal{"RTS.Modal"};
    modal.bind("ModalAccept",{ei::Device::Keyboard,int(p::Key::A)},{},{ei::Trigger::pressed()});
    CHECK(route.system().registerContext(modal));
    CHECK(route.system().activateContext("RTS.Modal",100));
    auto triggered=route.onEvent(key(p::Key::A),true);
    CHECK(contains(triggered,"ModalAccept"));
    CHECK(!contains(triggered,"AttackMove"));
    route.onEvent(key(p::Key::A,false),true);
    route.system().deactivateContext("RTS.Modal");
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    std::cout<<"RTS InputFlow semantic input / context routing passed\n";
}
