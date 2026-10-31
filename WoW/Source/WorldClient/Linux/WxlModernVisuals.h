#pragma once
namespace Liquid {class CMaterialSettings;}
// Returns true only when native fog may be replaced for this world frame.
bool WxlBeginVisualWorld(bool underwater,const float* target=nullptr);
void WxlCaptureVisualCamera();
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
