#include <cstdint>
#include <cstring>
#include <xvid.h>
#include "ThirdParty/DivxDecoder/DivxDecoder.h"

struct XvidDecoderSlot {
    void* m_handle;
    int32_t m_width;
    int32_t m_height;
};

static XvidDecoderSlot s_slots[16];
static bool s_globalInitialized;

#if defined(__SWITCH__)
// Movie frame decode times for the Switch's performance report (Engine/Source/Os/Switch/NxPerf.cpp).
extern "C" void wowdecomp_switch_perf_movie_decode(uint64_t microseconds);
extern "C" uint64_t wowdecomp_switch_perf_now_us(void);
#endif

static bool ValidSlot(int32_t decoder) {
    return decoder >= 0 && decoder < static_cast<int32_t>(sizeof(s_slots) / sizeof(s_slots[0]));
}

static bool GlobalInitialize() {
    if (s_globalInitialized) {
        return true;
    }

    xvid_gbl_init_t init;
    memset(&init, 0, sizeof(init));
    init.version = XVID_VERSION;

    if (xvid_global(nullptr, XVID_GBL_INIT, &init, nullptr) < 0) {
        return false;
    }

    s_globalInitialized = true;

    return true;
}

static void DestroySlot(XvidDecoderSlot& slot) {
    if (slot.m_handle) {
        xvid_decore(slot.m_handle, XVID_DEC_DESTROY, nullptr, nullptr);
        slot.m_handle = nullptr;
    }
}

int32_t InitializeDivxDecoder(int32_t decoder, int32_t width, int32_t height) {
    if (!ValidSlot(decoder) || !GlobalInitialize()) {
        return 1;
    }

    XvidDecoderSlot& slot = s_slots[decoder];
    DestroySlot(slot);

    xvid_dec_create_t create;
    memset(&create, 0, sizeof(create));
    create.version = XVID_VERSION;
    create.width = width;
    create.height = height;

    if (xvid_decore(nullptr, XVID_DEC_CREATE, &create, nullptr) < 0) {
        return 1;
    }

    slot.m_handle = create.handle;
    slot.m_width = width;
    slot.m_height = height;

    return 0;
}

int32_t SetOutputFormat(int32_t decoder, int32_t format, int32_t width, int32_t height) {
    if (!ValidSlot(decoder) || !s_slots[decoder].m_handle) {
        return 1;
    }

    s_slots[decoder].m_width = width;
    s_slots[decoder].m_height = height;

    return 0;
}

int32_t DivxDecode(int32_t decoder, DivxDecodeFrame* frame, int32_t flags) {
    if (!ValidSlot(decoder) || !s_slots[decoder].m_handle || !frame) {
        return 1;
    }

    XvidDecoderSlot& slot = s_slots[decoder];

    xvid_dec_frame_t decode;
    memset(&decode, 0, sizeof(decode));
    decode.version = XVID_VERSION;
    decode.general = XVID_LOWDELAY;
    decode.bitstream = const_cast<void*>(frame->m_input);
    decode.length = static_cast<int>(frame->m_inputSize);

    if (frame->m_render && frame->m_output) {
        decode.output.csp = XVID_CSP_BGRA | XVID_CSP_VFLIP;
        decode.output.plane[0] = frame->m_output;
        decode.output.stride[0] = slot.m_width * 4;
    } else {
        decode.output.csp = XVID_CSP_NULL;
    }

#if defined(__SWITCH__)
    uint64_t start = wowdecomp_switch_perf_now_us();
#endif

    if (xvid_decore(slot.m_handle, XVID_DEC_DECODE, &decode, nullptr) < 0) {
        return 1;
    }

#if defined(__SWITCH__)
    wowdecomp_switch_perf_movie_decode(wowdecomp_switch_perf_now_us() - start);
#endif

    return 0;
}

int32_t UnInitializeDivxDecoder(int32_t decoder) {
    if (!ValidSlot(decoder) || !s_slots[decoder].m_handle) {
        return 1;
    }

    DestroySlot(s_slots[decoder]);

    return 0;
}
