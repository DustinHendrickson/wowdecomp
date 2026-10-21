file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (WOWDECOMP_PLATFORM_MAC)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Mac/*.cpp" "Mac/*.mm")
elseif (WOWDECOMP_PLATFORM_SWITCH)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Mac/*.cpp" "Switch/*.cpp")
elseif (WOWDECOMP_ARCH_64)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win64/*.cpp")
else ()
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win32/*.cpp")
endif ()

add_executable(Wow WIN32 MACOSX_BUNDLE ${PRIVATE_SOURCES} ${PLATFORM_SOURCES})

if (WOWDECOMP_PLATFORM_MAC)
    set(WOW_OUTPUT_NAME "World of Warcraft")
elseif (WOWDECOMP_PLATFORM_SWITCH)
    set(WOW_OUTPUT_NAME Wow)
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

# On the Switch, Wow.elf is packaged as WoW.nro, the homebrew executable the Homebrew Menu starts. It goes into
# sdmc:/switch/wow/ next to the game's Data folder (see docs/SWITCH_PORT.md).
if (WOWDECOMP_PLATFORM_SWITCH)
    # Wow.elf keeps the symbols: crash logs give addresses as offsets into it, for aarch64-none-elf-addr2line.
    set_target_properties(Wow PROPERTIES SUFFIX .elf)

    # The commit a build came from, printed at the top of Logs/Console.txt to match a log to its Wow.elf.
    execute_process(
        COMMAND git -c safe.directory=* rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE WOWDECOMP_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (NOT WOWDECOMP_BUILD_ID)
        set(WOWDECOMP_BUILD_ID unknown)
    endif ()
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Switch/NxMain.cpp PROPERTIES COMPILE_DEFINITIONS WOWDECOMP_BUILD_ID="${WOWDECOMP_BUILD_ID}")

    set(WOW_NACP ${CMAKE_CURRENT_BINARY_DIR}/Wow.nacp)
    set(WOW_NRO ${WOWDECOMP_BIN_DIR}/WoW.nro)
    set(WOW_ICON ${CMAKE_SOURCE_DIR}/Tools/Switch/icon.jpg)

    # Files packed into the NRO's RomFS: the SwitchControls add-on, installed into Interface/AddOns at start.
    set(WOW_ROMFS ${CMAKE_SOURCE_DIR}/Tools/Switch/romfs)
    file(GLOB_RECURSE WOW_ROMFS_FILES CONFIGURE_DEPENDS ${WOW_ROMFS}/*)

    add_custom_command(
        OUTPUT ${WOW_NACP}
        COMMAND ${NX_TOOLS}/nacptool --create "World of Warcraft" "wowdecomp" "3.3.5.12340" ${WOW_NACP}
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${WOW_NRO}
        COMMAND ${NX_TOOLS}/elf2nro $<TARGET_FILE:Wow> ${WOW_NRO} --nacp=${WOW_NACP} --icon=${WOW_ICON} --romfsdir=${WOW_ROMFS}
        DEPENDS Wow ${WOW_NACP} ${WOW_ICON} ${WOW_ROMFS_FILES}
        COMMENT "Packaging WoW.nro"
        VERBATIM
    )

    add_custom_target(WowNro ALL DEPENDS ${WOW_NRO})
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
