get_directory_property(SOUNDENGINE_DEFINITIONS COMPILE_DEFINITIONS)
list(REMOVE_ITEM SOUNDENGINE_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX)
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${SOUNDENGINE_DEFINITIONS}")

set(FMODEX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fmodex)
set(FMODEX_FLAC_DIR ${FMODEX_DIR}/lib/flac-1.1.4)

file(GLOB FMODEX_SOURCES CONFIGURE_DEPENDS
    "${FMODEX_DIR}/src/*.cpp"
    "${FMODEX_DIR}/lib/ogg_vorbis/ogg/src/*.c"
    "${FMODEX_DIR}/lib/ogg_vorbis/vorbis/lib/*.c"
    "${FMODEX_FLAC_DIR}/src/libFLAC/*.c"
    "${FMODEX_DIR}/lib/freeverb/*.cpp"
    "${FMODEX_DIR}/lib/sfx/foreverb/*.cpp"
)

# Platform layer: DirectSound / WinMM / ASIO on Windows. The macOS output layer (CoreAudio) goes in fmodex/mac/src.
if (WOWDECOMP_PLATFORM_WIN)
    set(FMODEX_PLATFORM_DIR ${FMODEX_DIR}/win32/src)
    file(GLOB FMODEX_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${FMODEX_PLATFORM_DIR}/*.cpp"
        "${FMODEX_PLATFORM_DIR}/asio/*.cpp"
        "${FMODEX_DIR}/lib/openal/utils/*.cpp"
    )
elseif (WOWDECOMP_PLATFORM_SWITCH)
    # The macOS OS and net layers are POSIX and shared; the output is libnx audout instead of CoreAudio.
    set(FMODEX_PLATFORM_DIR ${FMODEX_DIR}/switch/src)
    file(GLOB FMODEX_PLATFORM_SOURCES CONFIGURE_DEPENDS "${FMODEX_PLATFORM_DIR}/*.cpp")
    list(APPEND FMODEX_PLATFORM_SOURCES
        ${FMODEX_DIR}/mac/src/fmod_os_misc.cpp
        ${FMODEX_DIR}/mac/src/fmod_os_net.cpp
        ${FMODEX_DIR}/win32/src/fmod_dsp_resampler_linear.cpp
        ${FMODEX_DIR}/win32/src/fmod_dsp_distortion.cpp
    )
else ()
    set(FMODEX_PLATFORM_DIR ${FMODEX_DIR}/mac/src)
    file(GLOB FMODEX_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${FMODEX_PLATFORM_DIR}/*.cpp"
        "${FMODEX_PLATFORM_DIR}/*.mm"
    )
    # Plain C DSPs that sit in win32/src next to their x86 assembly versions (FMOD_SUPPORT_SIMD is off on Mac).
    list(APPEND FMODEX_PLATFORM_SOURCES
        ${FMODEX_DIR}/win32/src/fmod_dsp_resampler_linear.cpp
        ${FMODEX_DIR}/win32/src/fmod_dsp_distortion.cpp
    )
endif ()

add_library(fmodex STATIC ${FMODEX_SOURCES} ${FMODEX_PLATFORM_SOURCES})

target_include_directories(fmodex
    PUBLIC
        ${FMODEX_DIR}/src
    PRIVATE
        ${FMODEX_PLATFORM_DIR}
        ${FMODEX_DIR}/lib/ogg_vorbis/ogg/include
        ${FMODEX_DIR}/lib/ogg_vorbis/vorbis/include
        ${FMODEX_DIR}/lib/ogg_vorbis/vorbis/lib
        ${FMODEX_FLAC_DIR}/include
        ${FMODEX_FLAC_DIR}/src/libFLAC/include
        ${FMODEX_DIR}/lib/openal/include
        ${FMODEX_DIR}/win32/lib/wmsdk/include
        ${FMODEX_DIR}/lib/sfx/foreverb
        ${FMODEX_DIR}/lib/freeverb
)

target_compile_definitions(fmodex
    PRIVATE
        FLAC__NO_DLL
        FLAC__HAS_OGG=1
)

if (WOWDECOMP_PLATFORM_WIN)
    target_link_libraries(fmodex
        INTERFACE
            winmm
            ws2_32
            msacm32
    )
endif ()

if (MSVC)
    target_compile_options(fmodex PRIVATE /MP)
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(fmodex
        PRIVATE
            FLAC__CPU_IA32
            FLAC__HAS_NASM
            FLAC__USE_3DNOW
    )

    find_program(NASM_EXECUTABLE nasm)
    if (NOT NASM_EXECUTABLE)
        message(FATAL_ERROR
            "NASM not found. The Win32 build assembles FMOD Ex and libFLAC .s/.nasm sources. "
            "Put nasm on PATH or pass -DNASM_EXECUTABLE=<path to nasm.exe>."
        )
    endif ()

    set(FMODEX_ASM_OBJECTS)

    file(GLOB FMODEX_ASM_SOURCES CONFIGURE_DEPENDS "${FMODEX_DIR}/win32/src/*_asm.s")
    foreach (src ${FMODEX_ASM_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/fmodex_asm/${name}.obj)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fmodex_asm
            COMMAND ${NASM_EXECUTABLE} -f win32 -O0 -I${FMODEX_DIR}/win32/src/ -o ${obj} ${src}
            DEPENDS ${src} ${FMODEX_DIR}/win32/src/c32.mac
            VERBATIM
        )
        list(APPEND FMODEX_ASM_OBJECTS ${obj})
    endforeach ()

    foreach (name cpu_asm lpc_asm)
        set(src ${FMODEX_FLAC_DIR}/src/libFLAC/ia32/${name}.nasm)
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/fmodex_asm/${name}.obj)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fmodex_asm
            COMMAND ${NASM_EXECUTABLE} -f win32 -O0 -dOBJ_FORMAT_win32 -I${FMODEX_FLAC_DIR}/src/libFLAC/ia32/ -o ${obj} ${src}
            DEPENDS ${src} ${FMODEX_FLAC_DIR}/src/libFLAC/ia32/nasm.h
            VERBATIM
        )
        list(APPEND FMODEX_ASM_OBJECTS ${obj})
    endforeach ()

    set_source_files_properties(${FMODEX_ASM_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(fmodex PRIVATE ${FMODEX_ASM_OBJECTS})
endif ()

file(GLOB SOUNDENGINE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(SoundEngine STATIC
    ${SOUNDENGINE_SOURCES}
)

target_include_directories(SoundEngine
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(SoundEngine
    PUBLIC
        fmodex
)
