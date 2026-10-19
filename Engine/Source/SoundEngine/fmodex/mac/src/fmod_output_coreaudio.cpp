/*
    CoreAudio output for FMOD Ex on macOS (wowdecomp). See fmod_output_coreaudio.h.
*/

#include <AudioToolbox/AudioToolbox.h>

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_memory.h"
#include "fmod_output_coreaudio.h"
#include "fmod_soundi.h"
#include "fmod_systemi.h"

namespace FMOD
{

FMOD_OUTPUT_DESCRIPTION_EX coreaudiooutput;

FMOD_OUTPUT_DESCRIPTION_EX *OutputCoreAudio::getDescriptionEx()
{
    FMOD_memset(&coreaudiooutput, 0, sizeof(FMOD_OUTPUT_DESCRIPTION_EX));

    coreaudiooutput.name          = "FMOD CoreAudio Output";
    coreaudiooutput.version       = 0x00010100;
    coreaudiooutput.polling       = true;
    coreaudiooutput.getnumdrivers = &OutputCoreAudio::getNumDriversCallback;
    coreaudiooutput.getdrivername = &OutputCoreAudio::getDriverNameCallback;
    coreaudiooutput.getdrivercaps = &OutputCoreAudio::getDriverCapsCallback;
    coreaudiooutput.init          = &OutputCoreAudio::initCallback;
    coreaudiooutput.close         = &OutputCoreAudio::closeCallback;
    coreaudiooutput.start         = &OutputCoreAudio::startCallback;
    coreaudiooutput.stop          = &OutputCoreAudio::stopCallback;
    coreaudiooutput.gethandle     = &OutputCoreAudio::getHandleCallback;
    coreaudiooutput.getposition   = &OutputCoreAudio::getPositionCallback;
    coreaudiooutput.lock          = &OutputCoreAudio::lockCallback;
    coreaudiooutput.mType         = FMOD_OUTPUTTYPE_COREAUDIO;
    coreaudiooutput.mSize         = sizeof(OutputCoreAudio);

    return &coreaudiooutput;
}

int OutputCoreAudio::render(void *refcon, void *flags, const void *timestamp, unsigned int bus, unsigned int frames, void *bufferlist)
{
    OutputCoreAudio *output = (OutputCoreAudio *)refcon;
    AudioBufferList *buffers = (AudioBufferList *)bufferlist;
    char            *dst = (char *)buffers->mBuffers[0].mData;
    unsigned int     bytes = frames * output->mFrameBytes;

    if (!output->mRunning || !output->mBuffer)
    {
        FMOD_memset(dst, 0, bytes);
        return noErr;
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

    return noErr;
}

static OSStatus RenderCallback(void *refcon, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *timestamp, UInt32 bus, UInt32 frames, AudioBufferList *bufferlist)
{
    return OutputCoreAudio::render(refcon, flags, timestamp, bus, frames, bufferlist);
}

FMOD_RESULT OutputCoreAudio::getNumDrivers(int *numdrivers)
{
    /* The default output device, whichever it is. */
    *numdrivers = 1;
    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::getDriverName(int id, char *name, int namelen)
{
    FMOD_strncpy(name, "Default Output", namelen);
    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::getDriverCaps(int id, FMOD_CAPS *caps)
{
    /* The output unit converts formats and channel counts to whatever the device needs. */
    *caps = (FMOD_CAPS)(*caps | FMOD_CAPS_OUTPUT_MULTICHANNEL | FMOD_CAPS_OUTPUT_FORMAT_PCM16 | FMOD_CAPS_OUTPUT_FORMAT_PCMFLOAT);
    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    FMOD_RESULT  result;
    unsigned int bufferlength;
    int          numbuffers;

    Plugin::init();     /* Sets up gGlobal for this plugin's debug / file / memory access. */

    mUnit       = 0;
    mBuffer     = 0;
    mReadOffset = 0;
    mPlayed     = 0;
    mRunning    = false;

    if (*outputformat != FMOD_SOUND_FORMAT_PCMFLOAT)
    {
        *outputformat = FMOD_SOUND_FORMAT_PCM16;
    }

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

    AudioComponentDescription description;
    FMOD_memset(&description, 0, sizeof(description));
    description.componentType         = kAudioUnitType_Output;
    description.componentSubType      = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(0, &description);
    AudioComponentInstance unit = 0;

    if (!component || AudioComponentInstanceNew(component, &unit) != noErr)
    {
        return FMOD_ERR_OUTPUT_INIT;
    }

    mUnit = unit;

    AudioStreamBasicDescription format;
    FMOD_memset(&format, 0, sizeof(format));
    format.mSampleRate       = *outputrate;
    format.mFormatID         = kAudioFormatLinearPCM;
    format.mFormatFlags      = (*outputformat == FMOD_SOUND_FORMAT_PCMFLOAT ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket  = 1;
    format.mChannelsPerFrame = outputchannels;
    format.mBitsPerChannel   = *outputformat == FMOD_SOUND_FORMAT_PCMFLOAT ? 32 : 16;
    format.mBytesPerFrame    = mFrameBytes;
    format.mBytesPerPacket   = mFrameBytes;

    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr)
    {
        return FMOD_ERR_FORMAT;
    }

    AURenderCallbackStruct callback;
    callback.inputProc       = RenderCallback;
    callback.inputProcRefCon = this;

    if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr ||
        AudioUnitInitialize(unit) != noErr)
    {
        return FMOD_ERR_OUTPUT_INIT;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::close()
{
    mRunning = false;

    if (mUnit)
    {
        AudioUnitUninitialize((AudioComponentInstance)mUnit);
        AudioComponentInstanceDispose((AudioComponentInstance)mUnit);
        mUnit = 0;
    }

    if (mBuffer)
    {
        FMOD_Memory_Free(mBuffer);
        mBuffer = 0;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::getHandle(void **handle)
{
    *handle = mUnit;
    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::start()
{
    FMOD_RESULT result = OutputPolled::start();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = true;

    if (AudioOutputUnitStart((AudioComponentInstance)mUnit) != noErr)
    {
        mRunning = false;
        return FMOD_ERR_OUTPUT_DRIVERCALL;
    }

    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::stop()
{
    FMOD_RESULT result = OutputPolled::stop();

    if (result != FMOD_OK)
    {
        return result;
    }

    mRunning = false;
    AudioOutputUnitStop((AudioComponentInstance)mUnit);

    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::getPosition(unsigned int *pcm)
{
    *pcm = __atomic_load_n(&mPlayed, __ATOMIC_ACQUIRE);
    return FMOD_OK;
}

FMOD_RESULT OutputCoreAudio::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
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

FMOD_RESULT F_CALLBACK OutputCoreAudio::getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers)
{
    return ((OutputCoreAudio *)output)->getNumDrivers(numdrivers);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen)
{
    return ((OutputCoreAudio *)output)->getDriverName(id, name, namelen);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps)
{
    return ((OutputCoreAudio *)output)->getDriverCaps(id, caps);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata)
{
    return ((OutputCoreAudio *)output)->init(selecteddriver, flags, outputrate, outputchannels, outputformat, dspbufferlength, dspnumbuffers, extradriverdata);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::closeCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputCoreAudio *)output)->close();
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::startCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputCoreAudio *)output)->start();
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::stopCallback(FMOD_OUTPUT_STATE *output)
{
    return ((OutputCoreAudio *)output)->stop();
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::getHandleCallback(FMOD_OUTPUT_STATE *output, void **handle)
{
    return ((OutputCoreAudio *)output)->getHandle(handle);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm)
{
    return ((OutputCoreAudio *)output)->getPosition(pcm);
}

FMOD_RESULT F_CALLBACK OutputCoreAudio::lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return ((OutputCoreAudio *)output)->lock(offset, length, ptr1, ptr2, len1, len2);
}

}
