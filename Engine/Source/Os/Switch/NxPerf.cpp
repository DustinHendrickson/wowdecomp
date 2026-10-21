// Performance report on the Switch (wowdecomp_switch_perf_* in Base/Platform.h): every 5 seconds a line in
// Logs/Console.txt with the frame rate and the slowest frame, FMOD's CPU use, the longest wait between refills of the
// audio renderer's buffers and its underruns, and how long movie frames take to decode. For finding what makes sound
// stutter or movies judder on the console.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include "Engine/Source/Base/Platform.h"

namespace {

uint64_t NowUs() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000;
}

uint64_t s_windowStart;
uint64_t s_lastFrame;
uint32_t s_frames;
uint64_t s_slowestFrameUs;

std::atomic<uint64_t> s_audioGapUs(0);
std::atomic<uint32_t> s_audioUnderruns(0);
std::atomic<uint32_t> s_movieFrames(0);
std::atomic<uint64_t> s_movieTotalUs(0);
std::atomic<uint64_t> s_movieSlowestUs(0);
std::atomic<uint32_t> s_fmodPercent[4];

void Max(std::atomic<uint64_t>& value, uint64_t candidate) {
    uint64_t current = value.load();

    while (candidate > current && !value.compare_exchange_weak(current, candidate)) {
    }
}

}

extern "C" void wowdecomp_switch_perf_frame(void) {
    uint64_t now = NowUs();

    if (!s_windowStart) {
        s_windowStart = now;
        s_lastFrame = now;
        return;
    }

    uint64_t frame = now - s_lastFrame;
    s_lastFrame = now;
    s_frames++;

    if (frame > s_slowestFrameUs) {
        s_slowestFrameUs = frame;
    }

    uint64_t elapsed = now - s_windowStart;

    if (elapsed < 5000000) {
        return;
    }

    uint32_t movieFrames = s_movieFrames.exchange(0);
    uint64_t movieTotal = s_movieTotalUs.exchange(0);
    uint64_t movieSlowest = s_movieSlowestUs.exchange(0);

    fprintf(stderr,
        "Perf: %.1f fps, slowest frame %.1f ms | FMOD cpu dsp %u%% stream %u%% update %u%% total %u%% | audio longest refill wait %.1f ms, %u underruns",
        s_frames * 1000000.0 / elapsed, s_slowestFrameUs / 1000.0,
        s_fmodPercent[0].load(), s_fmodPercent[1].load(), s_fmodPercent[2].load(), s_fmodPercent[3].load(),
        s_audioGapUs.exchange(0) / 1000.0, s_audioUnderruns.exchange(0));

    if (movieFrames) {
        fprintf(stderr, " | movie %u frames decoded, %.1f ms average, %.1f ms slowest", movieFrames, movieTotal / 1000.0 / movieFrames, movieSlowest / 1000.0);
    }

    fprintf(stderr, "\n");

    s_windowStart = now;
    s_frames = 0;
    s_slowestFrameUs = 0;
}

extern "C" void wowdecomp_switch_perf_audio_refill(void) {
    static uint64_t s_last;
    uint64_t now = NowUs();

    if (s_last) {
        Max(s_audioGapUs, now - s_last);
    }

    s_last = now;
}

extern "C" void wowdecomp_switch_perf_audio_underrun(void) {
    s_audioUnderruns++;
}

extern "C" void wowdecomp_switch_perf_movie_decode(uint64_t microseconds) {
    s_movieFrames++;
    s_movieTotalUs += microseconds;
    Max(s_movieSlowestUs, microseconds);
}

extern "C" void wowdecomp_switch_perf_fmod(float dsp, float stream, float update, float total) {
    s_fmodPercent[0] = static_cast<uint32_t>(dsp + 0.5f);
    s_fmodPercent[1] = static_cast<uint32_t>(stream + 0.5f);
    s_fmodPercent[2] = static_cast<uint32_t>(update + 0.5f);
    s_fmodPercent[3] = static_cast<uint32_t>(total + 0.5f);
}

extern "C" uint64_t wowdecomp_switch_perf_now_us(void) {
    return NowUs();
}
