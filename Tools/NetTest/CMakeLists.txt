# macOS only: WowConnection on the Mac network loop against a loopback server (see main.cpp).
add_executable(NetTest main.cpp)

set_target_properties(NetTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_BIN_DIR}
)

target_include_directories(NetTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(NetTest
    PRIVATE
        WowServices
        ObjectAlloc
        Os
        BlizzardCore
        Storm
        Base
        ${WOWDECOMP_MAC_FRAMEWORKS}
)
