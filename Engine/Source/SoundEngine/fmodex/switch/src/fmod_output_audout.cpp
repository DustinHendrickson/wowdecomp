/*
    libnx audout output for FMOD Ex on the Nintendo Switch (wowdecomp). See fmod_output_audout.h.
*/

#include <switch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_memory.h"
#include "fmod_output_audout.h"
#include "fmod_soundi.h"
#include "fmod_systemi.h"

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX audoutoutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputAudout::getDescriptionEx()
{
    FMOD_memset(&audoutoutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    audoutoutput.name          = "FMOD Switch audout Output";
    audoutoutput.version       = 0x00010100;
    audoutoutput.polling       = true;
    audoutoutput.getnumdrivers = &OutputAudout::getNumDriversCallback;
    audoutoutput.getdrivername = &OutputAudout::getDriverNameCallback;
    audoutoutput.getdrivercaps = &OutputAudout::getDriverCapsCallback;
    audoutoutput.init          = &OutputAudout::initCallback;
    audoutoutput.close         = &OutputAudout::closeCallback;
    audoutoutput.start         = &OutputAudout::startCallback;
    audoutoutput.stop          = &OutputAudout::stopCallback;
    audoutoutput.gethandle     = &OutputAudout::getHandleCallback;
    audoutoutput.getposition   = &OutputAudout::getPositionCallback;
    audoutoutput.lock          = &OutputAudout::lockCallback;
    audoutoutput.mType         = FMOD_OUTPUTTYPE_AUDOUT;
    audoutoutput.mSize         = sizeof(OutputAudout);

    return &audoutoutput;
}

/* Copies the next mHwFrames frames out of the ring as stereo, wrapping at its end. */
void OutputAudout::fill(short *dst)
{
    for (unsigned int i = 0; i < mHwFrames; i++)
    {
        const short *frame = mBuffer + (size_t)mReadFrame * mChannels;

        if (mChannels == 1)
        {
            dst[0] = dst[1] = frame[0];
        }
        else if (mChannels == 2)
        {
            dst[0] = frame[0];
            dst[1] = frame[1];
        }
        else
        {
            /* FMOD's speaker order: front left, front right, center, LFE, then surrounds in left / right pairs. */
            int left  = frame[0];
            int right = frame[1];
            int center = mChannels > 2 ? frame[2] : 0;

            left  += center / 2;
            right += center / 2;

            for (unsigned int channel = 4; channel + 1 < mChannels; channel += 2)
            {
                left  += frame[channel] / 2;
                right += frame[channel + 1] / 2;
            }

            dst[0] = (short)(left  < -32768 ? -32768 : left  > 32767 ? 32767 : left);
            dst[1] = (short)(right < -32768 ? -32768 : right > 32767 ? 32767 : right);
        }

        dst += 2;
        mReadFrame = (mReadFrame + 1) % mBufferFrames;
    }

    __atomic_add_fetch(&mPlayed, mHwFrames, __ATOMIC_RELEASE);
}

void *OutputAudout::feederThread(void *param)
{
    OutputAudout *output = (OutputAudout *)param;

    /* Queue every buffer once, then refill each one as audout hands it back. */
    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        AudioOutBuffer *buffer = (AudioOutBuffer *)output->mHwDescriptor[i];
        output->fill((short *)output->mHwBuffer[i]);
        audoutAppendAudioOutBuffer(buffer);
    }

    while (output->mRunning)
    {
        AudioOutBuffer *released = 0;
        u32             count = 0;

        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 100000000ull)) || !released)
        {
            continue;
        }

        if (!output->mRunning)
        {
            break;
        }

        output->fill((short *)released->buffer);
        audoutAppendAudioOutBuffer(released);
    }

    return 0;
}

