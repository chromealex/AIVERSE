// `f32` / `f64` tables: IEEE 754 binary32 / binary64 arithmetic on integer bit patterns.
//
// Scripts that reproduce another program's floating-point results (a port of a game that simulates in
// float) keep every value as its bit pattern in a Lua integer, so the simulation still never holds a Lua
// float (determinism rules, docs/scripting.md). Doing that arithmetic in Lua costs hundreds of VM
// instructions per operation; these bindings do one correctly rounded IEEE operation each.
//
// Determinism: + - * / sqrt and the int/float conversions are correctly rounded (round to nearest, ties
// to even) by IEEE 754 on every target (arm64, x86-64 SSE, wasm). Each binding performs exactly one
// operation, so nothing can be contracted into an FMA (the file is also built with -ffp-contract=off) and
// no wider intermediate exists (FLT_EVAL_METHOD 0 is required at compile time). NaN results are
// canonical (0x7fc00000 / 0x7ff8000000000000) so hardware NaN payload/sign differences never reach a
// script. A per-thread check refuses to run when the floating-point environment is not the IEEE default
// (flush-to-zero / denormals-are-zero, or a directed rounding mode).
//
// Bit patterns: f32 values are integers 0..0xffffffff (the unsigned 32-bit pattern), f64 values are any
// integer (the signed 64-bit reinterpretation, as BitConverter.DoubleToInt64Bits). Anything else is an
// error. The bindings are pure (no world access) and raise errors with luaL_error: no C++ object with a
// destructor is alive in them (see RtsScriptInternal.h, error discipline).
#include "RtsScriptIeee.h"
#include <cfloat>
#include <cstdint>
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "RtsScriptIeee needs FLT_EVAL_METHOD 0 (no extended-precision intermediates)"
#endif
#if defined(__FAST_MATH__)
#error "RtsScriptIeee must not be built with -ffast-math"
#endif

