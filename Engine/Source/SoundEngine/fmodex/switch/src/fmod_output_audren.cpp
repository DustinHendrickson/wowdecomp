/*
    Audio renderer output for FMOD Ex on the Nintendo Switch (wowdecomp). See fmod_output_audren.h.
*/

#include <switch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_memory.h"
#include "fmod_output_audren.h"
#include "fmod_soundi.h"
#include "fmod_systemi.h"

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX audrenoutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputAudren::getDescriptionEx()
{
    FMOD_memset(&audrenoutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    audrenoutput.name          = "FMOD Switch Audio Renderer Output";
    audrenoutput.version       = 0x00010100;
    audrenoutput.polling       = true;
    audrenoutput.getnumdrivers = &OutputAudren::getNumDriversCallback;
    audrenoutput.getdrivername = &OutputAudren::getDriverNameCallback;
    audrenoutput.getdrivercaps = &OutputAudren::getDriverCapsCallback;
    audrenoutput.init          = &OutputAudren::initCallback;
    audrenoutput.close         = &OutputAudren::closeCallback;
    audrenoutput.start         = &OutputAudren::startCallback;
    audrenoutput.stop          = &OutputAudren::stopCallback;
    audrenoutput.gethandle     = &OutputAudren::getHandleCallback;
    audrenoutput.getposition   = &OutputAudren::getPositionCallback;
    audrenoutput.lock          = &OutputAudren::lockCallback;
    audrenoutput.mType         = FMOD_OUTPUTTYPE_AUDREN;
    audrenoutput.mSize         = sizeof(OutputAudren);

    return &audrenoutput;
}

/* Copies the next mHwFrames frames out of the ring as stereo, wrapping at its end. */
void OutputAudren::fill(short *dst)
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

namespace
{
    const AudioRendererConfig kRendererConfig =
    {
        AudioRendererOutputRate_48kHz,
        4,      /* voices: renderer channels, one per channel of a voice, so the stereo voice takes two */
        0,      /* effects */
        1,      /* sinks */
        1,      /* mix objects */
        2,      /* mix buffers: stereo */
    };
}

void *OutputAudren::feederThread(void *param)
{
    OutputAudren *output = (OutputAudren *)param;
    AudioDriver  *driver = (AudioDriver *)output->mDriver;

    wowdecomp_switch_thread_start(WOWDECOMP_THREAD_AUDIO);

    /*
        Keep every wave buffer queued: one that has played (or was never queued) is refilled from the ring and queued
        again. The renderer runs a frame every 5 ms; waiting for one keeps this loop in step with it.
    */
    while (output->mRunning)
    {
        bool queued = false;

        for (int i = 0; i < NUMHWBUFFERS; i++)
        {
            AudioDriverWaveBuf *buffer = (AudioDriverWaveBuf *)output->mWaveBuf[i];

            if (buffer->state == AudioDriverWaveBufState_Free || buffer->state == AudioDriverWaveBufState_Done)
            {
                short *samples = (short *)output->mPool + (size_t)i * output->mHwFrames * 2;
                output->fill(samples);
                armDCacheFlush(samples, (size_t)output->mHwFrames * 2 * sizeof(short));
                audrvVoiceAddWaveBuf(driver, 0, buffer);
                queued = true;
            }
        }

        if (queued && !audrvVoiceIsPlaying(driver, 0))
        {
            audrvVoiceStart(driver, 0);
        }

        audrvUpdate(driver);
        audrenWaitFrame();
    }

    return 0;
}

FMOD_RESULT OutputAudren::getNumDrivers(int *numdrivers)
{
    *numdrivers = 1;
    return FMOD_OK;
}

FMOD_RESULT OutputAudren::getDriverName(int id, char *name, int namelen)
{
    FMOD_strncpy(name, "Nintendo Switch", namelen);
    return FMOD_OK;
}

FMOD_RESULT OutputAudren::getDriverCaps(int id, FMOD_CAPS *caps)
{
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL | FMOD_CAPS_OUTPUT_FORMAT_PCM16);
    return FMOD_OK;
}

