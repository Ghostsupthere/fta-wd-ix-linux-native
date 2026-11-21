#pragma once

#include <cstdint>

using HRESULT = int32_t;
using DWORD = uint32_t;
using BOOL = int32_t;
using UINT64 = uint64_t;

constexpr HRESULT D3D_OK = 0;
constexpr HRESULT S_FALSE = 1;
constexpr BOOL FALSE = 0;
constexpr DWORD D3DISSUE_BEGIN = 1;
constexpr DWORD D3DISSUE_END = 2;

enum D3DQUERYTYPE {
    D3DQUERYTYPE_TIMESTAMP,
    D3DQUERYTYPE_TIMESTAMPDISJOINT,
    D3DQUERYTYPE_TIMESTAMPFREQ,
};

struct IDirect3DQuery9 {
    virtual ~IDirect3DQuery9() = default;
    virtual HRESULT Issue(DWORD flags) = 0;
    virtual HRESULT GetData(void* data, DWORD size, DWORD flags) = 0;
    virtual void Release() = 0;
};

struct IDirect3DDevice9 {
    virtual ~IDirect3DDevice9() = default;
    virtual HRESULT CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** output) = 0;
};
