/*
    AAudio output for FMOD Ex on Android (wowdecomp). Polled like the CoreAudio output on macOS: the stream's data callback
    copies a ring of blocksize * numblocks samples out in order, getPosition reports how many samples it has handed
    over, and the FMOD mixer thread fills each block once the play cursor has passed it.

    The game opens several FMOD systems at once (its own, one to enumerate devices, voice chat); each gets a stream of
    its own, which Android mixes, unlike the Switch's audio renderer, which needed one shared driver.
*/

#ifndef _FMOD_OUTPUT_AAUDIO_H
#define _FMOD_OUTPUT_AAUDIO_H

#include "fmod_settings.h"
#include "fmod_output_polled.h"

/* The platform's native output; it takes the slot CoreAudio has on macOS. */
#define FMOD_OUTPUTTYPE_AAUDIO FMOD_OUTPUTTYPE_COREAUDIO

namespace FMOD
{
    class OutputAAudio : public OutputPolled
    {
      private:

        void                       *mStream;            /* AAudioStream */
        char                       *mBuffer;
        unsigned int                mBufferBytes;
        unsigned int                mFrameBytes;
        unsigned int                mReadOffset;
        volatile unsigned int       mPlayed;
        volatile bool               mRunning;
        volatile bool               mDisconnected;      /* the stream was lost; reopened on the next getPosition */
        int                         mRate;
        int                         mChannels;
        FMOD_SOUND_FORMAT           mFormat;

        bool                        openStream();
        void                        closeStream();

      public:

        static FMOD_OUTPUT_DESCRIPTION_EX *getDescriptionEx();

        /* AAudio data and error callbacks; the parameters are AAudio types, kept opaque here. */
        static int                  render(void *stream, void *userdata, void *audiodata, int frames);
        static void                 error(void *stream, void *userdata, int result);

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
