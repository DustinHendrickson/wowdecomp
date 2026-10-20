file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_POSIX)
    # Portable code that sits in Win64/ because of where it lives in the original binary.
    list(APPEND PRIVATE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Win64/SMsg.cpp
    )
endif ()

add_library(Storm STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Storm
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Storm
    PUBLIC
        blast
        bzip2
        zlib
)
