/*
    Audio renderer (audren) output for FMOD Ex on the Nintendo Switch (wowdecomp). Polled like the CoreAudio output on
    macOS: FMOD mixes into a ring of blocksize * numblocks samples, and the ring is copied into the wave buffers of a
    renderer voice in order, counting what has been handed over as played. getPosition reports that count, and the FMOD
    mixer thread fills each block once the play cursor has passed it.

    The game runs several FMOD systems at once (its own, one kept open to enumerate devices, voice chat), each with an
    output, but the process has one audio renderer session. Two libnx audio drivers on it overwrite each other's state
    with every update: buffers stop completing and what plays tears. So there is one driver, shared: each output gets
    its own voice, memory pool and wave buffers in it, and one service thread refills them all, every renderer frame.

    The voice plays 48 kHz stereo PCM16; other channel counts are folded to stereo as they are copied out. (The simpler
    audout service refuses to start for homebrew on some systems; the renderer is what SDL and most homebrew use.)
*/

#ifndef _FMOD_OUTPUT_AUDREN_H
#define _FMOD_OUTPUT_AUDREN_H

#include "fmod_settings.h"
#include "fmod_output_polled.h"

/* The platform's native output; it takes the slot CoreAudio has on macOS. */
#define FMOD_OUTPUTTYPE_AUDREN FMOD_OUTPUTTYPE_COREAUDIO

namespace FMOD
{
    class OutputAudren : public OutputPolled
    {
      private:

        /*
            Small wave buffers, a few queued: what is queued is how far "played" runs ahead of what is heard, on top of
            FMOD's own ring. Whole FMOD blocks (21 ms each) made the sound late; six 5 ms slices keep it to about 32 ms,
            enough to ride out a late wake-up.
        */
        enum { NUMHWBUFFERS = 6, HWFRAMES = 256 };

        int                         mVoice;                       /* in the shared driver; -1 for none */
        int                         mPoolId;
        void                       *mPool;                        /* the wave buffers' memory */
        size_t                      mPoolBytes;
        void                       *mWaveBuf[NUMHWBUFFERS];       /* AudioDriverWaveBuf */
        unsigned int                mHwFrames;
        short                      *mBuffer;
        unsigned int                mBufferFrames;
        unsigned int                mChannels;
        unsigned int                mReadFrame;
        volatile unsigned int       mPlayed;
        bool                        mRendererUser;                /* holds a reference on the shared driver */

        static void                *serviceThread(void *param);
        void                        fill(short *dst);
        void                        service(void *driver);
        void                        detach();

      public:

        static FMOD_OUTPUT_DESCRIPTION_EX *getDescriptionEx();

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
