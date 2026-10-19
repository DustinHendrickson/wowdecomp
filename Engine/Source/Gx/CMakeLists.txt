file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (NOT WIN32)
    # The OpenGL device is WGL-bound for now; it comes back on macOS once its context code has a Mac/ twin.
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDevice(D3d|D3d9Ex|OpenGL)/")
endif ()

add_library(Gx STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Gx
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(Gx
        PRIVATE
            d3d9.lib
            nvapi
            opengl32.lib
    )
endif ()
