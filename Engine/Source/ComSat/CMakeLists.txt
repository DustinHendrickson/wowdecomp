file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_POSIX)
    # Byte-order helpers only; Os/Mac/Win32Sockets.h maps the Winsock names.
    list(APPEND PRIVATE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Win64/ComSatNetworkAddress.cpp)
endif ()

add_library(ComSat STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(ComSat
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
