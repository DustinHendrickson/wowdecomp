/*
    AAudio output for FMOD Ex on Android (wowdecomp). See fmod_output_aaudio.h.
*/

#include <aaudio/AAudio.h>

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_memory.h"
#include "fmod_output_aaudio.h"
#include "fmod_soundi.h"
#include "fmod_systemi.h"

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX aaudiooutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputAAudio::getDescriptionEx()
{
    FMOD_memset(&aaudiooutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    aaudiooutput.name          = "FMOD AAudio Output";
    aaudiooutput.version       = 0x00010100;
    aaudiooutput.polling       = true;
    aaudiooutput.getnumdrivers = &OutputAAudio::getNumDriversCallback;
    aaudiooutput.getdrivername = &OutputAAudio::getDriverNameCallback;
    aaudiooutput.getdrivercaps = &OutputAAudio::getDriverCapsCallback;
    aaudiooutput.init          = &OutputAAudio::initCallback;
    aaudiooutput.close         = &OutputAAudio::closeCallback;
    aaudiooutput.start         = &OutputAAudio::startCallback;
    aaudiooutput.stop          = &OutputAAudio::stopCallback;
    aaudiooutput.gethandle     = &OutputAAudio::getHandleCallback;
    aaudiooutput.getposition   = &OutputAAudio::getPositionCallback;
    aaudiooutput.lock          = &OutputAAudio::lockCallback;
    aaudiooutput.mType         = FMOD_OUTPUTTYPE_AAUDIO;
    aaudiooutput.mSize         = sizeof(OutputAAudio);

    return &aaudiooutput;
}

int OutputAAudio::render(void *stream, void *userdata, void *audiodata, int frames)
{
    OutputAAudio *output = (OutputAAudio *)userdata;
    char         *dst = (char *)audiodata;
    unsigned int  bytes = frames * output->mFrameBytes;

    if (!output->mRunning || !output->mBuffer)
    {
        FMOD_memset(dst, 0, bytes);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    /* Copy out of the ring, wrapping at its end. */
    unsigned int remaining = bytes;

    while (remaining)
    {
        unsigned int chunk = output->mBufferBytes - output->mReadOffset;

        if (chunk > remaining)
        {
            chunk = remaining;
        }

        FMOD_memcpy(dst, output->mBuffer + output->mReadOffset, chunk);
        dst += chunk;
        remaining -= chunk;
        output->mReadOffset = (output->mReadOffset + chunk) % output->mBufferBytes;
    }

    __atomic_add_fetch(&output->mPlayed, frames, __ATOMIC_RELEASE);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void OutputAAudio::error(void *stream, void *userdata, int result)
{
    /* The stream is lost when the output device changes (headphones unplugged, a Bluetooth device connected). AAudio
       doesn't allow reopening from its own callback, so the mixer thread does it on its next getPosition. */
    if (result == AAUDIO_ERROR_DISCONNECTED)
    {
        ((OutputAAudio *)userdata)->mDisconnected = true;
    }
}

static aaudio_data_callback_result_t DataCallback(AAudioStream *stream, void *userdata, void *audiodata, int32_t frames)
{
    return OutputAAudio::render(stream, userdata, audiodata, frames);
}

static void ErrorCallback(AAudioStream *stream, void *userdata, aaudio_result_t result)
{
    OutputAAudio::error(stream, userdata, result);
}

bool OutputAAudio::openStream()
{
    AAudioStreamBuilder *builder = 0;

    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
    {
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setSampleRate(builder, mRate);
    AAudioStreamBuilder_setChannelCount(builder, mChannels);
    AAudioStreamBuilder_setFormat(builder, mFormat == FMOD_SOUND_FORMAT_PCMFLOAT ? AAUDIO_FORMAT_PCM_FLOAT : AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setDataCallback(builder, DataCallback, this);
    AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, this);

    AAudioStream *stream = 0;
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK)
    {
        return false;
    }

    mStream = stream;
    return true;
}

void OutputAAudio::closeStream()
{
    if (mStream)
    {
        AAudioStream_requestStop((AAudioStream *)mStream);
        AAudioStream_close((AAudioStream *)mStream);
        mStream = 0;
    }
}

FMOD_RESULT OutputAAudio::getNumDrivers(int *numdrivers)
{
    /* The device Android routes game audio to, whichever it is. */
    *numdrivers = 1;
    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::getDriverName(int id, char *name, int namelen)
{
    FMOD_strncpy(name, "Default Output", namelen);
    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::getDriverCaps(int id, FMOD_CAPS *caps)
{
    /* AAudio converts sample rates and channel counts to the device's. */
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL | FMOD_CAPS_OUTPUT_FORMAT_PCM16 | FMOD_CAPS_OUTPUT_FORMAT_PCMFLOAT);
    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    FMOD_RESULT  result;
    unsigned int bufferlength;
    int          numbuffers;

    Plugin::init();     /* Sets up gGlobal for this plugin's debug / file / memory access. */

    mStream     = 0;
    mBuffer     = 0;
    mReadOffset = 0;
    mPlayed     = 0;
    mRunning    = false;
    mDisconnected = false;

    if (*outputformat != FMOD_SOUND_FORMAT_PCMFLOAT)
    {
        *outputformat = FMOD_SOUND_FORMAT_PCM16;
    }

    mRate     = *outputrate;
    mChannels = outputchannels;
    mFormat   = *outputformat;

    result = mSystem->getDSPBufferSize(&bufferlength, &numbuffers);
    if (result != FMOD_OK)
    {
        return result;
    }

    SoundI::getBytesFromSamples(1, &mFrameBytes, outputchannels, *outputformat);
    SoundI::getBytesFromSamples(bufferlength * numbuffers, &mBufferBytes, outputchannels, *outputformat);

    mBuffer = (char *)FMOD_Memory_Calloc(mBufferBytes);
    if (!mBuffer)
    {
        return FMOD_ERR_MEMORY;
    }

    if (!openStream())
    {
        return FMOD_ERR_OUTPUT_INIT;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::close()
{
    mRunning = false;
    closeStream();

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::getHandle(void **handle)
{
    *handle = mStream;
    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::start()
{
    FMOD_RESULT result = OutputPolled::start();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = true;

    if (!mStream || AAudioStream_requestStart((AAudioStream *)mStream) != AAUDIO_OK)
    {
        mRunning = false;
        return FMOD_ERR_OUTPUT_DRIVERCALL;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::stop()
{
    FMOD_RESULT result = OutputPolled::stop();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = false;

    if (mStream)
    {
        AAudioStream_requestStop((AAudioStream *)mStream);
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::getPosition(unsigned int *pcm)
{
    /* The output device changed: a new stream on the new one, carrying on from the same ring position. */
    if (mDisconnected && mRunning)
    {
        mDisconnected = false;
        closeStream();

        if (openStream())
        {
            AAudioStream_requestStart((AAudioStream *)mStream);
        }
    }

    *pcm = __atomic_load_n(&mPlayed, __ATOMIC_ACQUIRE);
    return FMOD_OK;
}

FMOD_RESULT OutputAAudio::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    offset %= mBufferBytes;

    if (offset + length > mBufferBytes)
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = mBuffer;
        *len1 = mBufferBytes - offset;
        *len2 = length - (mBufferBytes - offset);
    }
    else
    {
        *ptr1 = mBuffer + offset;
        *ptr2 = 0;
        *len1 = length;
        *len2 = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK OutputAAudio::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputAAudio *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputAAudio::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputAAudio *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputAAudio::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputAAudio *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputAAudio::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputAAudio *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputAAudio::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAAudio *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputAAudio::startCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAAudio *)output)->start();
}

FMOD_RESULT F_CALLBACK OutputAAudio::stopCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAAudio *)output)->stop();
}

FMOD_RESULT F_CALLBACK OutputAAudio::getHandleCallback(FMOD_OUTPUT_STATE *output, void **handle)
{
    return ((OutputAAudio *)output)->getHandle(handle);
}

FMOD_RESULT F_CALLBACK OutputAAudio::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputAAudio *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputAAudio::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputAAudio *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}
