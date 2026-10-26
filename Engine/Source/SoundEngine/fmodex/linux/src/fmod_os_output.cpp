#include "fmod_settings.h"

#include "fmod.h"
#include "fmod_output.h"
#include "fmod_output_alsa.h"
#include "fmod_output_pulse.h"
#include "fmod_pluginfactory.h"

// OFFSET: 0x00915FC0
FMOD_RESULT FMOD_OS_Output_Register(FMOD::PluginFactory *pluginfactory)
{
    #ifdef FMOD_SUPPORT_ALSA
    // Register the PulseAudio output under the ALSA type slot instead of the
    // stock OutputALSA: FMOD Ex has no pulse FMOD_OUTPUTTYPE, and the raw-ALSA
    // "default" PCM is silent under PipeWire. OutputPulse talks libpulse
    // directly and falls back to raw ALSA itself if no pulse server is present.
    pluginfactory->registerOutput(FMOD::OutputPulse::getDescriptionEx());
    #endif

    return FMOD_OK;
}

// OFFSET: 0x00916030
FMOD_RESULT FMOD_OS_Output_GetDefault(FMOD_OUTPUTTYPE *outputtype)
{
    if (!outputtype)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

#ifdef FMOD_SUPPORT_ALSA
    *outputtype = FMOD_OUTPUTTYPE_ALSA;
#else
    *outputtype = FMOD_OUTPUTTYPE_NOSOUND;
#endif

    return FMOD_OK;
}
