#ifndef ENGINE_SOURCE_OS_ANDROID_ANDROIDSYSTEM_H
#define ENGINE_SOURCE_OS_ANDROID_ANDROIDSYSTEM_H

// Android services the shared POSIX layers call, implemented through the activity (JNI) in Os/Android.

#include <cstdint>

// Opens a URL or file in the app Android picks for it (a browser for http links). False if nothing could.
bool AndroidOpenURL(const char* url);

// The screen in pixels, landscape (the activity's window once it exists, the display's metrics before).
void AndroidGetScreenSize(int32_t* width, int32_t* height);

#endif
