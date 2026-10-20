file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_POSIX)
    # BSD socket code under Winsock names; Os/Mac/Win32Sockets.h maps them. The event loop has a Mac/ version.
    list(APPEND PRIVATE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Win64/WowConnection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Win64/BattlenetLogin.cpp
    )
endif ()

add_library(WowServices STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(WowServices
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
