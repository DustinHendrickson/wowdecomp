# macOS only: the game window and input layer on their own (see main.mm).
add_executable(OsWindowTest MACOSX_BUNDLE main.mm)

set_target_properties(OsWindowTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_BIN_DIR}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.wowdecomp.oswindowtest
    MACOSX_BUNDLE_BUNDLE_NAME OsWindowTest
)

target_include_directories(OsWindowTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(OsWindowTest
    PRIVATE
        Os
        BlizzardCore
        Storm
        Base
        ${WOWDECOMP_MAC_FRAMEWORKS}
)
