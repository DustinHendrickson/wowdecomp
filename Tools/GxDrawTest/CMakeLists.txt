# macOS only: the OpenGL Gx device drawing into the game window (see main.mm).
add_executable(GxDrawTest MACOSX_BUNDLE main.mm)

set_target_properties(GxDrawTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_MAC_APP_DIR}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.wowdecomp.gxdrawtest
    MACOSX_BUNDLE_BUNDLE_NAME GxDrawTest
)

target_include_directories(GxDrawTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(GxDrawTest
    PRIVATE
        Gx
        Gxu
        Tempest
        Images
        BLPFile
        Event
        jpeg
        Os
        BlizzardCore
        Storm
        Base
        ${WOWDECOMP_MAC_FRAMEWORKS}
)