FMOD_RESULT OutputAudren::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    FMOD_RESULT  result;
    unsigned int bufferlength;
    int          numbuffers;

    Plugin::init();     /* Sets up gGlobal for this plugin's debug / file / memory access. */

    mBuffer          = 0;
    mReadFrame       = 0;
    mPlayed          = 0;
    mRunning         = false;
    mThreadStarted   = false;
    mRendererStarted = false;
    mThread          = 0;
    mDriver          = 0;
    mPool            = 0;
    mPoolBytes       = 0;
    mChannels        = outputchannels > 0 ? outputchannels : 2;

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        mWaveBuf[i] = 0;
    }

    *outputformat = FMOD_SOUND_FORMAT_PCM16;
    *outputrate   = 48000;

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

    Result rc = audrenInitialize(&kRendererConfig);

    if (R_FAILED(rc))
    {
        fprintf(stderr, "FMOD audren: audrenInitialize failed (0x%x, module %u, description %u)\n", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        return FMOD_ERR_OUTPUT_INIT;
    }

    mRendererStarted = true;

    AudioDriver *driver = (AudioDriver *)calloc(1, sizeof(AudioDriver));
    mDriver = driver;

    if (!driver)
    {
        return FMOD_ERR_MEMORY;
    }

    rc = audrvCreate(driver, &kRendererConfig, 2);

    if (R_FAILED(rc))
    {
        fprintf(stderr, "FMOD audren: audrvCreate failed (0x%x, module %u, description %u)\n", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        free(driver);
        mDriver = 0;
        return FMOD_ERR_OUTPUT_INIT;
    }

    /* The wave buffers live in one memory pool the renderer reads from: page aligned, sizes in whole pages. */
    mPoolBytes = ((size_t)mHwFrames * 2 * sizeof(short) * NUMHWBUFFERS + AUDREN_MEMPOOL_ALIGNMENT - 1) & ~(size_t)(AUDREN_MEMPOOL_ALIGNMENT - 1);
    mPool = aligned_alloc(AUDREN_MEMPOOL_ALIGNMENT, mPoolBytes);

    if (!mPool)
    {
        return FMOD_ERR_MEMORY;
    }

    memset(mPool, 0, mPoolBytes);
    armDCacheFlush(mPool, mPoolBytes);

    int pool = audrvMemPoolAdd(driver, mPool, mPoolBytes);

    if (pool < 0 || !audrvMemPoolAttach(driver, pool))
    {
        fprintf(stderr, "FMOD audren: memory pool rejected\n");
        return FMOD_ERR_OUTPUT_INIT;
    }

    static const u8 sinkChannels[2] = { 0, 1 };
    audrvDeviceSinkAdd(driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sinkChannels);

    rc = audrvUpdate(driver);

    if (R_SUCCEEDED(rc))
    {
        rc = audrenStartAudioRenderer();
    }

    if (R_FAILED(rc))
    {
        fprintf(stderr, "FMOD audren: starting the renderer failed (0x%x, module %u, description %u)\n", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        return FMOD_ERR_OUTPUT_INIT;
    }

    /* One stereo voice, left to left and right to right. */
    if (!audrvVoiceInit(driver, 0, 2, PcmFormat_Int16, 48000))
    {
        fprintf(stderr, "FMOD audren: voice rejected\n");
        return FMOD_ERR_OUTPUT_INIT;
    }

    audrvVoiceSetDestinationMix(driver, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(driver, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(driver, 0, 0.0f, 0, 1);
    audrvVoiceSetMixFactor(driver, 0, 0.0f, 1, 0);
    audrvVoiceSetMixFactor(driver, 0, 1.0f, 1, 1);

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        AudioDriverWaveBuf *buffer = (AudioDriverWaveBuf *)calloc(1, sizeof(AudioDriverWaveBuf));
        mWaveBuf[i] = buffer;

        if (!buffer)
        {
            return FMOD_ERR_MEMORY;
        }

        buffer->data_raw            = mPool;
        buffer->size                = mPoolBytes;
        buffer->start_sample_offset = (s32)(i * mHwFrames);
        buffer->end_sample_offset   = (s32)((i + 1) * mHwFrames);
    }

    fprintf(stderr, "FMOD audren: 48000 Hz stereo, %u-frame buffers, FMOD mixes %d channels\n", mHwFrames, outputchannels);

    return FMOD_OK;
}

FMOD_RESULT OutputAudren::close()
{
    mRunning = false;

    if (mThreadStarted)
    {
        pthread_join((pthread_t)mThread, 0);
        mThreadStarted = false;
    }

    if (mDriver)
    {
        audrvClose((AudioDriver *)mDriver);
        free(mDriver);
        mDriver = 0;
    }

    if (mRendererStarted)
    {
        audrenExit();
        mRendererStarted = false;
    }

    for (int i = 0; i < NUMHWBUFFERS; i++)
    {
        free(mWaveBuf[i]);
        mWaveBuf[i] = 0;
    }

    free(mPool);
    mPool = 0;

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputAudren::getHandle(void **handle)
{
    *handle = 0;
    return FMOD_OK;
}

FMOD_RESULT OutputAudren::start()
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

FMOD_RESULT OutputAudren::stop()
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

FMOD_RESULT OutputAudren::getPosition(unsigned int *pcm)
{
    *pcm = __atomic_load_n(&mPlayed, __ATOMIC_ACQUIRE);
    return FMOD_OK;
}

FMOD_RESULT OutputAudren::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
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

FMOD_RESULT F_CALLBACK OutputAudren::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputAudren *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputAudren::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputAudren *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputAudren::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputAudren *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputAudren::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputAudren *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputAudren::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudren *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputAudren::startCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudren *)output)->start();
}

FMOD_RESULT F_CALLBACK OutputAudren::stopCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputAudren *)output)->stop();
}

FMOD_RESULT F_CALLBACK OutputAudren::getHandleCallback(FMOD_OUTPUT_STATE *output, void **handle)
{
    return ((OutputAudren *)output)->getHandle(handle);
}

FMOD_RESULT F_CALLBACK OutputAudren::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputAudren *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputAudren::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputAudren *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}
