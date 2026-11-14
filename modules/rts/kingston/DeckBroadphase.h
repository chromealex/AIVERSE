#pragma once
#include "modules/rts/RtsDriving.h"
namespace kingston {
// Conservative rejection only. Keep exact engine deckSpot, deck ordering and mouth
// rules for nearby cars. No cache, world pointer or authoritative state is retained.
inline bool deckMayTouchBody(const aiverse::game::DrivingDeck& d,
                             aiverse::Vec3 center, aiverse::Fixed halfBody) {
 using namespace aiverse;
 if(d.pts.size()<2 || d.at.size()!=d.pts.size())return true;
 auto loX=d.pts[0].x,hiX=loX,loZ=d.pts[0].z,hiZ=loZ;
 int64_t projectionError=4;
 for(size_t i=1;i<d.pts.size();++i){
  loX=fxMin(loX,d.pts[i].x);hiX=fxMax(hiX,d.pts[i].x);
  loZ=fxMin(loZ,d.pts[i].z);hiZ=fxMax(hiZ,d.pts[i].z);
  // Q16 normalization can displace a projected endpoint by < segment-length
  // raw units per axis. Include this and squared-distance truncation slack.
  projectionError=std::max(projectionError,(d.at[i]-d.at[i-1]).floorInt()+4);
 }
 auto margin=halfBody+d.width/2+Fixed::fromRatio(1,16)+Fixed::fromRaw(projectionError);
 return center.x>=loX-margin && center.x<=hiX+margin &&
        center.z>=loZ-margin && center.z<=hiZ+margin;
}
}
