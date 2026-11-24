#pragma once
// lip-sync: viseme tracks for talking faces (presentation only, float math). Two sources:
// - Audio: analyseSpeech splits mono PCM into frames, measures loudness (RMS), zero-crossing rate and a high/low band
//   energy ratio, and picks a viseme per frame: silence, closed lips (Mbp) on short dips between voiced frames,
//   fricatives (Fv) for noisy high-band frames, open vowels (Aa / Oh / Ee) by loudness and brightness.
// - Text: visemesFromText maps letters and digraphs to visemes and spreads them over the line's duration (dialogue
//   without recorded audio, or as a fallback).
// Both return keys (time, viseme, weight) smoothed with attack/release; sampleVisemes gives the weights at a time for
// the face rig. The Viseme order matches engine/character/vivid/Face.h (Silence, Aa, Ee, Oh, Fv, Mbp): pass
// `static_cast<vivid::Viseme>(key.viseme)` to Face::setViseme.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace aiverse::lib::animation {
enum class LipViseme:uint8_t {Silence,Aa,Ee,Oh,Fv,Mbp};
inline constexpr int kLipVisemes=6;
struct VisemeKey {float time=0;LipViseme viseme=LipViseme::Silence;float weight=0;};
struct LipSyncConfig {float frameSeconds=0.02f,silenceRms=0.02f,loudRms=0.2f,fricativeZcr=0.25f,brightRatio=1.2f,darkRatio=0.6f,closeGapSeconds=0.06f,attack=0.04f,release=0.08f;};
inline bool valid(const LipSyncConfig& c){return c.frameSeconds>0.001f&&c.frameSeconds<0.5f&&c.silenceRms>=0&&c.loudRms>c.silenceRms&&c.fricativeZcr>0&&c.attack>0&&c.release>0;}