namespace rts::game::script {
namespace {

constexpr uint32_t kNan32 = 0x7fc00000u;
constexpr uint64_t kNan64 = 0x7ff8000000000000ull;

inline float f32Of(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
inline uint32_t bitsOf(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
inline double f64Of(uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
inline uint64_t bitsOf(double d) { uint64_t b; std::memcpy(&b, &d, 8); return b; }
inline uint32_t canon(float f) { return f != f ? kNan32 : bitsOf(f); }
inline uint64_t canon(double d) { return d != d ? kNan64 : bitsOf(d); }

// The IEEE default environment, checked once per thread: subnormal operands and results survive and
// rounding is to nearest. Operands go through volatile so the compiler cannot fold the check.
bool environmentOk() {
    volatile uint32_t tiny = 1u, fmin = 0x00800000u, one = 0x3f800000u, half = 0x3f000000u, above = 0x33800001u;
    volatile uint64_t tiny64 = 1u, one64 = 0x3ff0000000000000ull, half64 = 0x3fe0000000000000ull;
    if (bitsOf(f32Of(tiny) * f32Of(one)) != 1u) return false;                    // denormals-are-zero
    if (bitsOf(f32Of(fmin) * f32Of(half)) != 0x00400000u) return false;           // flush-to-zero
    if (bitsOf(f32Of(one) + f32Of(above)) != 0x3f800001u) return false;           // not toward zero / down
    if (bitsOf(-f32Of(one) - f32Of(above)) != 0xbf800001u) return false;          // not toward zero / up
    if (bitsOf(f32Of(one) + f32Of(0x33800000u)) != 0x3f800000u) return false;     // ties to even
    if (bitsOf(f64Of(tiny64) * f64Of(one64)) != 1u) return false;
    if (bitsOf(f64Of(0x0010000000000000ull) * f64Of(half64)) != 0x0008000000000000ull) return false;
    return true;
}
thread_local int tEnv = 0;   // 0 unchecked, 1 ok, -1 not IEEE default
inline void requireEnv(lua_State* L) {
    if (tEnv == 0) tEnv = environmentOk() ? 1 : -1;
    if (tEnv < 0) luaL_error(L, "f32/f64: the floating-point environment is not the IEEE default (flush-to-zero or rounding mode)");
}

inline uint32_t arg32(lua_State* L, int i) {
    const int ok = lua_isinteger(L, i);
    const lua_Integer v = lua_tointeger(L, i);
    if (!ok || v < 0 || v > lua_Integer(0xffffffffu)) luaL_error(L, "f32: argument %d is not a binary32 bit pattern (integer 0..0xffffffff)", i);
    return uint32_t(v);
}
inline uint64_t arg64(lua_State* L, int i) {
    if (!lua_isinteger(L, i)) luaL_error(L, "f64: argument %d is not a binary64 bit pattern (integer)", i);
    return uint64_t(lua_tointeger(L, i));
}
inline lua_Integer argInteger(lua_State* L, int i, const char* what) {
    if (!lua_isinteger(L, i)) luaL_error(L, "%s: argument %d is not an integer", what, i);
    return lua_tointeger(L, i);
}
inline int push32(lua_State* L, float f) { lua_pushinteger(L, lua_Integer(canon(f))); return 1; }
inline int push64(lua_State* L, double d) { lua_pushinteger(L, lua_Integer(canon(d))); return 1; }

// One operation per function (never a*b+c in one expression: no contraction).
int f32Add(lua_State* L) { requireEnv(L); const float r = f32Of(arg32(L, 1)) + f32Of(arg32(L, 2)); return push32(L, r); }
int f32Sub(lua_State* L) { requireEnv(L); const float r = f32Of(arg32(L, 1)) - f32Of(arg32(L, 2)); return push32(L, r); }
int f32Mul(lua_State* L) { requireEnv(L); const float r = f32Of(arg32(L, 1)) * f32Of(arg32(L, 2)); return push32(L, r); }
int f32Div(lua_State* L) { requireEnv(L); const float r = f32Of(arg32(L, 1)) / f32Of(arg32(L, 2)); return push32(L, r); }
int f32Sqrt(lua_State* L) { requireEnv(L); const float a = f32Of(arg32(L, 1)); const float r = __builtin_sqrtf(a); return push32(L, r); }
int f32FromInt(lua_State* L) { requireEnv(L); const float r = float(argInteger(L, 1, "f32.fromint")); return push32(L, r); }
int f32Lt(lua_State* L) { lua_pushboolean(L, f32Of(arg32(L, 1)) < f32Of(arg32(L, 2))); return 1; }
int f32Le(lua_State* L) { lua_pushboolean(L, f32Of(arg32(L, 1)) <= f32Of(arg32(L, 2))); return 1; }
int f32Eq(lua_State* L) { lua_pushboolean(L, f32Of(arg32(L, 1)) == f32Of(arg32(L, 2))); return 1; }
int f32ToF64(lua_State* L) { requireEnv(L); const double r = double(f32Of(arg32(L, 1))); return push64(L, r); }

// a*b + c*d rounded once to binary32 (a fused two-term dot product). Both products are exact in binary64
// (24-bit significands); TwoSum gives their binary64 sum s and its exact error e. Rounding s to binary32 is
// the correctly rounded result unless s lies exactly on a binary32 midpoint (every binary32 midpoint,
// subnormal or the overflow threshold included, is a binary64 number), where the sign of e decides.
float dot2(float a, float b, float c, float d) {
    const double p1 = double(a) * double(b);
    const double p2 = double(c) * double(d);
    const double sum = p1 + p2;
    float r = float(sum);
    if (sum - sum == 0.0) {   // finite
        const double back = sum - p1;
        const double err = (p1 - (sum - back)) + (p2 - back);
        const double rd = double(r);
        if (err != 0.0 && rd != sum) {
            const float other = __builtin_nextafterf(r, sum > rd ? __builtin_inff() : -__builtin_inff());
            const double mid = (rd + double(other)) * 0.5;
            if (sum == mid) r = (err > 0.0) == (other > r) ? other : r;
        }
    }
    return r;
}
int f32Dot2(lua_State* L) {
    requireEnv(L);
    return push32(L, dot2(f32Of(arg32(L, 1)), f32Of(arg32(L, 2)), f32Of(arg32(L, 3)), f32Of(arg32(L, 4))));
}

// Fused helpers of hot script primitives (a port's float32 grid code): each is the composition of the
// operations above, with the error of the script code it replaces where that code rejects an operand.
inline bool finite32(uint32_t b) { return (b & 0x7f800000u) != 0x7f800000u; }
// floor of a finite binary32 whose floor fits a signed int32 (|a| < 2^31); other operands are errors
inline lua_Integer floorInt(lua_State* L, uint32_t a) {
    const uint32_t be = (a >> 23) & 255u;
    if (be == 255u) luaL_error(L, "non-finite float-to-int conversion");
    if (be > 157u) luaL_error(L, "float-to-int outside signed int32 range");
    return lua_Integer(__builtin_floorf(f32Of(a)));
}
inline uint32_t field32(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    const int ok = lua_isinteger(L, -1);
    const lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (!ok || v < 0 || v > lua_Integer(0xffffffffu)) luaL_error(L, "f32: field %s is not a binary32 bit pattern (integer 0..0xffffffff)", k);
    return uint32_t(v);
}
// floorint(a): Mathf.FloorToInt of a finite value in the int32 range
int f32FloorInt(lua_State* L) { return lua_pushinteger(L, floorInt(L, arg32(L, 1))), 1; }
// cellat(p, width, height): the grid cell (y * width + x) holding point {x=, y=} (floors), -1 outside
int f32CellAt(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const uint32_t px = field32(L, 1, "x"), py = field32(L, 1, "y");
    const lua_Integer w = argInteger(L, 2, "f32.cellat"), h = argInteger(L, 3, "f32.cellat");
    const lua_Integer x = floorInt(L, px), y = floorInt(L, py);
    lua_pushinteger(L, x >= 0 && y >= 0 && x < w && y < h ? y * w + x : -1);
    return 1;
}
// fdot2(a, b, c, d): dot2 of finite operands; a non-finite operand is an error
int f32FiniteDot2(lua_State* L) {
    requireEnv(L);
    const uint32_t a = arg32(L, 1), b = arg32(L, 2), c = arg32(L, 3), d = arg32(L, 4);
    if (!finite32(a) || !finite32(b) || !finite32(c) || !finite32(d)) luaL_error(L, "nonfinite dot operand");
    return push32(L, dot2(f32Of(a), f32Of(b), f32Of(c), f32Of(d)));
}
// sqdist(p, q): x = p.x - q.x, y = p.y - q.y (each rounded), then fdot2(x, x, y, y)
int f32SqDist(lua_State* L) {
    requireEnv(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    const float x = f32Of(field32(L, 1, "x")) - f32Of(field32(L, 2, "x"));
    const float y = f32Of(field32(L, 1, "y")) - f32Of(field32(L, 2, "y"));
    if (!finite32(canon(x)) || !finite32(canon(y))) luaL_error(L, "nonfinite dot operand");
    return push32(L, dot2(x, x, y, y));
}

int f64Add(lua_State* L) { requireEnv(L); const double r = f64Of(arg64(L, 1)) + f64Of(arg64(L, 2)); return push64(L, r); }
int f64Sub(lua_State* L) { requireEnv(L); const double r = f64Of(arg64(L, 1)) - f64Of(arg64(L, 2)); return push64(L, r); }
int f64Mul(lua_State* L) { requireEnv(L); const double r = f64Of(arg64(L, 1)) * f64Of(arg64(L, 2)); return push64(L, r); }
int f64Div(lua_State* L) { requireEnv(L); const double r = f64Of(arg64(L, 1)) / f64Of(arg64(L, 2)); return push64(L, r); }
int f64Sqrt(lua_State* L) { requireEnv(L); const double a = f64Of(arg64(L, 1)); const double r = __builtin_sqrt(a); return push64(L, r); }
int f64FromInt(lua_State* L) { requireEnv(L); const double r = double(argInteger(L, 1, "f64.fromint")); return push64(L, r); }
int f64Lt(lua_State* L) { lua_pushboolean(L, f64Of(arg64(L, 1)) < f64Of(arg64(L, 2))); return 1; }
int f64Le(lua_State* L) { lua_pushboolean(L, f64Of(arg64(L, 1)) <= f64Of(arg64(L, 2))); return 1; }
int f64Eq(lua_State* L) { lua_pushboolean(L, f64Of(arg64(L, 1)) == f64Of(arg64(L, 2))); return 1; }
int f64ToF32(lua_State* L) { requireEnv(L); const float r = float(f64Of(arg64(L, 1))); return push32(L, r); }

} // namespace

void openIeee(lua_State* L) {
    static const luaL_Reg f32[] = {{"add", f32Add}, {"sub", f32Sub}, {"mul", f32Mul}, {"div", f32Div}, {"sqrt", f32Sqrt},
                                   {"dot2", f32Dot2}, {"fromint", f32FromInt}, {"lt", f32Lt}, {"le", f32Le}, {"eq", f32Eq}, {"tof64", f32ToF64},
                                   {"floorint", f32FloorInt}, {"cellat", f32CellAt}, {"fdot2", f32FiniteDot2}, {"sqdist", f32SqDist},
                                   {nullptr, nullptr}};
    static const luaL_Reg f64[] = {{"add", f64Add}, {"sub", f64Sub}, {"mul", f64Mul}, {"div", f64Div}, {"sqrt", f64Sqrt},
                                   {"fromint", f64FromInt}, {"lt", f64Lt}, {"le", f64Le}, {"eq", f64Eq}, {"tof32", f64ToF32},
                                   {nullptr, nullptr}};
    luaL_newlib(L, f32);
    lua_setglobal(L, "f32");
    luaL_newlib(L, f64);
    lua_setglobal(L, "f64");
}

} // namespace rts::game::script
