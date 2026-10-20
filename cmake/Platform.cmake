# Platform selection for wowdecomp.
#
# Windows is the reference platform: its sources live in Win32/ and Win64/ folders (guarded inside by
# WOWDECOMP_ARCH_32 / WOWDECOMP_ARCH_64). The macOS port puts its implementations in Mac/ folders next to them.

if (WIN32)
    set(WOWDECOMP_PLATFORM_WIN 1)
elseif (APPLE)
    set(WOWDECOMP_PLATFORM_MAC 1)
    # AppKit glue (windows, events, dialogs, clipboard) is Objective-C++ in .mm files.
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
else ()
    message(FATAL_ERROR "wowdecomp targets Windows and macOS only.")
endif ()

# Adjusts a module's source list for the current platform: off Windows, drops the Win32/ and Win64/ folders and
# adds every .cpp and .mm under Mac/ folders. Call it right after the module's file(GLOB...) of *.cpp.
function (wowdecomp_platform_sources var)
    # Match folder names relative to the module, so a checkout path that happens to contain them doesn't drop sources.
    set(sources "")
    foreach (source ${${var}})
        file(RELATIVE_PATH relative ${CMAKE_CURRENT_SOURCE_DIR} ${source})
        set(relative "/${relative}")
        if (WOWDECOMP_PLATFORM_WIN AND relative MATCHES "/Mac/")
            continue ()
        endif ()
        if (NOT WOWDECOMP_PLATFORM_WIN AND relative MATCHES "/[Ww]in(32|64)/")
            continue ()
        endif ()
        list(APPEND sources ${source})
    endforeach ()
    if (NOT WOWDECOMP_PLATFORM_WIN)
        file(GLOB_RECURSE mac_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/*/Mac/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/*/Mac/*.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/Mac/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Mac/*.mm"
        )
        list(APPEND sources ${mac_sources})
        list(REMOVE_DUPLICATES sources)
    endif ()
    set(${var} ${sources} PARENT_SCOPE)
endfunction ()

# Flags for the project's own code (not ThirdParty) on macOS. Call it after add_subdirectory(ThirdParty).
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
    # Stand-ins for MSVC CRT headers that macOS lacks (<malloc.h>).
    include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/Mac)
    # arm64: <xmmintrin.h> / <emmintrin.h> resolve to sse2neon wrappers.
    if (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        include_directories(BEFORE SYSTEM
            ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Compat/MacArm64
            ${CMAKE_SOURCE_DIR}/ThirdParty/sse2neon
        )
    endif ()
endmacro ()

# Where the client executables go.
if (WOWDECOMP_PLATFORM_WIN)
    set(WOWDECOMP_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
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
