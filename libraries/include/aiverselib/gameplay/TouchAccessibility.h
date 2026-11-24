#pragma once
// touch-accessibility: touch controls and accessibility settings for the host input and UI layers (presentation side,
// float math; the simulation receives ordinary commands from InputFlow actions).
// - Touch: virtual sticks (fixed or floating, dead zone, radius), buttons with hit circles and hold/toggle mode,
//   gesture recognition (tap, double tap, long press, swipe with direction, pinch zoom, two-finger rotate, pan) from
//   raw touch points with ids. Output: stick vectors, button states and gesture events per frame.
// - Accessibility: colour-blind simulation/compensation matrices (protanopia, deuteranopia, tritanopia), text and UI
//   scale, subtitle style, reduced motion (camera shake / flash scale), hold-to-toggle, aim assist strength, and an
//   input remap table with conflict detection. validate() names the first invalid field.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace aiverse::lib::gameplay::touch {
struct Touch {int64_t id=0;float x=0,y=0;bool down=true;};                        // screen pixels; down=false on release frame
struct Stick {std::string action;float centerX=0,centerY=0,radius=80,deadZone=0.15f;bool floating=true;float areaX0=0,areaY0=0,areaX1=0,areaY1=0;};   // area: where a floating stick may start
struct Button {std::string action;float x=0,y=0,radius=48;bool toggle=false;};
struct Layout {std::vector<Stick> sticks;std::vector<Button> buttons;float doubleTapSeconds=0.3f,longPressSeconds=0.5f,swipeDistance=80,tapSlop=12;};
enum class GestureKind:uint8_t {Tap,DoubleTap,LongPress,Swipe,Pinch,Rotate,Pan};
struct Gesture {GestureKind kind{};float x=0,y=0,dx=0,dy=0,scale=1,angle=0;};
struct StickValue {std::string action;float x=0,y=0;bool active=false;};
struct ButtonValue {std::string action;bool held=false,pressed=false,released=false;};
struct TouchFrame {std::vector<StickValue> sticks;std::vector<ButtonValue> buttons;std::vector<Gesture> gestures;};
struct Tracked {float startX=0,startY=0,x=0,y=0,start=0;int owner=-1;bool stick=false,longFired=false;};    // owner: stick/button index
struct TouchState {std::map<int64_t,Tracked> touches;std::vector<float> stickCX,stickCY;std::vector<bool> toggled,wasHeld;float lastTapTime=-10,lastTapX=0,lastTapY=0,
    pinchStart=0,rotateStart=0;float time=0;};
