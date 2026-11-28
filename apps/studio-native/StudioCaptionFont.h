#pragma once
#include "third_party/stb/stb_truetype.h"
#include <RmlUi/Core/StringUtilities.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
// Small labels share the UI's TrueType rasterizer. A glyph's horizontal runs
// can be submitted to the native Scene overlay pass, including Scene exports.
class StudioCaptionFont {
public:
    struct Run { int x,y,width; unsigned alpha; };
    struct Glyph { float advance=0; std::vector<Run> runs; };
    struct Item { float x; const Glyph* glyph; };
    struct Layout { float advance=0; std::vector<Item> glyphs; };

    explicit StudioCaptionFont(const std::string& assets) {
        if(assets.empty())return;
        std::ifstream file(std::filesystem::path(assets)/"fonts"/"ui.ttf",std::ios::binary|std::ios::ate);
        if(!file)return;
        const auto size=file.tellg();
        if(size<=0 || size>16*1024*1024)return;
        bytes_.resize(size_t(size));
        file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes_.data()),size) || !stbtt_InitFont(&font_,bytes_.data(),0))return;
        scale_=stbtt_ScaleForMappingEmToPixels(&font_,16);
        int ascent,descent;
        stbtt_GetFontVMetrics(&font_,&ascent,&descent,nullptr);
        baseline_=(ascent+descent)*scale_*.5f;
    }
    StudioCaptionFont(const StudioCaptionFont&)=delete;
    StudioCaptionFont& operator=(const StudioCaptionFont&)=delete;
    float baselineOffset() const { return baseline_; }

    Layout layout(const std::string& text) {
        Layout result;
        if(scale_==0)return result;
        int previous=0;
        for(Rml::StringIteratorU8 it(text);it;++it){
            const int id=stbtt_FindGlyphIndex(&font_,int(*it));
            auto [entry,inserted]=glyphs_.try_emplace(id);
            if(inserted)entry->second=rasterize(id);
            result.advance+=stbtt_GetGlyphKernAdvance(&font_,previous,id)*scale_;
            result.glyphs.push_back({result.advance,&entry->second});
            result.advance+=entry->second.advance;
            previous=id;
        }
        return result;
    }
private:
    Glyph rasterize(int id) const {
        int x0,y0,x1,y1,advance;
        stbtt_GetGlyphHMetrics(&font_,id,&advance,nullptr);
        stbtt_GetGlyphBitmapBox(&font_,id,scale_,scale_,&x0,&y0,&x1,&y1);
        Glyph glyph;glyph.advance=advance*scale_;
        const int width=x1-x0,height=y1-y0;
        if(width<=0 || height<=0)return glyph;
        std::vector<unsigned char> pixels(size_t(width)*height);
        stbtt_MakeGlyphBitmap(&font_,pixels.data(),width,height,width,scale_,scale_,id);
        for(int y=0;y<height;++y){
            const auto coverage=[&](int x){return (unsigned(pixels[size_t(y)*width+x])+32)/64;};
            for(int x=0;x<width;){
                const unsigned level=coverage(x);
                const int start=x++;
                while(x<width && coverage(x)==level)++x;
                if(level)glyph.runs.push_back({x0+start,y0+y,x-start,level*255/4});
            }
        }
        return glyph;
    }
    std::vector<unsigned char> bytes_;
    stbtt_fontinfo font_{};
    float scale_=0,baseline_=0;
    std::map<int,Glyph> glyphs_;
};
}
