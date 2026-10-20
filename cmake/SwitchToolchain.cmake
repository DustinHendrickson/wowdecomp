# CMake toolchain for the Nintendo Switch (homebrew, libnx). Use it inside the Tools/Switch Docker image:
#
#   cmake -S . -B build/switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/SwitchToolchain.cmake -DCMAKE_BUILD_TYPE=Release
#
# Sources are compiled with LLVM Clang for aarch64-none-elf, because the decompiled code depends on Clang's MSVC
# compatibility (-fms-extensions, -fdeclspec) as on macOS. Headers and libraries are devkitA64's (newlib, libstdc++)
# and libnx's, and devkitA64's GCC links with libnx's switch.specs, so the result is an ordinary libnx homebrew ELF that
# elf2nro packages.
#
# Thread-local variables use emulated TLS (libgcc's __emutls_get_address): libnx keeps the ELF thread pointer behind
# __aarch64_read_tp (GCC's -mtp=soft), which Clang can't call for native TLS.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(NINTENDO_SWITCH TRUE)

if (DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
else ()
    set(DEVKITPRO /opt/devkitpro)
endif ()

set(DEVKITA64 ${DEVKITPRO}/devkitA64)
set(NX_ROOT ${DEVKITPRO}/libnx)
set(NX_PORTLIBS ${DEVKITPRO}/portlibs/switch)
set(NX_TOOLS ${DEVKITPRO}/tools/bin)

file(GLOB NX_GCC_VERSION_DIRS LIST_DIRECTORIES true ${DEVKITA64}/lib/gcc/aarch64-none-elf/*)
list(GET NX_GCC_VERSION_DIRS 0 NX_GCC_VERSION_DIR)
get_filename_component(NX_GCC_VERSION ${NX_GCC_VERSION_DIR} NAME)
set(NX_LIBSTDCXX_INCLUDE ${DEVKITA64}/aarch64-none-elf/include/c++/${NX_GCC_VERSION})

find_program(CMAKE_C_COMPILER NAMES clang-19 clang REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES clang++-19 clang++ REQUIRED)
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_AR ${DEVKITA64}/bin/aarch64-none-elf-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${DEVKITA64}/bin/aarch64-none-elf-ranlib CACHE FILEPATH "")
set(NX_GXX ${DEVKITA64}/bin/aarch64-none-elf-g++)

set(NX_ARCH_FLAGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -femulated-tls")
set(NX_SYSTEM_INCLUDES
    "-isystem ${NX_ROOT}/include -isystem ${NX_PORTLIBS}/include -isystem ${DEVKITA64}/aarch64-none-elf/include"
)

# char is unsigned in the AArch64 ELF ABI, but signed with MSVC (and on Apple arm64), which the decompiled code assumes:
# hashes, signature checks and byte math over char would come out different.
set(NX_COMMON_FLAGS "${NX_ARCH_FLAGS} -fsigned-char -ffunction-sections -fdata-sections -D__SWITCH__")
set(CMAKE_C_FLAGS_INIT "${NX_COMMON_FLAGS} ${NX_SYSTEM_INCLUDES}")
# libstdc++ first: its <cstdlib> and friends #include_next the newlib headers that follow.
set(CMAKE_CXX_FLAGS_INIT
    "${NX_COMMON_FLAGS} -nostdinc++ -isystem ${NX_LIBSTDCXX_INCLUDE} -isystem ${NX_LIBSTDCXX_INCLUDE}/aarch64-none-elf ${NX_SYSTEM_INCLUDES}"
)

# GCC links: libnx's specs file brings crt0, the linker script and the PIE settings that elf2nro expects. The game
# libraries depend on each other in every direction (ld64 on macOS doesn't mind), so they link as one group.
set(NX_LINK_FLAGS "-specs=${NX_ROOT}/switch.specs -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -L${NX_PORTLIBS}/lib -L${NX_ROOT}/lib")
set(CMAKE_C_LINK_EXECUTABLE "${NX_GXX} ${NX_LINK_FLAGS} <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group -lnx -lm")
set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_C_LINK_EXECUTABLE}")

set(CMAKE_FIND_ROOT_PATH ${NX_PORTLIBS} ${NX_ROOT} ${DEVKITA64}/aarch64-none-elf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler checks build a static library: a bare-metal test executable can't link without the specs above.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
