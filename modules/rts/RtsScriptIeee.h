// `f32` / `f64` script tables: IEEE binary32 / binary64 arithmetic on integer bit patterns
// (RtsScriptIeee.cpp; docs/scripting.md "Floating-point bit patterns").
#pragma once
struct lua_State;
namespace aiverse::game::script {
void openIeee(lua_State* L);   // sets the globals f32 and f64
}
