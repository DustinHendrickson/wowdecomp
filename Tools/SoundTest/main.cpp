// Sound test: FMOD Ex on the macOS platform layer (fmodex/mac). Sets the system up the way SoundEngine does (default
// output, driver caps, 1024 x 4 DSP buffer, software channels), checks that the CoreAudio output was picked, then plays
// a tone from memory and a WAV stream from disk and checks that both advance in real time, which only happens while the
// output unit is pulling mixed blocks. --audible plays them at a listenable volume instead of silently.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include "fmod.hpp"
#include "fmod_errors.h"

static int s_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } else { printf("ok   %s\n", #cond); } } while (0)
#define CHECK_OK(call) do { FMOD_RESULT result_ = (call); if (result_ != FMOD_OK) { printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #call, FMOD_ErrorString(result_)); s_failures++; } else { printf("ok   %s\n", #call); } } while (0)

static const int kRate = 44100;

// Mono 16-bit samples of a sine tone with short fades, so --audible doesn't click.
static std::vector<int16_t> Tone(float frequency, float seconds) {
    std::vector<int16_t> samples(static_cast<size_t>(kRate * seconds));
    size_t fade = kRate / 100;

    for (size_t i = 0; i < samples.size(); i++) {
        float envelope = 1.0f;

        if (i < fade) {
            envelope = static_cast<float>(i) / fade;
        } else if (samples.size() - i < fade) {
            envelope = static_cast<float>(samples.size() - i) / fade;
        }

        samples[i] = static_cast<int16_t>(12000.0f * envelope * std::sin(2.0f * 3.14159265f * frequency * i / kRate));
    }

    return samples;
}

static void Put32(std::string& data, uint32_t value) {
    data.append(reinterpret_cast<const char*>(&value), 4);
}

static void Put16(std::string& data, uint16_t value) {
    data.append(reinterpret_cast<const char*>(&value), 2);
}

static std::string Wav(const std::vector<int16_t>& samples) {
    std::string data = "RIFF";
    Put32(data, static_cast<uint32_t>(36 + samples.size() * 2));
    data += "WAVEfmt ";
    Put32(data, 16);
    Put16(data, 1);
    Put16(data, 1);
    Put32(data, kRate);
    Put32(data, kRate * 2);
    Put16(data, 2);
    Put16(data, 16);
    data += "data";
    Put32(data, static_cast<uint32_t>(samples.size() * 2));
    data.append(reinterpret_cast<const char*>(samples.data()), samples.size() * 2);
    return data;
}

// Plays sound for about a second, updating the system as the game's heartbeat does, and returns how many PCM samples
// the channel advanced per second of wall time.
static double PlayRate(FMOD::System* system, FMOD::Sound* sound, float volume) {
    FMOD::Channel* channel = nullptr;

    if (system->playSound(FMOD_CHANNEL_FREE, sound, true, &channel) != FMOD_OK || !channel) {
        return 0.0;
    }

    channel->setVolume(volume);
    channel->setPaused(false);

    // Let the first blocks reach the output before measuring.
    for (int i = 0; i < 10; i++) {
        system->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    unsigned int start = 0;
    channel->getPosition(&start, FMOD_TIMEUNIT_PCM);
    auto began = std::chrono::steady_clock::now();

    for (int i = 0; i < 100; i++) {
        system->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    unsigned int end = 0;
    channel->getPosition(&end, FMOD_TIMEUNIT_PCM);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();

    bool playing = false;
    channel->isPlaying(&playing);
    printf("     position %u -> %u over %.2f s, playing %d\n", start, end, seconds, playing);
    channel->stop();

    return (end - start) / seconds;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool audible = argc > 1 && !strcmp(argv[1], "--audible");
    float volume = audible ? 0.3f : 0.0f;

    FMOD::System* system = nullptr;
    CHECK_OK(FMOD::System_Create(&system));

    if (!system) {
        return 1;
    }

    unsigned int version = 0;
    CHECK_OK(system->getVersion(&version));
    CHECK(version == FMOD_VERSION);

    CHECK_OK(system->setOutput(FMOD_OUTPUTTYPE_AUTODETECT));

    int numDrivers = 0;
    CHECK_OK(system->getNumDrivers(&numDrivers));
    CHECK(numDrivers >= 1);

    char driverName[256] = {};
    CHECK_OK(system->getDriverName(0, driverName, sizeof(driverName)));
    printf("     driver 0: %s\n", driverName);
    CHECK_OK(system->setDriver(0));

    FMOD_CAPS caps = 0;
    FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_STEREO;
    CHECK_OK(system->getDriverCaps(0, &caps, nullptr, nullptr, &speakerMode));
    printf("     caps 0x%x, speaker mode %d\n", caps, speakerMode);

    CHECK_OK(system->setHardwareChannels(0, 0, 0, 0));
    CHECK_OK(system->setSoftwareChannels(32));
    CHECK_OK(system->setDSPBufferSize(1024, 4));
    CHECK_OK(system->setSpeakerMode(speakerMode));
    CHECK_OK(system->init(32, FMOD_INIT_3D_RIGHTHANDED, nullptr));

    FMOD_OUTPUTTYPE output = FMOD_OUTPUTTYPE_UNKNOWN;
    CHECK_OK(system->getOutput(&output));
    CHECK(output == FMOD_OUTPUTTYPE_COREAUDIO);

    int rate = 0;
    FMOD_SOUND_FORMAT format = FMOD_SOUND_FORMAT_NONE;
    int channels = 0;
    CHECK_OK(system->getSoftwareFormat(&rate, &format, &channels, nullptr, nullptr, nullptr));
    printf("     software format %d Hz, format %d, %d channels\n", rate, format, channels);

    // A tone from memory, decoded as a sample.
    std::string tone = Wav(Tone(440.0f, 3.0f));
    FMOD_CREATESOUNDEXINFO info;
    memset(&info, 0, sizeof(info));
    info.cbsize = sizeof(info);
    info.length = static_cast<unsigned int>(tone.size());

    FMOD::Sound* sample = nullptr;
    CHECK_OK(system->createSound(tone.data(), FMOD_OPENMEMORY | FMOD_SOFTWARE | FMOD_2D, &info, &sample));

    if (sample) {
        double samplesPerSecond = PlayRate(system, sample, volume);
        printf("     sample: %.0f samples/s (sound is %d Hz)\n", samplesPerSecond, kRate);
        CHECK(std::fabs(samplesPerSecond - kRate) < kRate * 0.15);
        sample->release();
    }

    // A stream from a file: FMOD's file layer and its stream thread on the Mac OS layer.
    char path[] = "/tmp/wowdecomp-soundtest-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);

    if (fd >= 0) {
        std::string wav = Wav(Tone(660.0f, 3.0f));
        CHECK(write(fd, wav.data(), wav.size()) == static_cast<ssize_t>(wav.size()));
        close(fd);

        FMOD::Sound* stream = nullptr;
        CHECK_OK(system->createStream(path, FMOD_SOFTWARE | FMOD_2D, nullptr, &stream));

        if (stream) {
            double samplesPerSecond = PlayRate(system, stream, volume);
            printf("     stream: %.0f samples/s\n", samplesPerSecond);
            CHECK(std::fabs(samplesPerSecond - kRate) < kRate * 0.15);
            stream->release();
        }

        unlink(path);
    }

    float dsp = 0.0f;
    float total = 0.0f;
    system->getCPUUsage(&dsp, nullptr, nullptr, &total);
    printf("     cpu: dsp %.1f%%, total %.1f%%\n", dsp, total);

    CHECK_OK(system->close());
    CHECK_OK(system->release());

    printf("%s (%d failures)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures != 0;
}
