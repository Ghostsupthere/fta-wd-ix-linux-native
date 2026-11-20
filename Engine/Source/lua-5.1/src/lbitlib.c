#define lbitlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include <stdint.h>


typedef int32_t Integer;
typedef uint32_t UInteger;


/* The original Win32 routines consume the low word of __ftol2's signed 64-bit
** truncation, including packed unsigned colors. A direct x64 double-to-int cast
** instead returns INT_MIN above INT_MAX. Preserve the original conversion
** without an out-of-range cast; x87's indefinite result has a zero low word.
*/
static UInteger bit_checkword (lua_State *L, int index) {
  lua_Number value = luaL_checknumber(L, index);
  if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0))
    return 0;
  return (UInteger)(int64_t)value;
}


static Integer bit_signed (UInteger word) {
  return (Integer)(word < 0x80000000u ? (int64_t)word : (int64_t)word - 4294967296LL);
}


static UInteger bit_shift (lua_State *L) {
  /* Native SHL/SHR/SAR mask CL to five bits, also for negative/wide counts. */
  return bit_checkword(L, 2) & 31u;
}


// VERIFY: d6a77d71 manual
// OFFSET: 0x00850CC0
static int bit_bnot (lua_State *L) {
  lua_pushnumber(L, (UInteger)~bit_checkword(L, 1));
  return 1;
}


// VERIFY: e75ae7e6 manual
// OFFSET: 0x00850D00
static int bit_band (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_checkword(L, 1);
  for (i = 2; i <= n; i++)
    w &= bit_checkword(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 2eb41a74 manual
// OFFSET: 0x00850D80
static int bit_bor (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_checkword(L, 1);
  for (i = 2; i <= n; i++)
    w |= bit_checkword(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 6aea1192 manual
// OFFSET: 0x00850E00
static int bit_bxor (lua_State *L) {
  int n = lua_gettop(L), i;
  UInteger w = bit_checkword(L, 1);
  for (i = 2; i <= n; i++)
    w ^= bit_checkword(L, i);
  lua_pushnumber(L, w);
  return 1;
}


// VERIFY: 5453693c auto
// OFFSET: 0x00850E80
static int bit_lshift (lua_State *L) {
  UInteger shift = bit_shift(L);
  lua_pushnumber(L, (UInteger)(bit_checkword(L, 1) << shift));
  return 1;
}


// VERIFY: 99a627b4 auto
// OFFSET: 0x00850EE0
static int bit_rshift (lua_State *L) {
  UInteger shift = bit_shift(L);
  lua_pushnumber(L, bit_checkword(L, 1) >> shift);
  return 1;
}


// VERIFY: b56ebf4a auto
// OFFSET: 0x00850F40
static int bit_arshift (lua_State *L) {
  UInteger shift = bit_shift(L);
  UInteger word = bit_checkword(L, 1);
  UInteger shifted = word >> shift;
  if (shift && (word & 0x80000000u))
    shifted |= 0xffffffffu << (32u - shift);
  lua_pushnumber(L, bit_signed(shifted));
  return 1;
}


// VERIFY: 8e98a529 auto
// OFFSET: 0x00850F90
static int bit_mod (lua_State *L) {
  Integer divisor = bit_signed(bit_checkword(L, 2));
  Integer dividend;
  if (divisor == 0) {
    lua_pushnumber(L, 1 / lua_tonumber(L, 2));
    return 1;
  }
  dividend = bit_signed(bit_checkword(L, 1));
  /* Native IDIV overflows on INT_MIN/-1. Its mathematical remainder is zero. */
  lua_pushnumber(L, divisor == -1 ? 0 : dividend % divisor);
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
