#include "../animation/AnimationEditor.h"
namespace aiverse::editor {
class AnimationGraphWindow : public EditorWindow {
public:
    const char* id() const override{return "animation_graph";}
    const char* title() const override{return "Граф анимации";}
    const char* group() const override{return "look";}
    const char* dockArea() const override{return "center";}
    bool openByDefault() const override{return false;}
    void draw(Editor& ed) override{panel_.draw(ed);}
    void update(Editor& ed,double dt) override{panel_.update(ed,dt);}
private:AnimationEditorPanel panel_{false};
};
AIVERSE_EDITOR_WINDOW(AnimationGraphWindow);
}