inline bool inside(float x,float y,float cx,float cy,float r){return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r;}
inline TouchFrame updateTouch(const Layout& l,TouchState& s,std::span<const Touch> touches,float dt){
    TouchFrame f;s.time+=std::max(0.f,dt);
    if(s.stickCX.size()!=l.sticks.size()){s.stickCX.resize(l.sticks.size());s.stickCY.resize(l.sticks.size());for(std::size_t i=0;i<l.sticks.size();++i){s.stickCX[i]=l.sticks[i].centerX;s.stickCY[i]=l.sticks[i].centerY;}}
    if(s.toggled.size()!=l.buttons.size()){s.toggled.assign(l.buttons.size(),false);s.wasHeld.assign(l.buttons.size(),false);}
    std::vector<bool> held(l.buttons.size(),false);
    for(auto& t:touches){auto it=s.touches.find(t.id);
        if(it==s.touches.end()&&t.down){Tracked tr{t.x,t.y,t.x,t.y,s.time};
            for(std::size_t i=0;i<l.buttons.size()&&tr.owner<0;++i)if(inside(t.x,t.y,l.buttons[i].x,l.buttons[i].y,l.buttons[i].radius))tr.owner=int(i);
            for(std::size_t i=0;i<l.sticks.size()&&tr.owner<0;++i){auto& st=l.sticks[i];bool hit=st.floating?(t.x>=st.areaX0&&t.x<=st.areaX1&&t.y>=st.areaY0&&t.y<=st.areaY1):inside(t.x,t.y,st.centerX,st.centerY,st.radius);
                bool busy=false;for(auto& [id,o]:s.touches)busy|=o.stick&&o.owner==int(i);
                if(hit&&!busy){tr.owner=int(i);tr.stick=true;if(st.floating){s.stickCX[i]=t.x;s.stickCY[i]=t.y;}}}
            it=s.touches.emplace(t.id,tr).first;}
        if(it==s.touches.end())continue;auto& tr=it->second;tr.x=t.x;tr.y=t.y;
        if(!t.down){                                                                  // release: taps and swipes for free touches
            if(tr.owner<0){float dx=tr.x-tr.startX,dy=tr.y-tr.startY,d=std::sqrt(dx*dx+dy*dy),dur=s.time-tr.start;
                if(d>=l.swipeDistance)f.gestures.push_back({GestureKind::Swipe,tr.x,tr.y,dx,dy});
                else if(d<=l.tapSlop&&dur<l.longPressSeconds){bool dbl=s.time-s.lastTapTime<=l.doubleTapSeconds&&inside(tr.x,tr.y,s.lastTapX,s.lastTapY,l.tapSlop*3);
                    f.gestures.push_back({dbl?GestureKind::DoubleTap:GestureKind::Tap,tr.x,tr.y});s.lastTapTime=dbl?-10:s.time;s.lastTapX=tr.x;s.lastTapY=tr.y;}}
            s.touches.erase(it);continue;}
        if(tr.owner<0&&!tr.longFired&&s.time-tr.start>=l.longPressSeconds&&inside(tr.x,tr.y,tr.startX,tr.startY,l.tapSlop)){tr.longFired=true;f.gestures.push_back({GestureKind::LongPress,tr.x,tr.y});}
        if(!tr.stick&&tr.owner>=0)held[std::size_t(tr.owner)]=true;
    }
    for(std::size_t i=0;i<l.sticks.size();++i){StickValue v{l.sticks[i].action};
        for(auto& [id,tr]:s.touches)if(tr.stick&&tr.owner==int(i)){float dx=(tr.x-s.stickCX[i])/l.sticks[i].radius,dy=(tr.y-s.stickCY[i])/l.sticks[i].radius;float len=std::sqrt(dx*dx+dy*dy);
            if(len>1){dx/=len;dy/=len;len=1;}if(len>=l.sticks[i].deadZone){float k=(len-l.sticks[i].deadZone)/(1-l.sticks[i].deadZone)/len;v.x=dx*k;v.y=-dy*k;}v.active=true;}
        if(!v.active&&l.sticks[i].floating){s.stickCX[i]=l.sticks[i].centerX;s.stickCY[i]=l.sticks[i].centerY;}f.sticks.push_back(v);}
    for(std::size_t i=0;i<l.buttons.size();++i){bool h=held[i];ButtonValue b{l.buttons[i].action};bool pressed=h&&!s.wasHeld[i],released=!h&&s.wasHeld[i];
        if(l.buttons[i].toggle){if(pressed)s.toggled[i]=!s.toggled[i];b.held=s.toggled[i];b.pressed=pressed&&s.toggled[i];b.released=pressed&&!s.toggled[i];}
        else{b.held=h;b.pressed=pressed;b.released=released;}s.wasHeld[i]=h;f.buttons.push_back(b);}
    // two free touches: pinch / rotate / pan
    std::vector<const Tracked*> free;for(auto& [id,tr]:s.touches)if(tr.owner<0)free.push_back(&tr);
    if(free.size()==2){float dx=free[1]->x-free[0]->x,dy=free[1]->y-free[0]->y,d=std::sqrt(dx*dx+dy*dy),a=std::atan2(dy,dx);
        if(s.pinchStart<=0){s.pinchStart=d;s.rotateStart=a;}else{f.gestures.push_back({GestureKind::Pinch,(free[0]->x+free[1]->x)/2,(free[0]->y+free[1]->y)/2,0,0,d/s.pinchStart});
            f.gestures.push_back({GestureKind::Rotate,0,0,0,0,1,std::remainder(a-s.rotateStart,6.2831853f)});}}
    else{s.pinchStart=0;if(free.size()==1&&!inside(free[0]->x,free[0]->y,free[0]->startX,free[0]->startY,l.tapSlop))f.gestures.push_back({GestureKind::Pan,free[0]->x,free[0]->y,free[0]->x-free[0]->startX,free[0]->y-free[0]->startY});}
    return f;
}
}
namespace aiverse::lib::gameplay::accessibility {
enum class ColorVision:uint8_t {Normal,Protanopia,Deuteranopia,Tritanopia};
using Mat3=std::array<float,9>;
// Simulation matrices (linear RGB, Machado et al. 2009, severity 1.0).
inline Mat3 simulationMatrix(ColorVision v){switch(v){
    case ColorVision::Protanopia:return {0.152286f,1.052583f,-0.204868f,0.114503f,0.786281f,0.099216f,-0.003882f,-0.048116f,1.051998f};
    case ColorVision::Deuteranopia:return {0.367322f,0.860646f,-0.227968f,0.280085f,0.672501f,0.047413f,-0.011820f,0.042940f,0.968881f};
    case ColorVision::Tritanopia:return {1.255528f,-0.076749f,-0.178779f,-0.078411f,0.930809f,0.147602f,0.004733f,0.691367f,0.303900f};
    default:return {1,0,0,0,1,0,0,0,1};}}
// Daltonize: shift the colour error the viewer cannot see into channels they can (strength 0..1).
inline std::array<float,3> compensate(ColorVision v,std::array<float,3> rgb,float strength=1){
    if(v==ColorVision::Normal)return rgb;auto m=simulationMatrix(v);std::array<float,3> sim{m[0]*rgb[0]+m[1]*rgb[1]+m[2]*rgb[2],m[3]*rgb[0]+m[4]*rgb[1]+m[5]*rgb[2],m[6]*rgb[0]+m[7]*rgb[1]+m[8]*rgb[2]};
    std::array<float,3> err{rgb[0]-sim[0],rgb[1]-sim[1],rgb[2]-sim[2]};std::array<float,3> shift{0,0.7f*err[0]+err[1],0.7f*err[0]+err[2]};
    std::array<float,3> out;for(int i=0;i<3;++i)out[std::size_t(i)]=std::clamp(rgb[std::size_t(i)]+shift[std::size_t(i)]*strength,0.f,1.f);return out;}
struct SubtitleStyle {bool enabled=true,speakerNames=true,background=true;float scale=1,backgroundOpacity=0.6f;};
struct Settings {ColorVision colorVision=ColorVision::Normal;float colorStrength=1,textScale=1,uiScale=1,cameraShake=1,flashIntensity=1,aimAssist=0;
    bool reducedMotion=false,holdToToggle=false,monoAudio=false;SubtitleStyle subtitles{};std::map<std::string,std::vector<std::string>> bindings;};   // action -> keys/buttons
inline std::optional<std::string> validate(const Settings& s){
    auto in=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(uint8_t(s.colorVision)>3)return "colorVision";if(!in(s.colorStrength,0,1))return "colorStrength";if(!in(s.textScale,0.5f,3))return "textScale";if(!in(s.uiScale,0.5f,3))return "uiScale";
    if(!in(s.cameraShake,0,1))return "cameraShake";if(!in(s.flashIntensity,0,1))return "flashIntensity";if(!in(s.aimAssist,0,1))return "aimAssist";
    if(!in(s.subtitles.scale,0.5f,3))return "subtitles.scale";if(!in(s.subtitles.backgroundOpacity,0,1))return "subtitles.backgroundOpacity";
    for(auto& [a,keys]:s.bindings){if(a.empty())return "bindings";for(auto& k:keys)if(k.empty())return "bindings."+a;}
    return std::nullopt;
}
// Keys bound to more than one action: (key, actions) in key order.
inline std::vector<std::pair<std::string,std::vector<std::string>>> bindingConflicts(const Settings& s){
    std::map<std::string,std::vector<std::string>> byKey;for(auto& [a,keys]:s.bindings)for(auto& k:keys)byKey[k].push_back(a);
    std::vector<std::pair<std::string,std::vector<std::string>>> out;for(auto& [k,acts]:byKey)if(acts.size()>1)out.push_back({k,acts});return out;}
inline bool rebind(Settings& s,const std::string& action,std::size_t slot,const std::string& key){if(action.empty()||key.empty())return false;auto& v=s.bindings[action];if(slot>v.size()||slot>7)return false;
    if(slot==v.size())v.push_back(key);else v[slot]=key;return true;}
// Effective camera shake / flash after reduced motion.
inline float shakeScale(const Settings& s){return s.reducedMotion?0.f:s.cameraShake;}
inline float flashScale(const Settings& s){return s.reducedMotion?std::min(s.flashIntensity,0.3f):s.flashIntensity;}
}
