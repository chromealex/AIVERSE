#pragma once
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <aiverselib/app/Screens.h>
namespace aiverse::lib::app {
// Root must outlive this binding. Destroy bindings before closing/reloading the document.
// Retain one root per screen; replacing children does not invalidate the root listener.
class RmlScreenBinding final : public Rml::EventListener {
    Rml::Element& root_;
    ScreenStack& stack_;
    Actions& actions_;
    std::string screenId_;
public:
    RmlScreenBinding(Rml::Element& root,ScreenStack& stack,Actions& actions,std::string screenId)
        :root_(root),stack_(stack),actions_(actions),screenId_(std::move(screenId)) {root_.AddEventListener("click",this);}
    ~RmlScreenBinding() override {root_.RemoveEventListener("click",this);stack_.close(screenId_);}
    RmlScreenBinding(const RmlScreenBinding&)=delete;
    RmlScreenBinding& operator=(const RmlScreenBinding&)=delete;
    template<class Translate> bool show(Screen screen,Translate translate) {
        if(screen.id!=screenId_||!validScreen(screen))return false;
        const auto markup=screenMarkup(screen,translate);
        if(!stack_.open(std::move(screen)))return false;
        root_.SetInnerRML(markup);return true;
    }
    void ProcessEvent(Rml::Event& event) override {
        auto* element=event.GetTargetElement();
        while(element&&element!=&root_){
            auto action=stack_.click(screenId_,element->GetId());
            if(action){actions_.push(std::move(*action));event.StopPropagation();return;}
            element=element->GetParentNode();
        }
    }
};
// Map snapshot keys to element IDs. Text is escaped, never evaluated as markup.
inline std::vector<std::string> applyBindings(Rml::Element& root,const std::vector<Change>& changes) {
    std::vector<std::string> missing;
    for(const auto& change:changes){
        auto* element=root.GetElementById(change.key);
        if(!element){missing.push_back(change.key);continue;}
        element->SetInnerRML(escapeMarkup(displayValue(change.value)));
    }
    return missing;
}
}
