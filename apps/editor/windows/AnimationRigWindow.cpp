#include "../animation/AnimationEditor.h"
namespace aiverse::editor {
class AnimationRigWindow : public EditorWindow {
public:
    const char* id() const override{return "animation_rig";}
    const char* title() const override{return "MotionWeave — Риг анимации";}
    const char* group() const override{return "look";}
    const char* dockArea() const override{return "center";}
    bool openByDefault() const override{return false;}
    void draw(Editor& ed) override{panel_.draw(ed);}
    void update(Editor& ed,double dt) override{
        panel_.update(ed,dt);
        // GPU state is disposable on dock-tab hide, unlike graph document
        // and user preview controls; keep it alive only while visible.
        if (!open || !ed.isWindowInFront(this)) panel_.suspendPreview();
    }
private:AnimationEditorPanel panel_{true};
};
AIVERSE_EDITOR_WINDOW(AnimationRigWindow);
}
