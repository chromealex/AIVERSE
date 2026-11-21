#pragma once
// The race HUD of every client (any project with a map "race", docs/races.md): start countdown, place, lap, the
// standings, "wrong way" and the result. Reads only a lib::genre::RaceView (presentation never writes the sim).
#include "engine/i18n/Localization.h"
#include "libraries/include/aiverselib/genre/RaceSession.h"
#include "third_party/imgui/imgui.h"
#include <algorithm>
#include <cfloat>
#include <string>
namespace aiverse::ui {
inline void drawRaceHud(const i18n::Localization& lang,const lib::genre::RaceView& v,uint32_t tick,uint32_t tickRate){
    if(!v.enabled)return;
    auto* draw=ImGui::GetForegroundDrawList();auto size=ImGui::GetIO().DisplaySize;float w=size.x,h=size.y;
    auto big=[&](const std::string& text,float y,ImU32 color,float scale){
        auto* font=ImGui::GetFont();float px=ImGui::GetFontSize()*scale;auto ext=font->CalcTextSizeA(px,FLT_MAX,0,text.c_str());
        draw->AddRectFilled({w/2-ext.x/2-14,y-6},{w/2+ext.x/2+14,y+ext.y+6},IM_COL32(10,14,18,170),6);
        draw->AddText(font,px,{w/2-ext.x/2,y},color,text.c_str());};
    // Place and lap (top right).
    if(v.place){
        auto place=lang.tr("race.place",i18n::Args().num("place",v.place).num("racers",v.racers));
        auto lap=lang.tr("race.lap",i18n::Args().num("lap",std::max<int>(1,v.lap)).num("laps",v.laps));
        auto* font=ImGui::GetFont();float px=ImGui::GetFontSize()*2.2f;auto e1=font->CalcTextSizeA(px,FLT_MAX,0,place.c_str());
        draw->AddRectFilled({w-e1.x-40,14},{w-14,14+px*2+18},IM_COL32(10,14,18,170),6);
        draw->AddText(font,px,{w-e1.x-27,20},IM_COL32(255,214,90,255),place.c_str());
        draw->AddText(font,px*0.55f,{w-e1.x-27,24+px},IM_COL32(220,228,236,255),lap.c_str());
    }
    // Standings (left, under the status window).
    float y=h*0.32f;
    for(size_t i=0;i<v.order.size()&&i<8;++i){
        auto line=lang.tr("race.row",i18n::Args().num("place",int(i+1)).num("slot",int(v.order[i])));
        bool own=v.order[i]==v.racer;draw->AddText({18,y},own?IM_COL32(255,214,90,255):IM_COL32(200,208,216,255),line.c_str());y+=ImGui::GetFontSize()+3;
    }
    // Centre messages: countdown, go, wrong way, results.
    auto rate=std::max(1u,tickRate);
    if(v.phase==0&&v.countdownTicks>0)big(std::to_string((v.countdownTicks+rate-1)/rate),h*0.28f,IM_COL32(255,236,120,255),4.f);
    else if(v.phase==1&&v.countdownTicks==0&&v.lap<=1&&v.checkpoint==0&&!v.finished&&tick%(rate*2)<rate)big(lang.tr("race.go"),h*0.28f,IM_COL32(120,255,140,255),3.f);
    if(v.wrongWay&&!v.finished)big(lang.tr("race.wrongWay"),h*0.4f,IM_COL32(255,90,70,255),2.4f);
    if(v.phase==2){
        auto text=v.winner==v.racer&&v.racer?lang.tr("race.won"):lang.tr("race.result",i18n::Args().num("place",v.place).num("racers",v.racers));
        big(text,h*0.22f,IM_COL32(255,236,120,255),2.6f);
    }else if(v.finished)big(lang.tr("race.finished",i18n::Args().num("place",v.place)),h*0.22f,IM_COL32(255,236,120,255),2.2f);
}
}
