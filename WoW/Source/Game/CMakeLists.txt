file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_POSIX)
    # The Warden client keeps no Windows code: it registers the message handler and stores modules.
    list(APPEND PRIVATE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameClient/Win64/WardenClient.cpp)
endif ()

add_library(Game STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Game
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Game
    PUBLIC
        pcreposix
        freetype
)