inline std::vector<VisemeKey> analyseSpeech(std::span<const float> pcm,int sampleRate,const LipSyncConfig& c={}){
    std::vector<VisemeKey> keys;if(!valid(c)||sampleRate<=0||pcm.empty())return keys;
    auto frame=std::max<std::size_t>(16,std::size_t(c.frameSeconds*float(sampleRate)));
    std::vector<LipViseme> v;std::vector<float> loud;float lowState=0;
    for(std::size_t start=0;start<pcm.size();start+=frame){auto end=std::min(pcm.size(),start+frame);double sum=0,lowE=0,highE=0;int zc=0;
        for(std::size_t i=start;i<end;++i){float x=std::isfinite(pcm[i])?pcm[i]:0.f;sum+=double(x)*x;lowState+=0.1f*(x-lowState);float hi=x-lowState;lowE+=double(lowState)*lowState;highE+=double(hi)*hi;
            if(i>start&&((pcm[i-1]<0)!=(x<0)))++zc;}
        auto n=double(end-start);float rms=float(std::sqrt(sum/n)),zcr=float(zc)/float(n);float ratio=lowE>1e-12?float(highE/lowE):10.f;
        LipViseme pick=LipViseme::Silence;
        if(rms<c.silenceRms)pick=LipViseme::Silence;else if(zcr>=c.fricativeZcr&&ratio>c.brightRatio)pick=LipViseme::Fv;
        else if(ratio>=c.brightRatio)pick=LipViseme::Ee;else if(ratio<=c.darkRatio)pick=LipViseme::Oh;else pick=LipViseme::Aa;
        v.push_back(pick);loud.push_back(std::clamp((rms-c.silenceRms)/(c.loudRms-c.silenceRms),0.f,1.f));}
    // short silences between voiced frames are closed lips (plosives b/p/m)
    auto gap=std::max<std::size_t>(1,std::size_t(c.closeGapSeconds/c.frameSeconds));
    for(std::size_t i=1;i+1<v.size();){if(v[i]!=LipViseme::Silence){++i;continue;}std::size_t j=i;while(j<v.size()&&v[j]==LipViseme::Silence)++j;
        if(j<v.size()&&j-i<=gap&&v[i-1]!=LipViseme::Silence)for(std::size_t k=i;k<j;++k){v[k]=LipViseme::Mbp;loud[k]=1;}i=j;}
    float t=0,w=0;LipViseme cur=LipViseme::Silence;
    for(std::size_t i=0;i<v.size();++i){float target=v[i]==LipViseme::Silence?0.f:std::max(0.3f,loud[i]);
        if(v[i]!=cur&&!keys.empty()&&keys.back().viseme==cur&&w>0){keys.push_back({t,cur,0});w=0;}
        float rate=target>w?c.frameSeconds/c.attack:c.frameSeconds/c.release;w+=(target-w)*std::min(1.f,rate);cur=v[i];
        if(keys.empty()||keys.back().viseme!=cur||std::abs(keys.back().weight-w)>0.05f)keys.push_back({t,cur,w});t+=c.frameSeconds;}
    keys.push_back({t,LipViseme::Silence,0});return keys;
}
inline LipViseme letterViseme(char a,char b,int& consumed){
    consumed=1;a=char(std::tolower((unsigned char)a));b=char(std::tolower((unsigned char)b));
    if((a=='t'&&b=='h')||(a=='s'&&b=='h')||(a=='c'&&b=='h')){consumed=2;return LipViseme::Fv;}
    if((a=='o'&&b=='o')||(a=='o'&&b=='u')){consumed=2;return LipViseme::Oh;}
    if((a=='e'&&b=='e')||(a=='e'&&b=='a')){consumed=2;return LipViseme::Ee;}
    switch(a){case 'a':return LipViseme::Aa;case 'e':case 'i':case 'y':return LipViseme::Ee;case 'o':case 'u':case 'w':case 'q':return LipViseme::Oh;
    case 'f':case 'v':case 's':case 'z':case 'c':case 'x':case 'j':return LipViseme::Fv;case 'm':case 'b':case 'p':return LipViseme::Mbp;
    case ' ':case ',':case '.':case '!':case '?':case ';':case ':':return LipViseme::Silence;default:return LipViseme::Aa;}   // other consonants: half-open
}
// Latin text; letters are weighted (vowels longer), punctuation adds pauses. duration in seconds.
inline std::vector<VisemeKey> visemesFromText(std::string_view text,float duration,const LipSyncConfig& c={}){
    std::vector<VisemeKey> keys;if(!(duration>0)||!valid(c))return keys;struct Seg{LipViseme v;float w;};std::vector<Seg> segs;
    for(std::size_t i=0;i<text.size();){int n=1;auto v=letterViseme(text[i],i+1<text.size()?text[i+1]:'\0',n);
        float w=v==LipViseme::Silence?(text[i]==' '?0.5f:2.f):(v==LipViseme::Aa||v==LipViseme::Ee||v==LipViseme::Oh?1.5f:1.f);
        if(!segs.empty()&&segs.back().v==v)segs.back().w+=w;else segs.push_back({v,w});i+=std::size_t(n);}
    float total=0;for(auto& s:segs)total+=s.w;if(total<=0)return keys;float t=0;
    for(auto& s:segs){keys.push_back({t,s.v,s.v==LipViseme::Silence?0.f:1.f});t+=duration*s.w/total;}
    keys.push_back({duration,LipViseme::Silence,0});return keys;
}
// Blend weights of every viseme at `time`: a key holds, then blends into the next one over its last 60 ms.
inline std::array<float,kLipVisemes> sampleVisemes(std::span<const VisemeKey> keys,float time){
    std::array<float,kLipVisemes> w{};if(keys.empty()){w[0]=1;return w;}
    auto it=std::upper_bound(keys.begin(),keys.end(),time,[](float t,const VisemeKey& k){return t<k.time;});
    if(it==keys.begin()){w[std::size_t(keys.front().viseme)]=keys.front().weight;return w;}
    auto a=*(it-1);if(it==keys.end()){w[std::size_t(a.viseme)]=a.weight;return w;}auto b=*it;
    float span=std::min(b.time-a.time,0.06f);float f=span>0?(time-(b.time-span))/span:1.f;f=std::clamp(f,0.f,1.f);   // hold, then blend in the last 60 ms
    w[std::size_t(a.viseme)]+=a.weight*(1-f);w[std::size_t(b.viseme)]+=b.weight*f;float sum=0;for(float x:w)sum+=x;if(sum<1)w[0]+=1-sum;return w;
}
}
