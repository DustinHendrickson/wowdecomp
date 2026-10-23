# Platform selection for wowdecomp.
#
# Windows is the reference platform: its sources live in Win32/ and Win64/ folders (guarded inside by
# WOWDECOMP_ARCH_32 / WOWDECOMP_ARCH_64). The macOS port puts its implementations in Mac/ folders next to them.
#
# The Nintendo Switch port (cmake/SwitchToolchain.cmake) builds on the macOS port: most Mac/ code is plain POSIX and is
# shared. WOWDECOMP_PLATFORM_POSIX covers both. A Switch/ folder next to a Mac/ folder holds the Switch versions; a
# file there replaces the Mac/ file of the same name, and Objective-C++ (.mm) is macOS only.
#
# Android (the NDK's toolchain, cmake/AndroidToolchain.cmake) works the same way, with Android/ folders.

if (WIN32)
    set(WOWDECOMP_PLATFORM_WIN 1)
elseif (NINTENDO_SWITCH)
    set(WOWDECOMP_PLATFORM_SWITCH 1)
    set(WOWDECOMP_PLATFORM_POSIX 1)
elseif (ANDROID)
    set(WOWDECOMP_PLATFORM_ANDROID 1)
    set(WOWDECOMP_PLATFORM_POSIX 1)
elseif (APPLE)
    set(WOWDECOMP_PLATFORM_MAC 1)
    set(WOWDECOMP_PLATFORM_POSIX 1)
    # AppKit glue (windows, events, dialogs, clipboard) is Objective-C++ in .mm files.
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
else ()
    message(FATAL_ERROR "wowdecomp targets Windows, macOS, the Nintendo Switch and Android only.")
endif ()

# Adjusts a module's source list for the current platform: off Windows, drops the Win32/ and Win64/ folders and keeps
# Mac/ ones, adding their .mm files on macOS; on the Switch and Android, keeps that platform's Switch/ or Android/
# folders too, where a file replaces the Mac/ file of the same name in the same parent folder. Call it right after the
# module's file(GLOB_RECURSE...) of *.cpp.
function (wowdecomp_platform_sources var)
    set(overlay "")
    if (WOWDECOMP_PLATFORM_SWITCH)
        set(overlay Switch)
    elseif (WOWDECOMP_PLATFORM_ANDROID)
        set(overlay Android)
    endif ()

    # Match folder names relative to the module, so a checkout path that happens to contain them doesn't drop sources.
    set(sources "")
    set(overlay_sources "")
    foreach (source ${${var}})
        file(RELATIVE_PATH relative ${CMAKE_CURRENT_SOURCE_DIR} ${source})
        set(relative "/${relative}")
        if (WOWDECOMP_PLATFORM_WIN AND relative MATCHES "/(Mac|Switch|Android)/")
            continue ()
        endif ()
        if (NOT WOWDECOMP_PLATFORM_WIN AND relative MATCHES "/[Ww]in(32|64)/")
            continue ()
        endif ()
        if (relative MATCHES "/(Switch|Android)/")
            if (NOT overlay OR NOT relative MATCHES "/${overlay}/")
                continue ()
            endif ()
            list(APPEND overlay_sources ${source})
        endif ()
        list(APPEND sources ${source})
    endforeach ()
    foreach (source ${overlay_sources})
        string(REGEX REPLACE "/${overlay}/([^/]+)$" "/Mac/\\1" replaced ${source})
        list(REMOVE_ITEM sources ${replaced})
    endforeach ()
    if (WOWDECOMP_PLATFORM_MAC)
        file(GLOB_RECURSE mm_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
        foreach (source ${mm_sources})
            file(RELATIVE_PATH relative ${CMAKE_CURRENT_SOURCE_DIR} ${source})
            if ("/${relative}" MATCHES "/Mac/")
                list(APPEND sources ${source})
            endif ()
        endforeach ()
    endif ()
    set(${var} ${sources} PARENT_SCOPE)
endfunction ()

# Flags for the project's own code (not ThirdParty) on macOS and the Switch. Call it after add_subdirectory(ThirdParty).
macro (wowdecomp_mac_compile_options)
    add_compile_options(
        # Decompiled code type-puns and relies on MSVC-style layouts and extensions.
        -fno-strict-aliasing
        -fms-extensions
        -fdeclspec
        # Engine/Source/Base/Platform.h maps the Windows types, calling conventions and CRT names that shared code uses.
        "SHELL:-include ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Platform.h"
        -Wno-microsoft
        -Wno-ignored-attributes
        -Wno-unknown-pragmas
        -Wno-pragma-pack
        -Wno-invalid-offsetof
        -Wno-multichar
        -Wno-unused-value
        -Wno-switch
        -Wno-c++11-narrowing
        -Wno-deprecated-declarations
        # jpeg and lua are C built as C++ and use `register`.
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    # Stand-ins for MSVC CRT headers that macOS lacks (<malloc.h>). newlib has its own.
    if (WOWDECOMP_PLATFORM_MAC)
        include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/Mac)
    endif ()
    # Darwin headers the shared Mac/ layers include (<crt_externs.h>, <dlfcn.h>), for the Switch.
    if (WOWDECOMP_PLATFORM_SWITCH)
        include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/Switch)
    endif ()
    # Darwin headers the shared Mac/ layers include that bionic lacks (<crt_externs.h>), for Android. The NDK makes
    # non-literal format strings an error; Storm's printf wrappers pass theirs through, as on macOS.
    if (WOWDECOMP_PLATFORM_ANDROID)
        include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/Android)
        add_compile_options(-Wno-error=format-security)
    endif ()
    # arm64: <xmmintrin.h> / <emmintrin.h> resolve to sse2neon wrappers.
    if (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR WOWDECOMP_PLATFORM_SWITCH OR WOWDECOMP_PLATFORM_ANDROID)
        include_directories(BEFORE SYSTEM
            ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/MacArm64
            ${CMAKE_SOURCE_DIR}/ThirdParty/sse2neon
        )
    endif ()
