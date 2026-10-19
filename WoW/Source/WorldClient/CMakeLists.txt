file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

add_library(WorldClient STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(WorldClient
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
