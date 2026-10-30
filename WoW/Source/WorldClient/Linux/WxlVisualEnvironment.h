#ifndef WOW_SOURCE_WORLDCLIENT_LINUX_WXLVISUALENVIRONMENT_H
#define WOW_SOURCE_WORLDCLIENT_LINUX_WXLVISUALENVIRONMENT_H

// Reads the client-local WarcraftXL.cfg profile for native Linux equivalents
// of Windows renderer settings. Environment values take precedence so launch
// scripts can still override one setting for diagnosis.
bool WxlProfileEnabled(const char* key, bool fallback);
float WxlProfileNumber(const char* key, float fallback, float minimum, float maximum);

// Scales the linear fog range for a single world draw and restores the original
// values on exit. The caller must provide whether the camera is underwater.
struct WxlWorldFogScope {
    WxlWorldFogScope(bool underwater);
    ~WxlWorldFogScope();
    WxlWorldFogScope(const WxlWorldFogScope&) = delete;
    WxlWorldFogScope& operator=(const WxlWorldFogScope&) = delete;
private:
    float m_outdoorEnd = 0.0f;
    float m_interiorEnd = 0.0f;
    bool m_active = false;
};

#endif