endmacro ()

# Where the client executables go.
if (WOWDECOMP_PLATFORM_WIN)
    set(WOWDECOMP_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
elseif (WOWDECOMP_PLATFORM_SWITCH)
    set(WOWDECOMP_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Switch)
elseif (WOWDECOMP_PLATFORM_ANDROID)
    set(WOWDECOMP_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Android)
else ()
    set(WOWDECOMP_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Mac)
endif ()

# Where the macOS app bundles are built. Spotlight skips folders ending in .noindex (as Xcode's build folders do), so
# the build's copy of World of Warcraft.app doesn't show up next to the installed one; cmake --install copies it out.
set(WOWDECOMP_MAC_APP_DIR ${WOWDECOMP_BIN_DIR}/Apps.noindex)

# System libraries the client executables link on macOS.
set(WOWDECOMP_MAC_FRAMEWORKS
    "-framework Cocoa"
    "-framework OpenGL"
    "-framework IOKit"
    "-framework Carbon"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreServices"
    "-framework Security"
    "-framework SystemConfiguration"
)

# Builds the bundle icon from waystone.ico (32x32) with sips and iconutil and adds it to the target as a bundle
# resource. The name without extension is what Info.plist's CFBundleIconFile takes.
function(wowdecomp_mac_bundle_icon target name)
    set(ico ${CMAKE_SOURCE_DIR}/waystone.ico)
    set(work ${CMAKE_CURRENT_BINARY_DIR}/${name})
    set(icns ${CMAKE_CURRENT_BINARY_DIR}/${name}.icns)
    set(commands
        COMMAND ${CMAKE_COMMAND} -E make_directory ${work}.iconset
        COMMAND sips -s format png ${ico} --out ${work}.png
    )

    foreach (size 16 32 128 256 512)
        math(EXPR double "${size} * 2")
        list(APPEND commands
            COMMAND sips -z ${size} ${size} ${work}.png --out ${work}.iconset/icon_${size}x${size}.png
            COMMAND sips -z ${double} ${double} ${work}.png --out ${work}.iconset/icon_${size}x${size}@2x.png
        )
    endforeach ()

    add_custom_command(
        OUTPUT ${icns}
        ${commands}
        COMMAND iconutil -c icns ${work}.iconset -o ${icns}
        DEPENDS ${ico}
        COMMENT "Building ${name}.icns"
        VERBATIM
    )

    target_sources(${target} PRIVATE ${icns})
    set_source_files_properties(${icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endfunction ()
