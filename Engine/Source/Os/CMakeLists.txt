file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_platform_sources(PRIVATE_SOURCES)

if (WOWDECOMP_PLATFORM_POSIX)
    # OsFile only wraps Blizzard::File; Mac/Win32Files.h supplies the Win32 constants it uses.
    list(APPEND PRIVATE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Win64/OsFile.cpp
    )
endif ()

add_library(Os STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Os
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(Os
        PUBLIC
            imm32
            wininet
            ws2_32
            dinput8
            dxguid
            version
    )
elseif (APPLE)
    target_link_libraries(Os
        PUBLIC
            ${WOWDECOMP_MAC_FRAMEWORKS}
    )
endif()
