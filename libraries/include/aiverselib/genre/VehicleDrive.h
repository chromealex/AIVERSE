#pragma once
// vehicle-controllers: the driver's controls -> pedals, one mapping for every host and client (docs/races.md § Hosts,
// AGENTS.md "no genre split"). A host never invents its own mapping: it reads the player's input in one of these
// shapes and hands the VehicleInput to stepVehicle.
//
//  * Axes (keyboard / gamepad sticks, first or third person): forward / back = throttle / brake (the automatic
//    gearbox reverses from a standstill), right steers, the handbrake button, pitch for flight.
//  * Screen stick (top-down camera: +x right, +z down the screen; touch joystick or WASD as a direction), two modes:
//    Tank - stick up = throttle, down = brake / reverse, sideways = steering (diagonals count in full);
//    Steer - the car turns toward the stick direction and accelerates; let go and it rolls to a stop.
// Fixed math only; the results feed authoritative simulation.
#include "VehicleControllers.h"
namespace aiverse::lib::genre {
struct DriveAxes{Fixed forward{},right{},pitch{};bool handbrake=false;};   // forward / right in -1..1, pitch in turns
inline VehicleInput vehicleInputFromAxes(const DriveAxes& a){
    VehicleInput in;
    in.throttle=fxMax(Fixed{},a.forward);in.brake=fxMax(Fixed{},-a.forward);
    in.steer=fxClamp(a.right,-1_fx,1_fx);in.handbrake=a.handbrake;
    in.pitch=fxClamp(-a.pitch*4,-1_fx,1_fx);in.yaw=in.steer;
    return in;
}
enum class DriveStick:uint8_t{Tank,Steer};
// angle: stick direction in turns (0 = +z, a quarter = +x); magnitude 0..1.
struct DriveScreenStick{Fixed angle{},magnitude{};bool handbrake=false;};
inline VehicleInput vehicleInputFromStick(DriveStick mode,const DriveScreenStick& s,const VehicleState& car){
    VehicleInput in;in.handbrake=s.handbrake;const Fixed one=1_fx;
    if(mode==DriveStick::Tank){
        // up the screen is -z, so throttle is -cos; a turn to the right on screen is a negative library yaw
        const Fixed boost=Fixed::fromRatio(3,2);
        const Fixed t=-fxCosTurns(s.angle)*s.magnitude*boost,st=-fxSinTurns(s.angle)*s.magnitude*boost;
        in.throttle=fxClamp(t,Fixed{},one);in.brake=fxClamp(-t,Fixed{},one);in.steer=fxClamp(st,-one,one);
        return in;
    }
    const Fixed v=vehicleForwardSpeed(car);
    if(s.magnitude.raw<=0){if(v>Fixed::fromRatio(1,2))in.brake=Fixed::fromRatio(3,10);return in;}   // let go: roll to a stop
    const Fixed diff=vehicleDetail::wrap(s.angle-car.yaw);
    in.steer=fxClamp(diff*6,-one,one);
    in.throttle=fxAbs(diff)>Fixed::fromRatio(1,4)?s.magnitude/2:s.magnitude;
    return in;
}
}
