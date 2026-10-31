#ifndef WOW_SOURCE_UI_LINUX_WXLREQUIREDADDONS_H
#define WOW_SOURCE_UI_LINUX_WXLREQUIREDADDONS_H

// Native port of WarcraftXL's wxl-required-addons: an add-on whose TOC carries
// "## X-WarcraftXL-Required: 1" is force-enabled, cannot be disabled through DisableAddOn /
// DisableAllAddOns, and has its checkbox locked in the stock AddOns screen and ACP. The policy is a
// Lua script the Windows core runs once for each FrameScript context; here it is executed right
// after GlueXML and FrameXML finish loading, so the character-select context enforces it before the
// world loads and the in-world context enforces it for ACP.

#if defined(WOWDECOMP_LINUX)

void WxlRunRequiredAddonsPolicy();

#endif  // WOWDECOMP_LINUX

#endif
