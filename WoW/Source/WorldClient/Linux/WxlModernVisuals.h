#pragma once
namespace Liquid {class CMaterialSettings;}
// Returns true only when native fog may be replaced for this world frame.
bool WxlBeginVisualWorld(bool underwater,const float* target=nullptr);
void WxlCaptureVisualCamera();
void WxlFinishVisualWorld();
void WxlBeginModernWater();
void WxlTagModernWater(const Liquid::CMaterialSettings* settings);
void WxlEndModernWater();
void WxlReleaseModernVisuals();
