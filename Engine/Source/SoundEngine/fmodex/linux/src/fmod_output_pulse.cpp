#include "fmod_settings.h"

#ifdef FMOD_SUPPORT_ALSA

#include "fmod_memory.h"
#include "fmod_output_pulse.h"
#include "fmod_soundi.h"
#include "fmod_string.h"
#include "fmod_systemi.h"

#include <dlfcn.h>
#include <limits.h>
#include <pulse/error.h>

// glibc; declared here rather than via <errno.h> so no _GNU_SOURCE is needed.
extern char* program_invocation_short_name;

namespace FMOD
{

// The stream's application name, as shown by pavucontrol and friends. The
// engine owns the real name -- it is whatever title it gave the window -- so
// ask SDL at runtime rather than duplicating the string here. SDL is resolved
// with dlsym because the SoundEngine must not depend on the video stack at
// build time; if no window exists yet (or SDL is absent), fall back to the
// executable's own name.
static const char* PulseStreamName()
{
    typedef void** (*GetWindowsFn)(int*);
    typedef const char* (*GetTitleFn)(void*);
    typedef void (*FreeFn)(void*);

    GetWindowsFn getWindows = (GetWindowsFn)dlsym(RTLD_DEFAULT, "SDL_GetWindows");
    GetTitleFn getTitle = (GetTitleFn)dlsym(RTLD_DEFAULT, "SDL_GetWindowTitle");
    FreeFn sdlFree = (FreeFn)dlsym(RTLD_DEFAULT, "SDL_free");
    const char* title = 0;

    if (getWindows && getTitle && sdlFree)
    {
        int count = 0;
        void** windows = getWindows(&count);
        if (windows)
        {
            if (count > 0)
            {
                // The title is owned by the window, not the returned array;
                // pa_simple_new only needs it for the duration of the call.
                title = getTitle(windows[0]);
            }
            sdlFree(windows);
        }
    }

    if (title && title[0])
    {
        return title;
    }
    return program_invocation_short_name;
}

FMOD_OUTPUT_DESCRIPTION_EX pulseoutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputPulse::getDescriptionEx()
{
    FMOD_memset(&pulseoutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    pulseoutput.name          = "FMOD PulseAudio Output";
    pulseoutput.version       = 0x00010100;
    pulseoutput.polling       = true;
    pulseoutput.getnumdrivers = &OutputPulse::getNumDriversCallback;
    pulseoutput.getdrivername = &OutputPulse::getDriverNameCallback;
    pulseoutput.getdrivercaps = &OutputPulse::getDriverCapsCallback;
    pulseoutput.init          = &OutputPulse::initCallback;
    pulseoutput.close         = &OutputPulse::closeCallback;
    pulseoutput.getposition   = &OutputPulse::getPositionCallback;
    pulseoutput.lock          = &OutputPulse::lockCallback;

    // FMOD Ex has no pulse FMOD_OUTPUTTYPE, so this registers under the ALSA
    // slot: autodetect returns FMOD_OUTPUTTYPE_ALSA and gets this output.
    pulseoutput.mType         = FMOD_OUTPUTTYPE_ALSA;
    pulseoutput.mSize         = sizeof(OutputPulse);

    return &pulseoutput;
}

void *OutputPulse::writerThread(void *param)
{
    OutputPulse *pulse      = (OutputPulse *)param;
    unsigned int framebytes = pulse->mBlockLengthBytes / pulse->mBlockLength;
    int          block      = 0;

    while (!pulse->mWriterQuit)
    {
        char *data = pulse->mBuffer + block * pulse->mBlockLengthBytes;

        if (pulse->mPulse)
        {
            int error = 0;

            // Blocking: returns once the server has accepted the whole block,
            // which paces this thread at the stream rate -- the same role
            // snd_pcm_writei's blocking plays for the ALSA path.
            if (pa_simple_write(pulse->mPulse, data, pulse->mBlockLengthBytes, &error) < 0)
            {
                return 0;
            }
        }
        else if (pulse->mAlsa)
        {
            snd_pcm_uframes_t remaining = pulse->mBlockLength;

            while (remaining && !pulse->mWriterQuit)
            {
                snd_pcm_sframes_t written = snd_pcm_writei(pulse->mAlsa, data, remaining);
                if (written < 0)
                {
                    if (snd_pcm_recover(pulse->mAlsa, (int)written, 1) < 0)
                    {
                        return 0;
                    }
                    continue;
                }

                data      += written * framebytes;
                remaining -= written;
            }
        }

        block = (block + 1) % pulse->mNumBlocks;
        __atomic_add_fetch(&pulse->mPosition, pulse->mBlockLength, __ATOMIC_RELEASE);
    }

    return 0;
}

FMOD_RESULT OutputPulse::getNumDrivers(int *numdrivers)
{
    *numdrivers = 1;

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::getDriverName(int driver, char *name, int namelen)
{
    if (!name)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    FMOD_strncpy(name, "PulseAudio Default", namelen);

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::getDriverCaps(int id, FMOD_CAPS *caps)
{
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL);
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_FORMAT_PCM16);

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    Plugin::init();

    if (*outputrate <= 0 || outputchannels <= 0)
    {
        return FMOD_ERR_OUTPUT_FORMAT;
    }

    if (dspbufferlength <= 0 || dspnumbuffers <= 0)
    {
        return FMOD_ERR_OUTPUT_FORMAT;
    }

    mPulse    = 0;
    mAlsa     = 0;
    mChannels = outputchannels;

    // Primary: talk libpulse directly. Reaches pipewire-pulse / PulseAudio with
    // no asound.conf, which is the whole point -- FMOD's ALSA "default" path is
    // silent under PipeWire.
    pa_sample_spec ss;
    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = (unsigned int)*outputrate;
    ss.channels = (unsigned char)outputchannels;

    int paError = 0;
    mPulse = pa_simple_new(NULL, PulseStreamName(), PA_STREAM_PLAYBACK, NULL, "Game", &ss, NULL, NULL, &paError);

    if (!mPulse)
    {
        // Fallback: raw ALSA, so a box with no pulse/pipewire server still gets
        // sound rather than silence. Mirrors OutputALSA exactly.
        if (snd_pcm_open(&mAlsa, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            mAlsa = 0;
            return FMOD_ERR_OUTPUT_INIT;
        }

        const unsigned long long frames =
            (unsigned long long)dspbufferlength * (unsigned long long)dspnumbuffers;
        if (frames > ULLONG_MAX / 1000000ull)
        {
            snd_pcm_close(mAlsa);
            mAlsa = 0;
            return FMOD_ERR_OUTPUT_FORMAT;
        }

        const unsigned long long latencyus = frames * 1000000ull / (unsigned long long)*outputrate;
        if (latencyus > (unsigned long long)UINT_MAX)
        {
            snd_pcm_close(mAlsa);
            mAlsa = 0;
            return FMOD_ERR_OUTPUT_FORMAT;
        }

        if (snd_pcm_set_params(mAlsa, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, outputchannels, *outputrate, 1, (unsigned int)latencyus) < 0)
        {
            snd_pcm_close(mAlsa);
            mAlsa = 0;
            return FMOD_ERR_OUTPUT_FORMAT;
        }
    }

    *outputformat = FMOD_SOUND_FORMAT_PCM16;

    mBlockLength = dspbufferlength;
    mNumBlocks   = dspnumbuffers;
    mPosition    = 0;
    mWriterQuit  = false;

    SoundI::getBytesFromSamples(mBlockLength, &mBlockLengthBytes, outputchannels, *outputformat);

    const unsigned long long bufferbytes = (unsigned long long)mBlockLengthBytes * (unsigned long long)mNumBlocks;
    if (bufferbytes > (unsigned long long)INT_MAX)
    {
        close();
        return FMOD_ERR_MEMORY;
    }

    mBuffer = (char *)FMOD_Memory_Calloc((int)bufferbytes);
    if (!mBuffer)
    {
        close();
        return FMOD_ERR_MEMORY;
    }

    if (pthread_create(&mWriterThread, NULL, &OutputPulse::writerThread, this))
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
        close();
        return FMOD_ERR_OUTPUT_INIT;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::close()
{
    if (mPulse || mAlsa)
    {
        mWriterQuit = true;
        pthread_join(mWriterThread, NULL);
    }

    if (mPulse)
    {
        int error = 0;
        pa_simple_drain(mPulse, &error);
        pa_simple_free(mPulse);
        mPulse = 0;
    }

    if (mAlsa)
    {
        snd_pcm_drop(mAlsa);
        snd_pcm_close(mAlsa);
        mAlsa = 0;
    }

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::getPosition(unsigned int *pcm)
{
    *pcm = __atomic_load_n(&mPosition, __ATOMIC_ACQUIRE);

    return FMOD_OK;
}

FMOD_RESULT OutputPulse::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    unsigned int bufferbytes = mBlockLengthBytes * mNumBlocks;

    if (!mBuffer || bufferbytes == 0)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    offset %= bufferbytes;

    if (offset + length > bufferbytes)
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = mBuffer;
        *len1 = bufferbytes - offset;
        *len2 = length - (bufferbytes - offset);
    }
    else
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = NULL;
        *len1 = length;
        *len2 = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK OutputPulse::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputPulse *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputPulse::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputPulse *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputPulse::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputPulse *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputPulse::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputPulse *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputPulse::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputPulse *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputPulse::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputPulse *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputPulse::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputPulse *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}

#endif
