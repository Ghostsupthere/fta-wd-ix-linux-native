#define lbitlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdint.h>


typedef int32_t Integer;
typedef uint32_t UInteger;

/* Win32 integer instructions wrapped these operands. A direct double -> int32
 * cast is undefined for unsigned colours such as 0xFFFFFFFF on native Linux. */
static UInteger bit_word(lua_State *L, int index) {
  lua_Number value = luaL_checknumber(L, index);
  if (!isfinite(value)) {
    luaL_error(L, "bit operand must be finite");
    return 0;
  }
  value = fmod(trunc(value), 4294967296.0);
  if (value < 0) value += 4294967296.0;
  return (UInteger)value;
}
static Integer bit_signed(UInteger value) {
  return value <= INT32_MAX ? (Integer)value : -1 - (Integer)(UINT32_MAX - value);
}


// VERIFY: d6a77d71 manual
// OFFSET: 0x00850CC0
static int bit_bnot (lua_State *L) {
  lua_pushnumber(L, (UInteger)~bit_word(L, 1));
  return 1;
}


// VERIFY: e75ae7e6 manual
// OFFSET: 0x00850D00
static int bit_band (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_word(L, 1);
  for (i = 2; i <= n; i++)
    w &= bit_word(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 2eb41a74 manual
// OFFSET: 0x00850D80
static int bit_bor (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_word(L, 1);
  for (i = 2; i <= n; i++)
    w |= bit_word(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 6aea1192 manual
// OFFSET: 0x00850E00
static int bit_bxor (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_word(L, 1);
  for (i = 2; i <= n; i++)
    w ^= bit_word(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 5453693c auto
// OFFSET: 0x00850E80
static int bit_lshift (lua_State *L) {
  UInteger value = bit_word(L, 1), count = bit_word(L, 2) & 31u;
  lua_pushnumber(L, (UInteger)(value << count));
  return 1;
}

// VERIFY: 99a627b4 auto
// OFFSET: 0x00850EE0
static int bit_rshift (lua_State *L) {
  UInteger value = bit_word(L, 1), count = bit_word(L, 2) & 31u;
  lua_pushnumber(L, bit_signed(value >> count));
  return 1;
}

// VERIFY: b56ebf4a auto
// OFFSET: 0x00850F40
static int bit_arshift (lua_State *L) {
  UInteger value = bit_word(L, 1), count = bit_word(L, 2) & 31u;
  UInteger result = value >> count;
  if (count && (value & UINT32_C(0x80000000))) result |= UINT32_MAX << (32u-count);
  lua_pushnumber(L, bit_signed(result));
  return 1;
}

// VERIFY: 8e98a529 auto
// OFFSET: 0x00850F90
static int bit_mod (lua_State *L) {
  Integer value = bit_signed(bit_word(L, 1)), divisor = bit_signed(bit_word(L, 2));
  if (divisor == 0) {
    lua_pushnumber(L, 1 / lua_tonumber(L, 2));
    return 1;
  }
  lua_pushnumber(L, value == INT32_MIN && divisor == -1 ? 0 : value % divisor);
  return 1;
}

static const struct luaL_Reg bitlib[] = {
  {"bnot", bit_bnot},
  {"band", bit_band},
  {"bor", bit_bor},
  {"bxor", bit_bxor},
  {"lshift", bit_lshift},
  {"rshift", bit_rshift},
  {"arshift", bit_arshift},
  {"mod", bit_mod},
  {NULL, NULL}
};


// VERIFY: 23d41808 auto
// OFFSET: 0x00851010
LUALIB_API int luaopen_bit (lua_State *L) {
  luaL_openlib(L, "bit", bitlib, 0);
  return 1;
}
