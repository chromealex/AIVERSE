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
#include <algorithm>
#include <initializer_list>

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
constexpr lua_Integer kNone = LUA_MININTEGER;   // no neighbour (f32.flood)

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

// ---- grid segment walk: f32.segment(walkable, burning, owners, width, height, first, second, from, to)
// The four-connected cell walk from point `from` to point `to` (tables {x=, y=} of binary32 bit patterns) of a
// float32 grid port (OpenWar ArmySharedRoutes): true when every visited cell, both cells at a corner crossing
// included, is allowed: inside the grid, walkable[c] == true, burning[c] absent (burning may be nil), and
// owners[c] raw-equal to `first` or (when `second` is not nil) to `second`. The crossing parameters are the
// binary32 operations of the script version in the same order; more than width + height steps is false.
struct Walk {
    lua_State* L; int walkable, burning, owners, first, second; lua_Integer size;
    bool allowed(lua_Integer c) const {
        if (c < 0 || c >= size) return false;
        const bool w = lua_rawgeti(L, walkable, c) == LUA_TBOOLEAN && lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (!w) return false;
        if (burning) { const bool b = lua_rawgeti(L, burning, c) != LUA_TNIL && lua_toboolean(L, -1); lua_pop(L, 1); if (b) return false; }
        lua_rawgeti(L, owners, c);
        bool ok = lua_rawequal(L, -1, first) != 0;
        if (!ok && second) ok = lua_rawequal(L, -1, second) != 0;
        lua_pop(L, 1);
        return ok;
    }
};
int f32Segment(lua_State* L) {
    requireEnv(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    const lua_Integer w = argInteger(L, 4, "f32.segment"), h = argInteger(L, 5, "f32.segment");
    luaL_checktype(L, 8, LUA_TTABLE);
    luaL_checktype(L, 9, LUA_TTABLE);
    Walk k{L, 1, lua_isnoneornil(L, 2) ? 0 : 2, 3, 6, lua_isnoneornil(L, 7) ? 0 : 7, w * h};
    const uint32_t fx = field32(L, 8, "x"), fy = field32(L, 8, "y"), tx0 = field32(L, 9, "x"), ty0 = field32(L, 9, "y");
    auto cellOf = [&](uint32_t x, uint32_t y) -> lua_Integer {
        const lua_Integer cx = floorInt(L, x), cy = floorInt(L, y);
        return cx >= 0 && cy >= 0 && cx < w && cy < h ? cy * w + cx : -1;
    };
    lua_Integer cell = cellOf(fx, fy);
    const lua_Integer last = cellOf(tx0, ty0);
    if (!k.allowed(cell) || !k.allowed(last)) { lua_pushboolean(L, 0); return 1; }
    const float inf = f32Of(0x7f800000u), one = 1.0f;
    const float vx = f32Of(tx0) - f32Of(fx), vy = f32Of(ty0) - f32Of(fy);
    const int sx = vx < 0.0f ? -1 : 1, sy = vy < 0.0f ? -1 : 1;
    const float dx = vx == 0.0f ? inf : one / __builtin_fabsf(vx);
    const float dy = vy == 0.0f ? inf : one / __builtin_fabsf(vy);
    float tx, ty;
    if (vx == 0.0f) tx = inf; else { const float edge = float(floorInt(L, fx) + (sx > 0 ? 1 : 0)); const float gap = edge - f32Of(fx); tx = gap / vx; }
    if (vy == 0.0f) ty = inf; else { const float edge = float(floorInt(L, fy) + (sy > 0 ? 1 : 0)); const float gap = edge - f32Of(fy); ty = gap / vy; }
    lua_Integer steps = 0;
    while (cell != last) {
        if (steps > w + h) { lua_pushboolean(L, 0); return 1; }
        const bool crossX = tx < ty || tx == ty, crossY = ty < tx || ty == tx;
        const lua_Integer x = cell % w, y = cell / w;   // cell >= 0 here
        if (crossX && crossY) {
            const lua_Integer nx = sx < 0 ? (x > 0 ? cell - 1 : -1) : (x < w - 1 ? cell + 1 : -1);
            const lua_Integer ny = sy < 0 ? (y > 0 ? cell - w : -1) : (y < h - 1 ? cell + w : -1);
            if (!k.allowed(nx) || !k.allowed(ny)) { lua_pushboolean(L, 0); return 1; }
        }
        if (crossX) { cell = sx < 0 ? (x > 0 ? cell - 1 : -1) : (x < w - 1 ? cell + 1 : -1); tx = tx + dx; }
        if (crossY && cell >= 0) { const lua_Integer cy = cell / w; cell = sy < 0 ? (cy > 0 ? cell - w : -1) : (cy < h - 1 ? cell + w : -1); ty = ty + dy; }
        if (!k.allowed(cell)) { lua_pushboolean(L, 0); return 1; }
        ++steps;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// ---- grid flood: f32.flood(walkable, burning, owners, owner, width, height, dist, queue[, max[, previous[, blocked[, copy]]]])
// Breadth-first continuation of a seeded walk (OpenWar's owned-cell BFS loops): the queue array is popped in
// order from index 1 (entries appended while it runs are popped too). For a popped cell c with value
// v = dist[c] the next value is v + 1 (v must be an integer), or v itself when `copy` is true (label
// propagation). When `max` is not nil and the next value exceeds it, c is not expanded. The neighbours
// x-1, x+1, y-1, y+1 of c (x = c mod width, y = c // width, floored as Lua's % and //; a neighbour outside
// 0..width-1 x 0..height-1 is skipped) are taken, in that order, when dist[n] is nil, walkable[n] == true,
// burning[n] is nil or false (burning may be nil), owners[n] is raw-equal to owner and blocked[n] is nil or
// false (blocked may be nil): dist[n] = next value, previous[n] = c (previous may be nil), n appended to the
// queue. Exactly the visiting order of the script loop, so the queue and previous maps equal its result.
// Returns the queue length. Raw table access only; the work is outside Lua instruction counting.
int f32Flood(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checkany(L, 4);
    const lua_Integer w = argInteger(L, 5, "f32.flood"), h = argInteger(L, 6, "f32.flood");
    if (w <= 0 || h <= 0) luaL_error(L, "f32.flood: width and height must be positive");
    luaL_checktype(L, 7, LUA_TTABLE);
    luaL_checktype(L, 8, LUA_TTABLE);
    const bool bounded = !lua_isnoneornil(L, 9);
    const lua_Integer max = bounded ? argInteger(L, 9, "f32.flood") : 0;
    const int previous = lua_isnoneornil(L, 10) ? 0 : 10;
    if (previous) luaL_checktype(L, 10, LUA_TTABLE);
    const int blocked = lua_isnoneornil(L, 11) ? 0 : 11;
    if (blocked) luaL_checktype(L, 11, LUA_TTABLE);
    const bool copy = lua_toboolean(L, 12) != 0;
    const int walkable = 1, burning = lua_isnoneornil(L, 2) ? 0 : 2, owners = 3, owner = 4, dist = 7, queue = 8;
    lua_settop(L, 12);
    luaL_checkstack(L, 8, "f32.flood");
    lua_Integer n = lua_Integer(lua_rawlen(L, queue));
    for (lua_Integer head = 1; head <= n; ++head) {
        if (lua_rawgeti(L, queue, head) != LUA_TNUMBER || !lua_isinteger(L, -1)) luaL_error(L, "f32.flood: queue entry %d is not an integer", int(head));
        const lua_Integer c = lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, dist, c);   // the value v (stays on the stack in copy mode)
        lua_Integer next = 0;
        if (!copy) {
            if (!lua_isinteger(L, -1)) luaL_error(L, "f32.flood: dist of queued cell %d is not an integer", int(c));
            next = lua_tointeger(L, -1) + 1;
            lua_pop(L, 1);
            if (bounded && next > max) continue;
        }
        const lua_Integer y = c >= 0 ? c / w : -((-c + w - 1) / w), x = c - y * w;   // floored, as Lua
        const lua_Integer cand[4] = {x > 0 ? c - 1 : kNone, x < w - 1 ? c + 1 : kNone, y > 0 ? c - w : kNone, y < h - 1 ? c + w : kNone};
        for (const lua_Integer nb : cand) {
            if (nb == kNone) continue;
            if (lua_rawgeti(L, dist, nb) != LUA_TNIL) { lua_pop(L, 1); continue; }
            lua_pop(L, 1);
            const bool walk = lua_rawgeti(L, walkable, nb) == LUA_TBOOLEAN && lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (!walk) continue;
            if (burning) { const bool b = lua_rawgeti(L, burning, nb) != LUA_TNIL && lua_toboolean(L, -1); lua_pop(L, 1); if (b) continue; }
            lua_rawgeti(L, owners, nb);
            const bool mine = lua_rawequal(L, -1, owner) != 0;
            lua_pop(L, 1);
            if (!mine) continue;
            if (blocked) { const bool b = lua_rawgeti(L, blocked, nb) != LUA_TNIL && lua_toboolean(L, -1); lua_pop(L, 1); if (b) continue; }
            if (copy) lua_pushvalue(L, -1); else lua_pushinteger(L, next);
            lua_rawseti(L, dist, nb);
            if (previous) { lua_pushinteger(L, c); lua_rawseti(L, previous, nb); }
            lua_pushinteger(L, nb);
            lua_rawseti(L, queue, ++n);
        }
        if (copy) lua_pop(L, 1);
    }
    lua_pushinteger(L, n);
    return 1;
}

// ---- bucketed spatial query: f32.query(buckets, byId, px, py, radius, size)
// The query of a float32 port's spatial index (OpenWar ArmySpatialIndex): buckets[y][x] are arrays of unit
// ids, byId maps an id to its unit table. Bucket range lo = floorint((p - radius) / size), hi =
// floorint((p + radius) / size) per axis (binary32 operations; errors as floorint); every id of every existing
// bucket, rows then columns, ascending, is inspected: its unit must exist in byId, and is kept when its Health
// h is an integer with 0 < h <= 0x7f800000 and not radius*radius < sqdist(unit.Position, p). Returns the kept
// ids in ascending order and the number inspected; when an id has no unit, returns nil and the number
// inspected before it (the caller raises its own error).
int f32Query(lua_State* L) {
    requireEnv(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    const float px = f32Of(arg32(L, 3)), py = f32Of(arg32(L, 4)), radius = f32Of(arg32(L, 5)), size = f32Of(arg32(L, 6));
    lua_settop(L, 6);
    luaL_checkstack(L, 8, "f32.query");
    const lua_Integer loX = floorInt(L, canon((px - radius) / size)), loY = floorInt(L, canon((py - radius) / size));
    const lua_Integer hiX = floorInt(L, canon((px + radius) / size)), hiY = floorInt(L, canon((py + radius) / size));
    const float squared = radius * radius;
    lua_createtable(L, 8, 0);   // 7: result
    lua_Integer n = 0, inspected = 0;
    for (lua_Integer y = loY; y <= hiY; ++y) {
        if (lua_rawgeti(L, 1, y) != LUA_TTABLE) { lua_pop(L, 1); continue; }   // 8: row
        for (lua_Integer x = loX; x <= hiX; ++x) {
            if (lua_rawgeti(L, 8, x) != LUA_TTABLE) { lua_pop(L, 1); continue; }   // 9: bucket
            const lua_Integer count = lua_Integer(lua_rawlen(L, 9));
            for (lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(L, 9, i);   // 10: id
                lua_pushvalue(L, 10);
                if (lua_gettable(L, 2) == LUA_TNIL) { lua_pushnil(L); lua_pushinteger(L, inspected); return 2; }   // 11: unit
                ++inspected;
                lua_getfield(L, 11, "Health");
                int isInt = 0;
                const lua_Integer h = lua_tointegerx(L, -1, &isInt);
                if (!isInt) luaL_error(L, "f32.query: unit Health is not an integer");
                lua_pop(L, 1);
                if (h > 0 && h <= 2139095040) {
                    if (lua_getfield(L, 11, "Position") != LUA_TTABLE) luaL_error(L, "f32.query: unit Position is not a table");
                    const float dx = f32Of(field32(L, -1, "x")) - px, dy = f32Of(field32(L, -1, "y")) - py;
                    lua_pop(L, 1);
                    if (!finite32(canon(dx)) || !finite32(canon(dy))) luaL_error(L, "nonfinite dot operand");
                    const float d = dot2(dx, dx, dy, dy);
                    if (!(squared < d)) { lua_pushvalue(L, 10); lua_rawseti(L, 7, ++n); }
                }
                lua_pop(L, 2);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    // ascending (the script's table.sort of integer ids): copied out, sorted, written back
    if (n > 1) {
        bool ints = true;
        lua_Integer* ids = static_cast<lua_Integer*>(lua_newuserdatauv(L, size_t(n) * sizeof(lua_Integer), 0));
        for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L, 7, i); int ok = 0; ids[i - 1] = lua_tointegerx(L, -1, &ok); ints = ints && ok; lua_pop(L, 1); }
        if (!ints) luaL_error(L, "f32.query: unit ids must be integers");
        std::sort(ids, ids + n);
        for (lua_Integer i = 1; i <= n; ++i) { lua_pushinteger(L, ids[i - 1]); lua_rawseti(L, 7, i); }
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 7);
    lua_pushinteger(L, inspected);
    return 2;
}

// ---- Q96 trigonometry (atan2, sincos): exact series on unsigned fixed point with 96 fractional bits in
// base-2^24 limbs (little endian, no high zero limbs), every step truncating exactly as the integer-only
// script implementation it replaces (OpenWar scripts/openwar/trig.lua), then one rounding to binary32
// (the bit-exact value and a sticky bit through a correctly rounded int-to-float and an exact power-of-two
// scale). Same operations in the same order, so the results equal the script's bit for bit.
struct Q {
    int n = 0;
    int64_t d[16];
};
constexpr int64_t kB = 16777216;
inline void qTrim(Q& a) { while (a.n > 0 && a.d[a.n - 1] == 0) --a.n; }
inline int qCmp(const Q& a, const Q& b) {
    if (a.n != b.n) return a.n < b.n ? -1 : 1;
    for (int i = a.n - 1; i >= 0; --i) if (a.d[i] != b.d[i]) return a.d[i] < b.d[i] ? -1 : 1;
    return 0;
}
void qCheck(lua_State* L, int n) { if (n > 15) luaL_error(L, "Q96 value too large"); }
Q qAdd(lua_State* L, const Q& a, const Q& b) {
    Q r; int64_t carry = 0; const int n = a.n > b.n ? a.n : b.n;
    for (int i = 0; i < n; ++i) { const int64_t v = (i < a.n ? a.d[i] : 0) + (i < b.n ? b.d[i] : 0) + carry; r.d[i] = v % kB; carry = v / kB; }
    r.n = n;
    if (carry > 0) { qCheck(L, r.n); r.d[r.n++] = carry; }
    return r;
}
Q qSub(lua_State* L, const Q& a, const Q& b) {
    if (qCmp(a, b) < 0) luaL_error(L, "negative Q96 difference");
    Q r; int64_t borrow = 0;
    for (int i = 0; i < a.n; ++i) { const int64_t v = a.d[i] - (i < b.n ? b.d[i] : 0) - borrow; borrow = v < 0 ? 1 : 0; r.d[i] = v + borrow * kB; }
    r.n = a.n; qTrim(r); return r;
}
Q qDivSmall(const Q& a, int64_t d) {
    Q r; int64_t rem = 0;
    for (int i = a.n - 1; i >= 0; --i) { const int64_t v = rem * kB + a.d[i]; r.d[i] = v / d; rem = v % d; }
    r.n = a.n; qTrim(r); return r;
}
// product shifted right by 96 bits (four limbs), truncated
Q qMul(lua_State* L, const Q& a, const Q& b) {
    int64_t p[32] = {0};
    if (a.n + b.n > 31) luaL_error(L, "Q96 value too large");
    for (int i = 0; i < a.n; ++i) {
        int64_t carry = 0;
        for (int j = 0; j < b.n; ++j) { const int k = i + j; const int64_t v = p[k] + a.d[i] * b.d[j] + carry; p[k] = v % kB; carry = v / kB; }
        p[i + b.n] = carry;
    }
    Q r; const int total = a.n + b.n;
    for (int i = 4; i < total; ++i) { qCheck(L, r.n); r.d[r.n++] = p[i]; }
    qTrim(r); return r;
}
inline void qParts(uint32_t a, int64_t& m, int& e) {
    const int be = int((a >> 23) & 255u); m = a & 8388607u;
    if (be == 0) e = -149; else { m |= 8388608; e = be - 150; }
}
Q qFixed(lua_State* L, uint32_t a) {
    int64_t m; int e; qParts(a & 0x7fffffffu, m, e);
    const int shift = e + 96;
    if (shift < 0) luaL_error(L, "angle too small for Q96");
    Q r; for (int i = 0; i < shift / 24; ++i) { qCheck(L, r.n); r.d[r.n++] = 0; }
    int64_t v = m << (shift % 24);
    while (v > 0) { qCheck(L, r.n); r.d[r.n++] = v % kB; v /= kB; }
    qTrim(r); return r;
}
uint32_t qBits(const Q& a) {
    if (a.n == 0) return 0;
    const int drop = a.n - 2 > 0 ? a.n - 2 : 0;
    int64_t v = 0; for (int i = a.n - 1; i >= drop; --i) v = v * kB + a.d[i];
    int64_t sticky = 0; for (int i = 0; i < drop; ++i) if (a.d[i] != 0) { sticky = 1; break; }
    const float f = float((v << 1) | sticky);                      // F.fromInteger: correctly rounded
    return canon(f * f32Of(uint32_t(127 - 97 + drop * 24) << 23));  // F.mul by a power of two
}
Q qOf(std::initializer_list<int64_t> limbs) { Q r; for (int64_t x : limbs) r.d[r.n++] = x; qTrim(r); return r; }
const Q& qPi() { static const Q v = qOf({1673774, 578323, 8947107, 2375530, 3}); return v; }
const Q& qHalf() { static const Q v = qDivSmall(qPi(), 2); return v; }
const Q& qQuarter() { static const Q v = qDivSmall(qPi(), 4); return v; }
Q qSinFixed(lua_State* L, const Q& x) {
    const Q square = qMul(L, x, x); Q term = x, sum = x;
    for (int i = 1; i <= 40; ++i) {
        term = qDivSmall(qMul(L, term, square), int64_t(2 * i) * (2 * i + 1));
        if (term.n == 0) break;
        sum = i % 2 == 1 ? qSub(L, sum, term) : qAdd(L, sum, term);
    }
    return sum;
}
Q qRatio(lua_State* L, int64_t a, int64_t b) {
    if (!(a >= 0 && a <= b && b > 0)) luaL_error(L, "Q96 ratio domain");
    if (a == b) return qOf({0, 0, 0, 0, 1});
    Q r; r.n = 4; int64_t rem = a;
    for (int i = 3; i >= 0; --i) {
        int64_t digit = 0;
        for (int k = 0; k < 24; ++k) { rem *= 2; digit *= 2; if (rem >= b) { rem -= b; digit += 1; } }
        r.d[i] = digit;
    }
    qTrim(r); return r;
}
Q qAtanSmall(lua_State* L, const Q& x) {
    const Q square = qMul(L, x, x); Q power = x, sum = x;
    for (int i = 1; i <= 70; ++i) {
        power = qMul(L, power, square);
        const Q term = qDivSmall(power, 2 * i + 1);
        if (term.n == 0) break;
        sum = i % 2 == 1 ? qSub(L, sum, term) : qAdd(L, sum, term);
    }
    return sum;
}
inline uint32_t neg32(uint32_t a) { return a ^ 0x80000000u; }
// atan2(y, x) of binary32 bit patterns (Mathf.Atan2 as the script reproduces it)
int f32Atan2(lua_State* L) {
    requireEnv(L);
    const uint32_t y = arg32(L, 1), x = arg32(L, 2);
    const uint32_t ax = x & 0x7fffffffu, ay = y & 0x7fffffffu; const uint32_t sx = x >> 31, sy = y >> 31;
    if (!(ax < 0x7f800000u && ay < 0x7f800000u)) luaL_error(L, "nonfinite steering direction");
    uint32_t r;
    if (ay == 0) r = sx != 0 ? (sy != 0 ? neg32(qBits(qPi())) : qBits(qPi())) : y;
    else if (ax == 0) r = sy != 0 ? neg32(qBits(qHalf())) : qBits(qHalf());
    else {
        const bool swap = ay > ax; const uint32_t low = swap ? ax : ay, high = swap ? ay : ax;
        int64_t ml, mh; int el, eh; qParts(low, ml, el); qParts(high, mh, eh);
        if (eh - el > 30) {
            if (swap) r = sy != 0 ? neg32(qBits(qHalf())) : qBits(qHalf());
            else if (sx != 0) r = sy != 0 ? neg32(qBits(qPi())) : qBits(qPi());
            else r = canon(f32Of(y) / f32Of(x));
        } else {
            const int exponent = el < eh ? el : eh; const int64_t a = ml << (el - exponent), b = mh << (eh - exponent);
            Q angle = a * 2 > b ? qSub(L, qQuarter(), qAtanSmall(L, qRatio(L, b - a, b + a))) : qAtanSmall(L, qRatio(L, a, b));
            if (swap) angle = qSub(L, qHalf(), angle);
            if (sx != 0) angle = qSub(L, qPi(), angle);
            const uint32_t result = qBits(angle);
            r = sy != 0 ? neg32(result) : result;
        }
    }
    lua_pushinteger(L, lua_Integer(r));
    return 1;
}
// sincos(angle) of a binary32 bit pattern, |angle| <= 2^33: sine, cosine
int f32SinCos(lua_State* L) {
    requireEnv(L);
    const uint32_t angle = arg32(L, 1); const uint32_t a = angle & 0x7fffffffu;
    if (!(a <= 1342177280u)) luaL_error(L, "angle must be finite and within +/-2^33 radians");
    if (a < 729808896u) { lua_pushinteger(L, angle); lua_pushinteger(L, 0x3f800000); return 2; }   // |angle| < 2^-40
    Q x = qFixed(L, a);
    const Q two = qAdd(L, qPi(), qPi());
    Q periods[48]; int np = 0; periods[np++] = two;
    while (qCmp(periods[np - 1], x) < 0) { if (np >= 48) luaL_error(L, "Q96 value too large"); periods[np] = qAdd(L, periods[np - 1], periods[np - 1]); ++np; }
    for (int i = np - 1; i >= 0; --i) if (qCmp(x, periods[i]) >= 0) x = qSub(L, x, periods[i]);
    bool negativeSin = (angle >> 31) != 0, negativeCos = false;
    if (qCmp(x, qPi()) > 0) { x = qSub(L, two, x); negativeSin = !negativeSin; }
    if (qCmp(x, qHalf()) > 0) { x = qSub(L, qPi(), x); negativeCos = true; }
    const uint32_t sine = qBits(qSinFixed(L, x)), cosine = qBits(qSinFixed(L, qSub(L, qHalf(), x)));
    lua_pushinteger(L, lua_Integer(negativeSin ? neg32(sine) : sine));
    lua_pushinteger(L, lua_Integer(negativeCos ? neg32(cosine) : cosine));
    return 2;
}

} // namespace

void openIeee(lua_State* L) {
    static const luaL_Reg f32[] = {{"add", f32Add}, {"sub", f32Sub}, {"mul", f32Mul}, {"div", f32Div}, {"sqrt", f32Sqrt},
                                   {"dot2", f32Dot2}, {"fromint", f32FromInt}, {"lt", f32Lt}, {"le", f32Le}, {"eq", f32Eq}, {"tof64", f32ToF64},
                                   {"floorint", f32FloorInt}, {"cellat", f32CellAt}, {"fdot2", f32FiniteDot2}, {"sqdist", f32SqDist},
                                   {"atan2", f32Atan2}, {"sincos", f32SinCos}, {"segment", f32Segment}, {"flood", f32Flood}, {"query", f32Query},
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
