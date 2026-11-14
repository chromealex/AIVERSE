#pragma once
#include "engine/core/Fixed.h"
namespace kingston {
// Millimetre-cell rectangles shared with city-polish/source/traffic_layout.py.
inline bool trafficParkingReserved(aiverse::Fixed x,aiverse::Fixed z){
 constexpr int areas[][4]={{142000,177000,204000,178000},{204000,183500,204800,196500},{187000,204000,196000,205000},{177000,187000,178000,197000},{183000,180000,194000,181000}};
 for(auto&a:areas)if(x>=aiverse::Fixed::fromRatio(a[0],1000)&&x<=aiverse::Fixed::fromRatio(a[2],1000)&&z>=aiverse::Fixed::fromRatio(a[1],1000)&&z<=aiverse::Fixed::fromRatio(a[3],1000))return true;
 return false;
}
}
