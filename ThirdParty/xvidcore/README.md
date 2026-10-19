# xvidcore

- `include/xvid.h` and `lib/x64/libxvidcore.lib`: the prebuilt decoder the Windows x64 build links
  (`Tools/XvidDecoder`).
- `src/`: the C sources of xvidcore 1.3.7, built from source on macOS (generic C, no assembly). Taken from
  `xvidcore-1.3.7.tar.gz` (downloads.xvid.com, SHA-256
  `abbdcbd39555691dd1c9b4d08f0a031376a3b211652c0d8b3b8aa9be1303ce2d`), without the assembly (`*_asm` directories, `nasm.inc`) and
  makefiles.

xvidcore is GPL v2 (`licenses/xvid.txt`, `src/LICENSE`).
