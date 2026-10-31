#ifndef WOW_SOURCE_UI_LINUX_WXLGAMEOBJECTEDITOR_H
#define WOW_SOURCE_UI_LINUX_WXLGAMEOBJECTEDITOR_H

// Native port of WarcraftXL's wxl-gameobject-editor: a server-authorized mouse transform gizmo
// (translate/rotate) for one existing GameObject, driven by the FTA control add-on through the
// WarcraftXL_GOEditor* Lua functions. The Windows module detours EndScene and window messages and
// edits the object through fixed client addresses; here the overlay draws from
// CGxDeviceD3d9Ex::ScenePresent, mouse input arrives through NativeVisualHooks::mouseInput, and the
// selected object is moved through the reconstructed CGGameObject_C types.

#if defined(WOWDECOMP_LINUX)

void WxlRegisterGameObjectEditorLua();
void WxlGameObjectEditorWorldLeave();

#endif  // WOWDECOMP_LINUX

#endif
