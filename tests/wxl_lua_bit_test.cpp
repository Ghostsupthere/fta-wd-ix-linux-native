#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
uint64_t OsGetAsyncTimeClocks(){return 0;}
int64_t OsGetAsyncClocksPerSecond(){return 1;}
void OsValidateFunctionPointer(const void* p){assert(p);}
int main(){
    lua_State* L=luaL_newstate();assert(L);luaopen_base(L);lua_settop(L,0);luaopen_bit(L);lua_settop(L,0);
    const int status=luaL_dostring(L,R"(
        -- Exact colour extraction used for Cheese Art of War / Blood Surge.
        local white=0xFFFFFFFF
        local r=bit.rshift(bit.lshift(white,8),24)
        local g=bit.rshift(bit.lshift(white,16),24)
        local b=bit.band(white,0xff)
        assert(r==255 and g==255 and b==255,'white proc RGB became '..r..','..g..','..b)
        assert(bit.band(0xFFF4F4F4,0xff)==244)
        assert(bit.bnot(0)==0xFFFFFFFF and bit.band(-1,0xFF)==255)
        assert(bit.bor(0x80000000,1)==0x80000001)
        assert(bit.bxor(0xFFFFFFFF,0x80000000)==0x7FFFFFFF)
        assert(bit.band(0x100000001,0xFF)==1 and bit.band(-4294967297,0xFF)==255)
        assert(bit.lshift(0xFFFFFFFF,1)==0xFFFFFFFE)
        assert(bit.lshift(1,32)==1 and bit.lshift(1,33)==2 and bit.lshift(1,-1)==0x80000000)
        assert(bit.rshift(0x80000000,31)==1 and bit.rshift(0xFFFFFFFF,8)==0xFFFFFF)
        assert(bit.rshift(0xFFFFFFFF,0)==-1) -- Preserve this client's signed rshift result at count 0.
        assert(bit.arshift(0x80000000,31)==-1 and bit.arshift(0x80000000,0)==-2147483648)
        assert(bit.arshift(0x7FFFFFFF,31)==0)
        assert(bit.band(1.9,1)==1 and bit.band(-1.9,255)==255)
        assert(not pcall(bit.band,0/0,1) and not pcall(bit.lshift,1,1/0))
        assert(not pcall(bit.band,'bad',1))
        assert(bit.mod(0x80000000,-1)==0 and bit.mod(-7,3)==-1)
    )");
    if(status)std::fprintf(stderr,"%s\n",lua_tostring(L,-1));
    lua_close(L);return status?1:0;
}
