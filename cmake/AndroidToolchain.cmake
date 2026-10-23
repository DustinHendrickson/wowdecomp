# CMake toolchain for Android (arm64-v8a), on top of the NDK's own:
#
#   cmake -S . -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/AndroidToolchain.cmake -DCMAKE_BUILD_TYPE=Release
#
# The NDK is found from ANDROID_NDK_ROOT / ANDROID_NDK_HOME, or the newest one under ANDROID_HOME (or
# ~/Library/Android/sdk). The game builds as libwow.so for NativeActivity (Tools/Android/build.sh makes the APK).

set(ANDROID_ABI arm64-v8a CACHE STRING "")
set(ANDROID_PLATFORM android-30 CACHE STRING "")
set(ANDROID_STL c++_static CACHE STRING "")

if (DEFINED ENV{ANDROID_NDK_ROOT})
    set(WOWDECOMP_NDK $ENV{ANDROID_NDK_ROOT})
elseif (DEFINED ENV{ANDROID_NDK_HOME})
    set(WOWDECOMP_NDK $ENV{ANDROID_NDK_HOME})
else ()
    if (DEFINED ENV{ANDROID_HOME})
        set(WOWDECOMP_SDK $ENV{ANDROID_HOME})
    else ()
        set(WOWDECOMP_SDK $ENV{HOME}/Library/Android/sdk)
    endif ()

    file(GLOB WOWDECOMP_NDKS LIST_DIRECTORIES true ${WOWDECOMP_SDK}/ndk/*)
    list(SORT WOWDECOMP_NDKS COMPARE NATURAL ORDER DESCENDING)
    list(GET WOWDECOMP_NDKS 0 WOWDECOMP_NDK)
endif ()

include(${WOWDECOMP_NDK}/build/cmake/android.toolchain.cmake)

# char is unsigned in the AArch64 ABI, but signed with MSVC (and on Apple arm64), which the decompiled code assumes:
# hashes, signature checks and byte math over char would come out different (as on the Switch).
string(APPEND CMAKE_C_FLAGS_INIT " -fsigned-char")
string(APPEND CMAKE_CXX_FLAGS_INIT " -fsigned-char")
