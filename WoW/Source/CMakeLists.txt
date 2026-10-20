file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (WOWDECOMP_PLATFORM_MAC)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Mac/*.cpp" "Mac/*.mm")
elseif (WOWDECOMP_ARCH_64)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win64/*.cpp")
else ()
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win32/*.cpp")
endif ()

add_executable(Wow WIN32 MACOSX_BUNDLE ${PRIVATE_SOURCES} ${PLATFORM_SOURCES})

if (WOWDECOMP_PLATFORM_MAC)
    set(WOW_OUTPUT_NAME "World of Warcraft")
elseif (WOWDECOMP_ARCH_64)
    set(WOW_OUTPUT_NAME Wow64)
else ()
    set(WOW_OUTPUT_NAME Wow32)
endif ()

set_target_properties(Wow PROPERTIES
    OUTPUT_NAME ${WOW_OUTPUT_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_BIN_DIR}
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(Wow PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${WOWDECOMP_BIN_DIR})
endforeach ()

target_include_directories(Wow
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

if (WOWDECOMP_PLATFORM_WIN)
    target_link_options(Wow PRIVATE "/INCREMENTAL:NO" "/ENTRY:start")
endif ()

# On macOS Wow is "World of Warcraft.app", as Blizzard's Mac client was named, built into bin/Mac/Apps.noindex (out of
# Spotlight). Like Wow.exe it belongs in the game folder, next to Data/: cmake --install <build> --prefix <3.3.5a
# folder> puts it there, or into /Applications, where it runs from a game folder chosen at launch (WoW/Source/Mac/main.mm).
if (WOWDECOMP_PLATFORM_MAC)
    set_target_properties(Wow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_MAC_APP_DIR})

    foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
        string(TOUPPER ${config} config)
        set_target_properties(Wow PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${WOWDECOMP_MAC_APP_DIR})
    endforeach ()

    wowdecomp_mac_bundle_icon(Wow Wow)

    set_target_properties(Wow PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Mac/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER org.wowdecomp.wow
        MACOSX_BUNDLE_BUNDLE_NAME "World of Warcraft"
        MACOSX_BUNDLE_ICON_FILE Wow
        MACOSX_BUNDLE_SHORT_VERSION_STRING 3.3.5
        MACOSX_BUNDLE_BUNDLE_VERSION 12340
    )

    # The linker signs the executable ad hoc; signing the bundle seals Info.plist and the icon with it.
    add_custom_command(TARGET Wow POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:Wow>
        VERBATIM
    )

    install(TARGETS Wow BUNDLE DESTINATION .)
endif ()

target_link_libraries(Wow
    PRIVATE
        BLPFile
        Base
        BlizzardCore
        ComSat
        ConsoleBase
        Event
        FFX
        Frame
        FrameScript
        lua
        FrameXML
        Gx
        Gxu
        Images
        Model2
        Os
        Scrn
        Services
        ShaderEffect
        Tempest
        SoundEngine
        SteelseriesMouse
        Storm
        WoWCommon
        Battlenet
        Console
        ConsoleDevice
        DB
        Game
        Glue
        LiquidSystem
        Magic
        Minigame
        Net
        Object
        ObjectAlloc
        ObjectMgrClient
        SoundInterface
        UIUtil
        Ui
        WorldClient
        WowServices
        WowSvcs
)
