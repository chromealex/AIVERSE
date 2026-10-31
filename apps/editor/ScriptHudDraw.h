#pragma once
// Presentation for data-defined HUD panels. No gameplay rules live here.
#include "modules/rts/view/ScriptHud.h"
#include <cmath>
#include <imgui.h>
#include "engine/render/TextureCache.h"

namespace rts::editor {
struct HudHit { bool hovered=false, modal=false; int ability=-1; bool point=false; };
inline HudHit drawScriptHud(const std::vector<game::ScriptHudPanel>& panels, ImVec2 origin, ImVec2 size, render::TextureCache& textures, const std::string& base, int selectedAbility=-1) {
    HudHit hit;
    const bool modal=std::any_of(panels.begin(),panels.end(),[](const auto& p){return p.layout=="cards";});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,14);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,9);
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,.72f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14,12));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.045f,.11f,.15f,.97f));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.28f,.33f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.19f,.42f,.46f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.28f,.52f,.52f,1));
    for(size_t pi=0;pi<panels.size();++pi) {
        const auto& panel=panels[pi];const bool cards=panel.layout=="cards",support=panel.layout=="support",status=panel.layout=="status",top=panel.layout=="top";
        float w=std::min(cards?880.f:status?310.f:260.f,size.x-24);
        int count=0;for(const auto& item:panel.items)if(!item.description.empty())++count;
        const int columns=std::max(1,std::min(count,int((w-28)/185)));
        const float cardH=330.f;
        float h=cards?80+std::ceil(float(count)/columns)*(cardH+10):support?230:(status||top)?110:240;
        h=std::min(h,size.y-80);
        ImVec2 pos=cards?ImVec2(origin.x+(size.x-w)/2,origin.y+(size.y-h)/2):
            support?ImVec2(origin.x+size.x-w-12,origin.y+size.y-h-54):top?ImVec2(origin.x+size.x-w-12,origin.y+12):ImVec2(origin.x+12,origin.y+12);
        if(cards){hit.modal=true;ImGui::GetWindowDrawList()->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),IM_COL32(5,18,27,135));}
        ImGui::SetCursorScreenPos(pos);ImGui::PushID(int(pi));
        ImGui::BeginChild("script_panel",ImVec2(w,h),true);
        ImGui::BeginDisabled(modal&&!cards);
        ImGui::TextColored(ImVec4(.98f,.81f,.43f,1),"%s",panel.title.c_str());
        int ci=0;
        for(size_t ii=0;ii<panel.items.size();++ii) {
            const auto& item=panel.items[ii];ImGui::PushID(int(ii));
            const bool card=cards&&!item.description.empty();
            if(card){
                if(ci%columns)ImGui::SameLine();
                const float cw=(w-28-(columns-1)*10)/columns;
                ImVec2 p=ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("choice",ImVec2(cw,cardH));
                bool hover=ImGui::IsItemHovered();
                if(ImGui::IsItemClicked()&&item.enabled){hit.ability=item.ability;hit.point=item.pointTarget;}
                auto* dl=ImGui::GetWindowDrawList();
                ImU32 gold=IM_COL32(235,192,107,item.enabled?255:130);
                auto frame=item.frameImage.empty()?render::TextureRef{}:textures.resolve(item.frameImage,false,base);
                const bool framed=frame.valid();
                if(framed){
                    const float du=(frame.u1-frame.u0)/item.frameColumns;
                    dl->AddImage(ImTextureRef(ImTextureID(frame.tex.id)),p,ImVec2(p.x+cw,p.y+cardH),
                        ImVec2(frame.u0+du*item.frameIndex,frame.v0),ImVec2(frame.u0+du*(item.frameIndex+1),frame.v1),
                        IM_COL32(255,255,255,item.enabled?255:160));
                    if(hover)dl->AddRect(p,ImVec2(p.x+cw,p.y+cardH),gold,10.f,2.f);
                }else{
                    dl->AddRectFilled(p,ImVec2(p.x+cw,p.y+cardH),hover?IM_COL32(34,80,89,255):IM_COL32(21,49,64,255),12);
                    dl->AddRect(p,ImVec2(p.x+cw,p.y+cardH),gold,12,0,hover?3:1.5f);
                }
                auto tex=item.image.empty()?render::TextureRef{}:textures.resolve(item.image,false,base);
                const float iy=framed?cardH*.27f:44.f, ir=framed?46.f:32.f;
                if(tex.valid())dl->AddImage(ImTextureRef(ImTextureID(tex.tex.id)),ImVec2(p.x+cw/2-ir,p.y+iy-ir),ImVec2(p.x+cw/2+ir,p.y+iy+ir),ImVec2(tex.u0,tex.v0),ImVec2(tex.u1,tex.v1));
                else dl->AddCircleFilled(ImVec2(p.x+cw/2,p.y+iy),25,gold);
                auto* font=ImGui::GetFont();float fs=ImGui::GetFontSize();
                const float inset=framed?cw*.13f:12.f, wrap=cw-2*inset;
                const float titleSize=std::min(fs,14.f);
                auto title=font->CalcTextSizeA(titleSize,wrap,wrap,item.label.c_str());
                dl->AddText(font,titleSize,ImVec2(p.x+(cw-title.x)/2,p.y+(framed?cardH*.54f-title.y/2:80)),IM_COL32(255,244,219,255),item.label.c_str(),nullptr,wrap);
                dl->AddText(font,std::min(fs,13.f),ImVec2(p.x+inset,p.y+(framed?cardH*.61f:122)),framed?IM_COL32(56,48,40,255):IM_COL32(219,234,231,255),item.description.c_str(),nullptr,wrap);
                ++ci;
            }else if(item.ability>=0){
                ImGui::BeginDisabled(!item.enabled);
                const bool active=item.pointTarget&&item.ability==selectedAbility;
                if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.28f,.52f,.52f,1));
                std::string label=item.label+(item.status.empty()?"":"\n"+item.status);
                const bool clicked=ImGui::Button(label.c_str(),ImVec2(-1,support&&item.pointTarget?65:28));
                if(item.enabled&&(item.pointTarget?ImGui::IsItemActivated():clicked)){hit.ability=item.ability;hit.point=item.pointTarget;}
                if(active)ImGui::PopStyleColor();
                ImGui::EndDisabled();
                const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
                if(support&&item.pointTarget&&!item.image.empty()){
                    auto tex=textures.resolve(item.image,false,base);
                    auto p=ImGui::GetItemRectMin();
                    if(tex.valid())ImGui::GetWindowDrawList()->AddImage(ImTextureRef(ImTextureID(tex.tex.id)),ImVec2(p.x+8,p.y+16),ImVec2(p.x+40,p.y+48),ImVec2(tex.u0,tex.v0),ImVec2(tex.u1,tex.v1));
                }
                if(item.progress>=0)ImGui::ProgressBar(item.progress,ImVec2(-1,4),"");
                if(!item.description.empty()&&hovered)ImGui::SetTooltip("%s",item.description.c_str());
            }else ImGui::TextWrapped("%s",item.label.c_str());
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        hit.hovered|=ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::EndChild();ImGui::PopID();
    }
    ImGui::PopStyleColor(4);ImGui::PopStyleVar(4);return hit;
}
}
