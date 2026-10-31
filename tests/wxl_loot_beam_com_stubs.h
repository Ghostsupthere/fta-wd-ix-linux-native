/*
 * Copyright (C) 2002-2003 Jason Edmeades
 *                         Raphael Junqueira
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
// Test-only strict COM doubles; declarations match the supplied DXVK D3D9 interfaces.
#pragma once
#include <d3d9.h>
#include <cstdlib>
struct StrictIDirect3DDevice9 : IDirect3DDevice9 {
STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) override { std::abort(); }
STDMETHOD_(ULONG,AddRef)(THIS) override { std::abort(); }
STDMETHOD_(ULONG,Release)(THIS) override { std::abort(); }
STDMETHOD(TestCooperativeLevel)(THIS) override { std::abort(); }
STDMETHOD_(UINT, GetAvailableTextureMem)(THIS) override { std::abort(); }
STDMETHOD(EvictManagedResources)(THIS) override { std::abort(); }
STDMETHOD(GetDirect3D)(THIS_ IDirect3D9** ppD3D9) override { std::abort(); }
STDMETHOD(GetDeviceCaps)(THIS_ D3DCAPS9* pCaps) override { std::abort(); }
STDMETHOD(GetDisplayMode)(THIS_ UINT iSwapChain, D3DDISPLAYMODE* pMode) override { std::abort(); }
STDMETHOD(GetCreationParameters)(THIS_ D3DDEVICE_CREATION_PARAMETERS *pParameters) override { std::abort(); }
STDMETHOD(SetCursorProperties)(THIS_ UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override { std::abort(); }
STDMETHOD_(void, SetCursorPosition)(THIS_ int X,int Y, DWORD Flags) override { std::abort(); }
STDMETHOD_(WINBOOL, ShowCursor)(THIS_ WINBOOL bShow) override { std::abort(); }
STDMETHOD(CreateAdditionalSwapChain)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override { std::abort(); }
STDMETHOD(GetSwapChain)(THIS_ UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override { std::abort(); }
STDMETHOD_(UINT, GetNumberOfSwapChains)(THIS) override { std::abort(); }
STDMETHOD(Reset)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters) override { std::abort(); }
STDMETHOD(Present)(THIS_ const RECT *src_rect, const RECT *dst_rect,
            HWND dst_window_override, const RGNDATA *dirty_region) override { std::abort(); }
STDMETHOD(GetBackBuffer)(THIS_ UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override { std::abort(); }
STDMETHOD(GetRasterStatus)(THIS_ UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override { std::abort(); }
STDMETHOD(SetDialogBoxMode)(THIS_ WINBOOL bEnableDialogs) override { std::abort(); }
STDMETHOD_(void, SetGammaRamp)(THIS_ UINT swapchain_idx, DWORD flags, const D3DGAMMARAMP *ramp) override { std::abort(); }
STDMETHOD_(void, GetGammaRamp)(THIS_ UINT iSwapChain, D3DGAMMARAMP* pRamp) override { std::abort(); }
STDMETHOD(CreateTexture)(THIS_ UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateVolumeTexture)(THIS_ UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateCubeTexture)(THIS_ UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateVertexBuffer)(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateIndexBuffer)(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateRenderTarget)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(CreateDepthStencilSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINBOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(UpdateSurface)(THIS_ IDirect3DSurface9 *src_surface, const RECT *src_rect,
            IDirect3DSurface9 *dst_surface, const POINT *dst_point) override { std::abort(); }
STDMETHOD(UpdateTexture)(THIS_ IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override { std::abort(); }
STDMETHOD(GetRenderTargetData)(THIS_ IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override { std::abort(); }
STDMETHOD(GetFrontBufferData)(THIS_ UINT iSwapChain, IDirect3DSurface9* pDestSurface) override { std::abort(); }
STDMETHOD(StretchRect)(THIS_ IDirect3DSurface9 *src_surface, const RECT *src_rect,
            IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) override { std::abort(); }
STDMETHOD(ColorFill)(THIS_ IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override { std::abort(); }
STDMETHOD(CreateOffscreenPlainSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override { std::abort(); }
STDMETHOD(SetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override { std::abort(); }
STDMETHOD(GetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override { std::abort(); }
STDMETHOD(SetDepthStencilSurface)(THIS_ IDirect3DSurface9* pNewZStencil) override { std::abort(); }
STDMETHOD(GetDepthStencilSurface)(THIS_ IDirect3DSurface9** ppZStencilSurface) override { std::abort(); }
STDMETHOD(BeginScene)(THIS) override { std::abort(); }
STDMETHOD(EndScene)(THIS) override { std::abort(); }
STDMETHOD(Clear)(THIS_ DWORD rect_count, const D3DRECT *rects, DWORD flags,
            D3DCOLOR color, float z, DWORD stencil) override { std::abort(); }
STDMETHOD(SetTransform)(THIS_ D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override { std::abort(); }
STDMETHOD(GetTransform)(THIS_ D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override { std::abort(); }
STDMETHOD(MultiplyTransform)(THIS_ D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override { std::abort(); }
STDMETHOD(SetViewport)(THIS_ const D3DVIEWPORT9 *viewport) override { std::abort(); }
STDMETHOD(GetViewport)(THIS_ D3DVIEWPORT9* pViewport) override { std::abort(); }
STDMETHOD(SetMaterial)(THIS_ const D3DMATERIAL9 *material) override { std::abort(); }
STDMETHOD(GetMaterial)(THIS_ D3DMATERIAL9* pMaterial) override { std::abort(); }
STDMETHOD(SetLight)(THIS_ DWORD index, const D3DLIGHT9 *light) override { std::abort(); }
STDMETHOD(GetLight)(THIS_ DWORD Index, D3DLIGHT9*) override { std::abort(); }
STDMETHOD(LightEnable)(THIS_ DWORD Index, WINBOOL Enable) override { std::abort(); }
STDMETHOD(GetLightEnable)(THIS_ DWORD Index, WINBOOL* pEnable) override { std::abort(); }
STDMETHOD(SetClipPlane)(THIS_ DWORD index, const float *plane) override { std::abort(); }
STDMETHOD(GetClipPlane)(THIS_ DWORD Index, float* pPlane) override { std::abort(); }
STDMETHOD(SetRenderState)(THIS_ D3DRENDERSTATETYPE State, DWORD Value) override { std::abort(); }
STDMETHOD(GetRenderState)(THIS_ D3DRENDERSTATETYPE State, DWORD* pValue) override { std::abort(); }
STDMETHOD(CreateStateBlock)(THIS_ D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override { std::abort(); }
STDMETHOD(BeginStateBlock)(THIS) override { std::abort(); }
STDMETHOD(EndStateBlock)(THIS_ IDirect3DStateBlock9** ppSB) override { std::abort(); }
STDMETHOD(SetClipStatus)(THIS_ const D3DCLIPSTATUS9 *clip_status) override { std::abort(); }
STDMETHOD(GetClipStatus)(THIS_ D3DCLIPSTATUS9* pClipStatus) override { std::abort(); }
STDMETHOD(GetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9** ppTexture) override { std::abort(); }
STDMETHOD(SetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9* pTexture) override { std::abort(); }
STDMETHOD(GetTextureStageState)(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override { std::abort(); }
STDMETHOD(SetTextureStageState)(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override { std::abort(); }
STDMETHOD(GetSamplerState)(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override { std::abort(); }
STDMETHOD(SetSamplerState)(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override { std::abort(); }
STDMETHOD(ValidateDevice)(THIS_ DWORD* pNumPasses) override { std::abort(); }
STDMETHOD(SetPaletteEntries)(THIS_ UINT palette_idx, const PALETTEENTRY *entries) override { std::abort(); }
STDMETHOD(GetPaletteEntries)(THIS_ UINT PaletteNumber,PALETTEENTRY* pEntries) override { std::abort(); }
STDMETHOD(SetCurrentTexturePalette)(THIS_ UINT PaletteNumber) override { std::abort(); }
STDMETHOD(GetCurrentTexturePalette)(THIS_ UINT *PaletteNumber) override { std::abort(); }
STDMETHOD(SetScissorRect)(THIS_ const RECT *rect) override { std::abort(); }
STDMETHOD(GetScissorRect)(THIS_ RECT* pRect) override { std::abort(); }
STDMETHOD(SetSoftwareVertexProcessing)(THIS_ WINBOOL bSoftware) override { std::abort(); }
STDMETHOD_(WINBOOL, GetSoftwareVertexProcessing)(THIS) override { std::abort(); }
STDMETHOD(SetNPatchMode)(THIS_ float nSegments) override { std::abort(); }
STDMETHOD_(float, GetNPatchMode)(THIS) override { std::abort(); }
STDMETHOD(DrawPrimitive)(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override { std::abort(); }
STDMETHOD(DrawIndexedPrimitive)(THIS_ D3DPRIMITIVETYPE, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override { std::abort(); }
STDMETHOD(DrawPrimitiveUP)(THIS_ D3DPRIMITIVETYPE primitive_type,
            UINT primitive_count, const void *data, UINT stride) override { std::abort(); }
STDMETHOD(DrawIndexedPrimitiveUP)(THIS_ D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count,
            UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) override { std::abort(); }
STDMETHOD(ProcessVertices)(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override { std::abort(); }
STDMETHOD(CreateVertexDeclaration)(THIS_ const D3DVERTEXELEMENT9 *elements,
            IDirect3DVertexDeclaration9 **declaration) override { std::abort(); }
STDMETHOD(SetVertexDeclaration)(THIS_ IDirect3DVertexDeclaration9* pDecl) override { std::abort(); }
STDMETHOD(GetVertexDeclaration)(THIS_ IDirect3DVertexDeclaration9** ppDecl) override { std::abort(); }
STDMETHOD(SetFVF)(THIS_ DWORD FVF) override { std::abort(); }
STDMETHOD(GetFVF)(THIS_ DWORD* pFVF) override { std::abort(); }
STDMETHOD(CreateVertexShader)(THIS_ const DWORD *byte_code, IDirect3DVertexShader9 **shader) override { std::abort(); }
STDMETHOD(SetVertexShader)(THIS_ IDirect3DVertexShader9* pShader) override { std::abort(); }
STDMETHOD(GetVertexShader)(THIS_ IDirect3DVertexShader9** ppShader) override { std::abort(); }
STDMETHOD(SetVertexShaderConstantF)(THIS_ UINT reg_idx, const float *data, UINT count) override { std::abort(); }
STDMETHOD(GetVertexShaderConstantF)(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount) override { std::abort(); }
STDMETHOD(SetVertexShaderConstantI)(THIS_ UINT reg_idx, const int *data, UINT count) override { std::abort(); }
STDMETHOD(GetVertexShaderConstantI)(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount) override { std::abort(); }
STDMETHOD(SetVertexShaderConstantB)(THIS_ UINT reg_idx, const WINBOOL *data, UINT count) override { std::abort(); }
STDMETHOD(GetVertexShaderConstantB)(THIS_ UINT StartRegister, WINBOOL* pConstantData, UINT BoolCount) override { std::abort(); }
STDMETHOD(SetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override { std::abort(); }
STDMETHOD(GetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* OffsetInBytes, UINT* pStride) override { std::abort(); }
STDMETHOD(SetStreamSourceFreq)(THIS_ UINT StreamNumber, UINT Divider) override { std::abort(); }
STDMETHOD(GetStreamSourceFreq)(THIS_ UINT StreamNumber, UINT* Divider) override { std::abort(); }
STDMETHOD(SetIndices)(THIS_ IDirect3DIndexBuffer9* pIndexData) override { std::abort(); }
STDMETHOD(GetIndices)(THIS_ IDirect3DIndexBuffer9** ppIndexData) override { std::abort(); }
STDMETHOD(CreatePixelShader)(THIS_ const DWORD *byte_code, IDirect3DPixelShader9 **shader) override { std::abort(); }
STDMETHOD(SetPixelShader)(THIS_ IDirect3DPixelShader9* pShader) override { std::abort(); }
STDMETHOD(GetPixelShader)(THIS_ IDirect3DPixelShader9** ppShader) override { std::abort(); }
STDMETHOD(SetPixelShaderConstantF)(THIS_ UINT reg_idx, const float *data, UINT count) override { std::abort(); }
STDMETHOD(GetPixelShaderConstantF)(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount) override { std::abort(); }
STDMETHOD(SetPixelShaderConstantI)(THIS_ UINT reg_idx, const int *data, UINT count) override { std::abort(); }
STDMETHOD(GetPixelShaderConstantI)(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount) override { std::abort(); }
STDMETHOD(SetPixelShaderConstantB)(THIS_ UINT reg_idx, const WINBOOL *data, UINT count) override { std::abort(); }
STDMETHOD(GetPixelShaderConstantB)(THIS_ UINT StartRegister, WINBOOL* pConstantData, UINT BoolCount) override { std::abort(); }
STDMETHOD(DrawRectPatch)(THIS_ UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) override { std::abort(); }
STDMETHOD(DrawTriPatch)(THIS_ UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) override { std::abort(); }
STDMETHOD(DeletePatch)(THIS_ UINT Handle) override { std::abort(); }
STDMETHOD(CreateQuery)(THIS_ D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override { std::abort(); }
};
struct StrictIDirect3DStateBlock9 : IDirect3DStateBlock9 {
STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) override { std::abort(); }
STDMETHOD_(ULONG,AddRef)(THIS) override { std::abort(); }
STDMETHOD_(ULONG,Release)(THIS) override { std::abort(); }
STDMETHOD(GetDevice)(THIS_ struct IDirect3DDevice9** ppDevice) override { std::abort(); }
STDMETHOD(Capture)(THIS) override { std::abort(); }
STDMETHOD(Apply)(THIS) override { std::abort(); }
};
struct StrictIDirect3DSurface9 : IDirect3DSurface9 {
STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) override { std::abort(); }
STDMETHOD_(ULONG,AddRef)(THIS) override { std::abort(); }
STDMETHOD_(ULONG,Release)(THIS) override { std::abort(); }
STDMETHOD(GetDevice)(THIS_ struct IDirect3DDevice9** ppDevice) override { std::abort(); }
STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, const void *data, DWORD data_size, DWORD flags) override { std::abort(); }
STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) override { std::abort(); }
STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) override { std::abort(); }
STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) override { std::abort(); }
STDMETHOD_(DWORD, GetPriority)(THIS) override { std::abort(); }
STDMETHOD_(void, PreLoad)(THIS) override { std::abort(); }
STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) override { std::abort(); }
STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer) override { std::abort(); }
STDMETHOD(GetDesc)(THIS_ D3DSURFACE_DESC* pDesc) override { std::abort(); }
STDMETHOD(LockRect)(THIS_ D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags) override { std::abort(); }
STDMETHOD(UnlockRect)(THIS) override { std::abort(); }
STDMETHOD(GetDC)(THIS_ HDC* phdc) override { std::abort(); }
STDMETHOD(ReleaseDC)(THIS_ HDC hdc) override { std::abort(); }
};
struct StrictIDirect3DVertexBuffer9 : IDirect3DVertexBuffer9 {
STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) override { std::abort(); }
STDMETHOD_(ULONG,AddRef)(THIS) override { std::abort(); }
STDMETHOD_(ULONG,Release)(THIS) override { std::abort(); }
STDMETHOD(GetDevice)(THIS_ struct IDirect3DDevice9** ppDevice) override { std::abort(); }
STDMETHOD(SetPrivateData)(THIS_ REFGUID guid, const void *data, DWORD data_size, DWORD flags) override { std::abort(); }
STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) override { std::abort(); }
STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) override { std::abort(); }
STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) override { std::abort(); }
STDMETHOD_(DWORD, GetPriority)(THIS) override { std::abort(); }
STDMETHOD_(void, PreLoad)(THIS) override { std::abort(); }
STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) override { std::abort(); }
STDMETHOD(Lock)(THIS_ UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) override { std::abort(); }
STDMETHOD(Unlock)(THIS) override { std::abort(); }
STDMETHOD(GetDesc)(THIS_ D3DVERTEXBUFFER_DESC* pDesc) override { std::abort(); }
};
