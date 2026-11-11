#include "libraries/adapters/rmlui/ScreenBinding.h"
void compileRmlBinding(Rml::Element& root){
    aiverse::lib::app::ScreenStack stack;aiverse::lib::app::Actions actions;
    aiverse::lib::app::RmlScreenBinding binding(root,stack,actions,"hud");
    binding.show({"hud","hud.title",aiverse::lib::app::ScreenKind::Hud,false,{}},[](const std::string& key){return key;});
}
