file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_MAC)
    # Portable code that sits in Win64/ because of where it lives in the original binary.
    list(APPEND PRIVATE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/System/File/Win64/File.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/System/File/Win64/Filestack_Streaming.cpp
        # Only Win32 events and waits, which Lock/Mac/Win32Sync.h provides.
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Packages/Streaming/Win64/Manifests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Packages/Streaming/Win64/Storage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Packages/Streaming/Win64/Streaming.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Packages/Streaming/Win64/Transport.cpp
    )
endif ()

add_library(BlizzardCore STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(BlizzardCore
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(BlizzardCore
    PUBLIC
        blast
        lzma
        zlib
)

if(WIN32)
    target_link_libraries(BlizzardCore
        PUBLIC
            wininet
    )
elseif (APPLE)
    # Blizzard::URL runs on the system libcurl.
    target_link_libraries(BlizzardCore
        PUBLIC
            curl
    )
endif()

target_include_directories(BlizzardCore
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/Source/SoundEngine/fmodex/lib/ogg_vorbis/ogg/include
        ${CMAKE_SOURCE_DIR}/Engine/Source/SoundEngine/fmodex/lib/ogg_vorbis/vorbis/include
)
