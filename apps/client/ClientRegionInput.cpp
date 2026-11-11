#include "GameClient.h"
#include "RegionSettings.h"
namespace aiverse::client {
bool GameClient::regionEvent(const platform::Event& e,bool uiWants){
    using namespace platform;
    regionSwipe_.configure(manifest_);if(!regionSwipe_.enabled||spectator_)return false;
    regionSwipe_.refresh(*sim_,mod(),human_);
    if(regionTick_==UINT32_MAX)regionSwipe_.ratio=loadRegionRatio();
    auto point=[&](float x,float y){float3 p{-1,0,-1};groundAt(x,y,p);return p;};
    entityActions_.refresh(*sim_,mod(),human_,manifest_["entityActionControls"]);
    const bool press=(e.type==EventType::MouseDown&&e.button==0) || e.type==EventType::TouchBegin;
    if(entityPointer_){
        if(e.type==EventType::FocusLost || e.type==EventType::TouchCancel ||
           (e.type==EventType::KeyDown&&e.key==Key::Escape) ||
           (e.type==EventType::MouseDown&&e.button!=0) ||
           (e.type==EventType::TouchBegin&&e.touchId!=entityTouch_)){
            entityActions_.cancel();entityPointer_=false;return true;
        }
        if((e.type==EventType::TouchMove||e.type==EventType::TouchEnd)&&e.touchId!=entityTouch_)return true;
        if(e.type==EventType::MouseMove||e.type==EventType::TouchMove||e.type==EventType::MouseUp||e.type==EventType::TouchEnd){
            mouseX_=e.x;mouseY_=e.y;
            auto hover=e;hover.type=EventType::MouseMove;ui_.handleEvent(hover);
            if(e.type==EventType::MouseUp||e.type==EventType::TouchEnd){
                auto command=entityActions_.release(point(e.x,e.y),!ui_.pointerOverUi(),std::hypot(e.x-entityStartX_,e.y-entityStartY_)>=6);
                if(command)issue(*command);
                entityPointer_=false;uiOwnsPointer_=false;
            }
        }
        return true;
    }
    if(press&&!uiWants&&mode_==Mode::Normal&&entityActions_.press(cam_,float(windowWidth()),float(windowHeight()),e.x,e.y)){
        regionSwipe_.cancel();entityPointer_=true;entityStartX_=e.x;entityStartY_=e.y;
        entityTouch_=e.type==EventType::TouchBegin?e.touchId:-1;return true;
    }
    auto finish=[&]{
        if(std::hypot(e.x-downX_,e.y-downY_)<6)regionSwipe_.cancelFlow(*sim_,mod(),point(e.x,e.y),[&](Command c){issue(c);});
        regionSwipe_.cursor=point(e.x,e.y);regionSwipe_.release(time_,[&](Command c){issue(c);});};
    switch(e.type){
    case EventType::MouseDown:
        if(uiWants)return true;
        if(mode_!=Mode::Normal)return false;
        if(e.button==0){downX_=e.x;downY_=e.y;regionSwipe_.begin(point(e.x,e.y));}
        else {panning_=true;downX_=e.x;downY_=e.y;regionSwipe_.cancel();}return true;
    case EventType::MouseMove:
        if(panning_){float3 a,b;
            if(cam_.screenToGround(downX_,downY_,float(platform::windowWidth()),float(platform::windowHeight()),a)&&cam_.screenToGround(e.x,e.y,float(platform::windowWidth()),float(platform::windowHeight()),b))cam_.target+=a-b;
            downX_=e.x;downY_=e.y;
        }else regionSwipe_.move(point(e.x,e.y));return true;
    case EventType::MouseUp:
        if(e.button!=0){panning_=false;return true;}
        if(e.button==0){
            if(!uiOwnsPointer_&&std::hypot(e.x-downX_,e.y-downY_)<6&&regionSwipe_.region(point(e.x,e.y).x,point(e.x,e.y).z)<0){
                if(time_-lastClickTime_<.3)regionSwipe_.fit(cam_,float(platform::windowWidth()),float(platform::windowHeight()),true);
                lastClickTime_=time_;
            }
            finish();
        }return true;
    case EventType::MouseWheel:
        if(!uiWants){auto before=point(e.x,e.y);cam_.distance=std::clamp(cam_.distance*std::exp(-.14f*e.wheelY),cam_.minDistance,cam_.maxDistance);auto after=point(e.x,e.y);if(before.x>=0&&after.x>=0)cam_.target+=before-after;}return true;
    case EventType::TouchBegin:
        if(uiWants&&touches_.empty())return true;
        touches_[e.touchId]={e.x,e.y};mouseX_=e.x;mouseY_=e.y;
        if(touches_.size()==1){downX_=e.x;downY_=e.y;touchMoved_=false;if(mode_==Mode::Normal)regionSwipe_.begin(point(e.x,e.y));}
        else{regionSwipe_.cancel();auto it=touches_.begin();auto a=it->second,b=(++it)->second;pinchDist_=std::hypot(a.first-b.first,a.second-b.second);downX_=(a.first+b.first)*.5f;downY_=(a.second+b.second)*.5f;touchMoved_=true;}return true;
    case EventType::TouchMove:
        if(!touches_.count(e.touchId))return true;
        touches_[e.touchId]={e.x,e.y};mouseX_=e.x;mouseY_=e.y;
        if(touches_.size()==1){
            if(mode_==Mode::Skill){if(std::hypot(e.x-downX_,e.y-downY_)>8)touchMoved_=true;}
            else regionSwipe_.move(point(e.x,e.y));
        }
        else if(touches_.size()==2){auto it=touches_.begin();auto a=it->second,b=(++it)->second;float d=std::hypot(a.first-b.first,a.second-b.second),cx=(a.first+b.first)*.5f,cy=(a.second+b.second)*.5f;auto before=point(downX_,downY_);
            if(d>1&&pinchDist_>1)cam_.distance=std::clamp(cam_.distance*pinchDist_/d,cam_.minDistance,cam_.maxDistance);
            auto after=point(cx,cy);if(cam_.distance<cam_.maxDistance&&before.x>=0&&after.x>=0)cam_.target+=before-after;
            pinchDist_=d;downX_=cx;downY_=cy;}return true;
    case EventType::TouchEnd: case EventType::TouchCancel:
        if(!touches_.count(e.touchId))return true;
        if(mode_==Mode::Skill){
            if(touches_.size()==1&&!touchMoved_&&e.type==EventType::TouchEnd){auto click=e;click.type=EventType::MouseDown;click.button=0;toolMouseDown(click);}
            touches_.erase(e.touchId);regionSwipe_.cancel();return true;
        }
        if(touches_.size()==1&&!touchMoved_&&e.type==EventType::TouchEnd){
            if(regionSwipe_.region(point(e.x,e.y).x,point(e.x,e.y).z)<0&&std::hypot(e.x-downX_,e.y-downY_)<8){if(time_-lastClickTime_<.3)regionSwipe_.fit(cam_,float(platform::windowWidth()),float(platform::windowHeight()),true);lastClickTime_=time_;}finish();
        }else regionSwipe_.cancel();touches_.erase(e.touchId);return true;
    default:return false;
    }
}
}
