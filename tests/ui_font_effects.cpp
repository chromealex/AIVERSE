#include "engine/ui/UiInternal.h"
#include "third_party/rmlui/Source/Core/FontEffectOutline.h"
#include "third_party/rmlui/Source/Core/FontEffectShadow.h"
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>

int testFontEffects(Rml::RenderManager& manager) {
    int errors=0;
    auto check=[&](bool ok,const char* name){if(!ok){++errors;std::fprintf(stderr,"font effects: %s\n",name);}};
    std::ifstream input(std::filesystem::path(__FILE__).parent_path().parent_path()/"third_party/fonts/inter/Inter-Regular.ttf",std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    rts::ui::UiFontEngine fonts;
    if (!fonts.addFont(bytes,"test",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal)) return 1;
    const auto face=fonts.GetFontFaceHandle("test",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,24);
    auto outline=std::make_shared<Rml::FontEffectOutline>();outline->Initialise(2);outline->SetColour({19,42,58,255});outline->SetFingerprint(1);
    auto shadow=std::make_shared<Rml::FontEffectShadow>();shadow->Initialise({3,4});shadow->SetColour({0,0,0,128});shadow->SetFingerprint(2);shadow->SetLayer(Rml::FontEffect::Layer::Front);
    const Rml::FontEffectList list{outline,shadow};
    const auto effects=fonts.PrepareFontEffects(face,list);
    check(effects!=0 && fonts.PrepareFontEffects(face,list)==effects,"stable effect handle");
    check(fonts.PrepareFontEffects(face,{})==0,"empty effect set");
    const Rml::String language="en";Rml::TextShapingContext ctx{language};Rml::TexturedMeshList meshes,plain;
    const auto white=Rml::Colourb(255,255,255,255).ToPremultiplied(.5f);
    const int width=fonts.GenerateString(manager,face,effects,"13",{10,30},white,.5f,ctx,meshes);
    const int normal=fonts.GenerateString(manager,face,0,"13",{10,30},white,.5f,ctx,plain);
    check(width==normal && width==fonts.GetStringWidth(face,"13",ctx,Rml::Character(0)),"effects preserve advance");
    check(meshes.size()==3 && plain.size()==1,"back/base/front layers");
    if(meshes.size()==3 && plain.size()==1) {
        const auto a=meshes[0].mesh.vertices[0],b=meshes[1].mesh.vertices[0],c=meshes[2].mesh.vertices[0];
        check(a.position.x==b.position.x-2 && a.position.y==b.position.y-2,"outline expands glyph origin");
        check(c.position.x==b.position.x+3 && c.position.y==b.position.y+4,"shadow offset");
        check(a.colour==outline->GetColour().ToPremultiplied(.5f) && b.colour==white && c.colour==shadow->GetColour().ToPremultiplied(.5f),"effect opacity and colour");
        check(meshes[1].texture==meshes[2].texture && !(meshes[0].texture==meshes[1].texture),"outline texture and shared shadow texture");
        check(meshes[0].texture.GetDimensions().x>0,"effect texture materializes");
    }
    const int version=fonts.GetVersion(face);meshes.clear();
    fonts.GenerateString(manager,face,effects,"13",{10,30},white,.5f,ctx,meshes);
    check(fonts.GetVersion(face)==version,"cached text does not regenerate texture");
    meshes.clear();fonts.GenerateString(manager,face,effects,"13 Ω",{10,30},white,.5f,ctx,meshes);
    check(fonts.GetVersion(face)>version,"new glyph invalidates geometry");
    meshes.clear();plain.clear();fonts.ReleaseFontResources();
    return errors;
}
