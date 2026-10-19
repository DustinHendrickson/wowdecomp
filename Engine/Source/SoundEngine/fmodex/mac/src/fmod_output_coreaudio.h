/*
    CoreAudio output for FMOD Ex on macOS (wowdecomp). Polled like the WinMM output: the device plays a ring of
    blocksize * numblocks samples through the default output AudioUnit, getPosition reports how many samples it has
    played, and the FMOD mixer thread fills each block once the play cursor has passed it.
*/

#ifndef _FMOD_OUTPUT_COREAUDIO_H
#define _FMOD_OUTPUT_COREAUDIO_H

#include "fmod_settings.h"
#include "fmod_output_polled.h"

namespace FMOD
{
    class OutputCoreAudio : public OutputPolled
    {
      private:

        void                       *mUnit;              /* AudioComponentInstance */
        char                       *mBuffer;
        unsigned int                mBufferBytes;
        unsigned int                mFrameBytes;
        unsigned int                mReadOffset;
        volatile unsigned int       mPlayed;
        bool                        mRunning;

      public:

        static FMOD_OUTPUT_DESCRIPTION_EX *getDescriptionEx();

        /* AudioUnit render callback; the parameters are CoreAudio types, kept opaque here. */
        static int                  render(void *refcon, void *flags, const void *timestamp, unsigned int bus, unsigned int frames, void *bufferlist);

        FMOD_RESULT                 getNumDrivers(int *numdrivers);
        FMOD_RESULT                 getDriverName(int id, char *name, int namelen);
        FMOD_RESULT                 getDriverCaps(int id, FMOD_CAPS *caps);
        FMOD_RESULT                 init(int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata);
        FMOD_RESULT                 close();
        FMOD_RESULT                 getHandle(void **handle);
        FMOD_RESULT                 start();
        FMOD_RESULT                 stop();
        FMOD_RESULT                 getPosition(unsigned int *pcm);
        FMOD_RESULT                 lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2);

        static FMOD_RESULT F_CALLBACK getNumDriversCallback(FMOD_OUTPUT_STATE *output, int *numdrivers);
        static FMOD_RESULT F_CALLBACK getDriverNameCallback(FMOD_OUTPUT_STATE *output, int id, char *name, int namelen);
        static FMOD_RESULT F_CALLBACK getDriverCapsCallback(FMOD_OUTPUT_STATE *output, int id, FMOD_CAPS *caps);
        static FMOD_RESULT F_CALLBACK initCallback(FMOD_OUTPUT_STATE *output, int selecteddriver, FMOD_INITFLAGS flags, int *outputrate, int outputchannels, FMOD_SOUND_FORMAT *outputformat, int dspbufferlength, int dspnumbuffers, void *extradriverdata);
        static FMOD_RESULT F_CALLBACK closeCallback(FMOD_OUTPUT_STATE *output);
        static FMOD_RESULT F_CALLBACK startCallback(FMOD_OUTPUT_STATE *output);
        static FMOD_RESULT F_CALLBACK stopCallback(FMOD_OUTPUT_STATE *output);
        static FMOD_RESULT F_CALLBACK getHandleCallback(FMOD_OUTPUT_STATE *output, void **handle);
        static FMOD_RESULT F_CALLBACK getPositionCallback(FMOD_OUTPUT_STATE *output, unsigned int *pcm);
        static FMOD_RESULT F_CALLBACK lockCallback(FMOD_OUTPUT_STATE *output, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2);
    };
}

#endif
