# macOS only: FMOD Ex on the CoreAudio output and Mac OS layer (see main.cpp).
add_executable(SoundTest main.cpp)

set_target_properties(SoundTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_BIN_DIR}
)

target_link_libraries(SoundTest
    PRIVATE
        fmodex
        ${WOWDECOMP_MAC_FRAMEWORKS}
)

# fmod_errors.h returns string literals as char*.
target_compile_options(SoundTest PRIVATE -Wno-writable-strings)
