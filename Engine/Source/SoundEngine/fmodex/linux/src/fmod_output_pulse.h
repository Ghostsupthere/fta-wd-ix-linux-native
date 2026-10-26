#ifndef _FMOD_OUTPUT_PULSE_H
#define _FMOD_OUTPUT_PULSE_H

#include "fmod_settings.h"

#ifdef FMOD_SUPPORT_ALSA

#include "fmod_output_polled.h"

#include <pulse/simple.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

namespace FMOD
{
    // PulseAudio output for FMOD Ex. FMOD Ex ships no pulse/pipewire backend --
    // its Linux autodetect returns ALSA, which opens PCM "default" and fails
    // ENOENT under PipeWire, leaving the native client silent. This is a real
    // backend (not an ALSA-routing wrapper): it talks libpulse directly, so it
    // reaches pipewire-pulse / PulseAudio without any asound.conf. If the pulse
    // connection fails (no pulse/pipewire server) it falls back to raw ALSA so a
    // non-pulse box still gets sound rather than silence.
    //
    // Modelled on OutputALSA: an OutputPolled with a writer thread draining a
    // ring buffer the FMOD mixer fills through lock(), getPosition() reporting
    // frames consumed so the mixer knows how far ahead to render.
    class OutputPulse : public OutputPolled
    {
      private:

        pa_simple              *mPulse;
        snd_pcm_t              *mAlsa;      // fallback only
        pthread_t               mWriterThread;
        volatile bool           mWriterQuit;
        volatile unsigned int   mPosition;
        unsigned int            mBlockLength;
        unsigned int            mBlockLengthBytes;
        int                     mNumBlocks;
        int                     mChannels;
        char                   *mBuffer;

        static void            *writerThread(void *param);

      public:

        static FMOD_OUTPUT_DESCRIPTION_EX *getDescriptionEx();

        FMOD_RESULT              getNumDrivers(int *numdrivers);
        FMOD_RESULT              getDriverName(int driver, char *name, int namelen);
        FMOD_RESULT              getDriverCaps(int id, FMOD_CAPS *caps);
        FMOD_RESULT              init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata);
        FMOD_RESULT              close();
        FMOD_RESULT              getPosition(unsigned int *pcm);
        FMOD_RESULT              lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2);

        static FMOD_RESULT F_CALLBACK getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers);
        static FMOD_RESULT F_CALLBACK getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen);
        static FMOD_RESULT F_CALLBACK getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps);
        static FMOD_RESULT F_CALLBACK initCallback         (FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata);
        static FMOD_RESULT F_CALLBACK closeCallback        (FMOD_OUTPUT_STATE *output);
        static FMOD_RESULT F_CALLBACK getPositionCallback  (FMOD_OUTPUT_STATE *output, unsigned int *pcm);
        static FMOD_RESULT F_CALLBACK lockCallback         (FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2);
    };
}

#endif

#endif