FMOD_RESULT OutputAudout::getNumDrivers(int *numdrivers)
{
    *numdrivers = 1;
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::getDriverName(int id, char *name, int namelen)
{
    FMOD_strncpy(name, "Nintendo Switch", namelen);
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::getDriverCaps(int id, FMOD_CAPS *caps)
{
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL | FMOD_CAPS_OUTPUT_FORMAT_PCM16);
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    FMOD_RESULT  result;
    unsigned int bufferlength;
    int          numbuffers;

    Plugin::init();     /* Sets up gGlobal for this plugin's debug / file / memory access. */

    mBuffer        = 0;
    mReadFrame     = 0;
    mPlayed        = 0;
    mRunning       = false;
    mThreadStarted = false;
    mAudoutStarted = false;
    mThread        = 0;
    mChannels      = outputchannels > 0 ? outputchannels : 2;

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        mHwBuffer[i]     = 0;
        mHwDescriptor[i] = 0;
    }

    *outputformat = FMOD_SOUND_FORMAT_PCM16;

    Result rc = audoutInitialize();

    if (R_FAILED(rc))
    {
        fprintf(stderr, "FMOD audout: audoutInitialize failed (0x%x, module %u, description %u)\n", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        return FMOD_ERR_OUTPUT_INIT;
    }

    rc = audoutStartAudioOut();

    if (R_FAILED(rc))
    {
        fprintf(stderr, "FMOD audout: audoutStartAudioOut failed (0x%x, module %u, description %u)\n", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        audoutExit();
        return FMOD_ERR_OUTPUT_INIT;
    }

    fprintf(stderr, "FMOD audout: %u Hz, %u channels, FMOD mixes %d channels\n", audoutGetSampleRate(), audoutGetChannelCount(), outputchannels);

    mAudoutStarted = true;
    *outputrate = (int)audoutGetSampleRate();

    result = mSystem->getDSPBufferSize(&bufferlength, &numbuffers);
    if (result != FMOD_OK)
    {
        return result;
    }

    mBufferFrames = bufferlength * numbuffers;
    mHwFrames     = bufferlength;
    mBuffer       = (short *)FMOD_Memory_Calloc(mBufferFrames * mChannels * sizeof(short));
    if (!mBuffer)
    {
        return FMOD_ERR_MEMORY;
    }

    /* audout buffers are shared with the audio service: page aligned, sizes in whole pages. */
    size_t hwBytes = ((size_t)mHwFrames * 2 * sizeof(short) + 0xFFF) & ~(size_t)0xFFF;

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        AudioOutBuffer *buffer = (AudioOutBuffer *)calloc(1, sizeof(AudioOutBuffer));

        mHwBuffer[i] = aligned_alloc(0x1000, hwBytes);
        mHwDescriptor[i] = buffer;

        if (!buffer || !mHwBuffer[i])
        {
            return FMOD_ERR_MEMORY;
        }

        FMOD_memset(mHwBuffer[i], 0, hwBytes);
        buffer->buffer      = mHwBuffer[i];
        buffer->buffer_size = hwBytes;
        buffer->data_size   = (u64)mHwFrames * 2 * sizeof(short);
        buffer->data_offset = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAudout::close()
{
    mRunning = false;

    if (mThreadStarted)
    {
        pthread_join((pthread_t)mThread, 0);
        mThreadStarted = false;
    }

    if (mAudoutStarted)
    {
        audoutStopAudioOut();
        audoutExit();
        mAudoutStarted = false;
    }

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        free(mHwBuffer[i]);
        free(mHwDescriptor[i]);
        mHwBuffer[i]     = 0;
        mHwDescriptor[i] = 0;
    }

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAudout::getHandle(void **handle)
{
    *handle = 0;
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::start()
{
    FMOD_RESULT result = OutputPolled::start();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = true;

    pthread_attr_t attributes;
    pthread_t      thread;

    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 64 * 1024);
    int created = pthread_create(&thread, &attributes, feederThread, this);
    pthread_attr_destroy(&attributes);

    if (created)
    {
        mRunning = false;
        return FMOD_ERR_OUTPUT_DRIVERCALL;
    }

    mThread        = (void *)thread;
    mThreadStarted = true;
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::stop()
{
    FMOD_RESULT result = OutputPolled::stop();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = false;

    if (mThreadStarted)
    {
        pthread_join((pthread_t)mThread, 0);
        mThreadStarted = false;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAudout::getPosition(unsigned int *pcm)
{
    *pcm = __atomic_load_n(&mPlayed, __ATOMIC_ACQUIRE);
    return FMOD_OK;
}

FMOD_RESULT OutputAudout::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    unsigned int bufferBytes = mBufferFrames * mChannels * sizeof(short);
    char        *buffer = (char *)mBuffer;

    offset %= bufferBytes;

    if (offset + length > bufferBytes)
    {
        *ptr1 = buffer + offset;
        *ptr2 = buffer;
        *len1 = bufferBytes - offset;
        *len2 = length - (bufferBytes - offset);
    }
    else
    {
        *ptr1 = buffer + offset;
        *ptr2 = 0;
        *len1 = length;
        *len2 = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK OutputAudout::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputAudout *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputAudout::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputAudout *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputAudout::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputAudout *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputAudout::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputAudout *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputAudout::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudout *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputAudout::startCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudout *)output)->start();
}

FMOD_RESULT F_CALLBACK OutputAudout::stopCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudout *)output)->stop();
}

FMOD_RESULT F_CALLBACK OutputAudout::getHandleCallback(FMOD_OUTPUT_STATE *output, void **handle)
{
    return ((OutputAudout *)output)->getHandle(handle);
}

FMOD_RESULT F_CALLBACK OutputAudout::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputAudout *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputAudout::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputAudout *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}
