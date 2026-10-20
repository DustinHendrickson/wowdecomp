# Build environment for the Nintendo Switch port: devkitA64 + libnx + portlibs, plus LLVM Clang.
#
# The decompiled code leans on Clang's MSVC compatibility (-fms-extensions, -fdeclspec), as the macOS port does, so
# sources are compiled with Clang targeting aarch64-none-elf against devkitA64's newlib, libstdc++ and libnx headers.
# devkitA64's GCC links, and devkitPro's tools package the NRO.
FROM devkitpro/devkita64:latest

RUN apt-get update \
 && apt-get install -y --no-install-recommends wget gnupg ca-certificates lsb-release \
 && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
 && echo "deb http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-19 main" > /etc/apt/sources.list.d/llvm.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends clang-19 lld-19 llvm-19 \
 && rm -rf /var/lib/apt/lists/*

ENV PATH=/usr/lib/llvm-19/bin:/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
