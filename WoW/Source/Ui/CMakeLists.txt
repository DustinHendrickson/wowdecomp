file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (NOT WIN32)
    # Logitech G-series LCD support. LCD.cpp is built around the Windows-only CEzLcd; macOS gets stubs instead.
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/UILCD/(Windows/|LCD\\.cpp$)")
endif ()

add_library(Ui STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(Ui
        PRIVATE
            lgLcd
    )
endif ()
