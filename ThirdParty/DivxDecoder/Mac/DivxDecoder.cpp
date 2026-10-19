// macOS stand-in for DivxDecoder (the 3.3.5a client's DivxDecoder.dll; the Windows x64 build uses Tools/XvidDecoder).
// There is no MPEG-4 decoder on macOS yet, so every decoder fails to initialize and the client skips its movies.

#include "DivxDecoder.h"

extern "C" {

int32_t DivxDecode(int32_t decoder, DivxDecodeFrame* frame, int32_t flags) {
    return 1;
}

int32_t InitializeDivxDecoder(int32_t decoder, int32_t width, int32_t height) {
    return 1;
}

int32_t SetOutputFormat(int32_t decoder, int32_t format, int32_t width, int32_t height) {
    return 1;
}

int32_t UnInitializeDivxDecoder(int32_t decoder) {
    return 0;
}

}
