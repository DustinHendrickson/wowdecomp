file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

add_library(WoWCommon STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(WoWCommon
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
