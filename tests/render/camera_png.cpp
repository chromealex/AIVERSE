// Minimal PNG encoder contract: magic, IHDR, IEND, dimensions.
#include "engine/render/CameraPng.h"
#include <stdexcept>
using namespace aiverse::render;
static void check(bool b){if(!b)throw std::runtime_error("camera png");}
int main(){
 const uint8_t rgba[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
 std::vector<uint8_t> png;
 check(cameraEncodePngRgba8(2,2,rgba,png));
 check(png.size()>60);
 check(png[0]==137&&png[1]==80&&png[2]==78&&png[3]==71);
 check(png[12]=='I'&&png[13]=='H'&&png[14]=='D'&&png[15]=='R');
 check(png[png.size()-8]=='I'&&png[png.size()-7]=='E'&&
       png[png.size()-6]=='N'&&png[png.size()-5]=='D');
 std::vector<uint8_t> bad;
 check(!cameraEncodePngRgba8(3,2,rgba,bad));
 check(!cameraEncodePngRgba8(0,2,rgba,bad));
}
