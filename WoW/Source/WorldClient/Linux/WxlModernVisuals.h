#pragma once
namespace wxl::grass::environment { struct Values; }
namespace Liquid {class CMaterialSettings;}
// Returns true only when native fog may be replaced for this world frame.
bool WxlBeginVisualWorld(bool underwater,const float* target=nullptr);
void WxlCaptureVisualCamera();
#if defined(WOWDECOMP_LINUX)
namespace wxl::atmosphere { struct PlayerLantern; struct CorpseLightList; struct SpellLightList; }
const wxl::atmosphere::PlayerLantern& WxlFrameLantern();
const wxl::atmosphere::CorpseLightList& WxlFrameCorpseLights();
float WxlSurfaceWetness();
const float* WxlSurfaceCloud();
const wxl::atmosphere::SpellLightList& WxlFrameSpellLights();
#endif
void WxlFinishVisualWorld();
// Draws the glare and the fog once the liquid pass is done, then fits the stock fog
// the see-through M2 batches use (TransparentFog). No-op unless it can run this frame.
void WxlBeginTransparentFog();
// True when WxlBeginTransparentFog already drew this frame's glare.
bool WxlGlareDrawn();
void WxlBeginModernWater();
void WxlTagModernWater(const Liquid::CMaterialSettings* settings);
void WxlEndModernWater();
void WxlReleaseModernVisuals();
#if defined(WOWDECOMP_LINUX)
void WxlSetAtmosphereOptions(const wxl::grass::environment::Values& values);
bool WxlAtmosphereSupported();
const char* WxlAtmosphereStatus();
#endif
