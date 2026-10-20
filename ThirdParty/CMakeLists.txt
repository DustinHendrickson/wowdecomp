add_library(zlib STATIC
    zlib/adler32.c
    zlib/compress.c
    zlib/crc32.c
    zlib/deflate.c
    zlib/gzio.c
    zlib/infback.c
    zlib/inffast.c
    zlib/inflate.c
    zlib/inftrees.c
    zlib/trees.c
    zlib/uncompr.c
    zlib/zutil.c
)

target_include_directories(zlib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/zlib
)

add_library(blast STATIC
    zlib/contrib/blast/blast.c
)

target_include_directories(blast
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/zlib/contrib/blast
)

add_library(bzip2 STATIC
    bzip2/blocksort.c
    bzip2/bzlib.c
    bzip2/compress.c
    bzip2/crctable.c
    bzip2/decompress.c
    bzip2/huffman.c
    bzip2/randtable.c
)

target_include_directories(bzip2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/bzip2
)

add_library(lzma STATIC
    lzma/LzmaDecode.c
)

target_include_directories(lzma
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lzma
)

if (WIN32)
    add_library(nvapi INTERFACE)

    target_include_directories(nvapi
        INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/nvapi
    )

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_link_libraries(nvapi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nvapi/amd64/nvapi64.lib)
    else ()
        target_link_libraries(nvapi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nvapi/x86/nvapi.lib)
    endif ()
endif ()

add_library(freetype STATIC
    freetype/src/base/ftsystem.c
    freetype/src/base/ftinit.c
    freetype/src/base/ftdebug.c
    freetype/src/base/ftbase.c
    freetype/src/autohint/autohint.c
    freetype/src/cff/cff.c
    freetype/src/cid/type1cid.c
    freetype/src/psaux/psaux.c
    freetype/src/psnames/psnames.c
    freetype/src/raster/raster.c
    freetype/src/sfnt/sfnt.c
    freetype/src/smooth/smooth.c
    freetype/src/truetype/truetype.c
    freetype/src/type1/type1.c
    freetype/src/winfonts/winfnt.c
)

target_include_directories(freetype
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/freetype/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/freetype/src
)

add_library(expat STATIC
    expat/lib/xmlparse.c
    expat/lib/xmlrole.c
    expat/lib/xmltok.c
)

target_compile_definitions(expat
    PUBLIC
        _STATIC
    PRIVATE
        _LIB
)

if (WIN32)
    target_compile_definitions(expat PRIVATE COMPILED_FROM_DSP)
else ()
    target_include_directories(expat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/expat-mac)
endif ()

target_include_directories(expat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/expat/lib
)

add_library(pcre STATIC
    pcre/pcre.c
    pcre/get.c
    pcre/study.c
    pcre/maketables.c
)

target_compile_definitions(pcre
    PUBLIC
        PCRE_STATIC
    PRIVATE
        SUPPORT_UTF8
        SUPPORT_UCP
)

target_include_directories(pcre
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/pcre
)

add_library(DivxDecoder INTERFACE)

target_include_directories(DivxDecoder
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/DivxDecoder
)

if (WOWDECOMP_ARCH_32)
    set(DIVXDECODER_IMPORT_LIB ${CMAKE_CURRENT_BINARY_DIR}/DivxDecoder.lib)

    execute_process(
        COMMAND ${CMAKE_AR} /NOLOGO /MACHINE:X86 /DEF:${CMAKE_CURRENT_SOURCE_DIR}/DivxDecoder/DivxDecoder.def /OUT:${DIVXDECODER_IMPORT_LIB}
        RESULT_VARIABLE DIVXDECODER_RESULT
    )

    if (NOT DIVXDECODER_RESULT EQUAL 0)
        message(FATAL_ERROR "DivxDecoder: creating the import library failed")
    endif ()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/DivxDecoder/DivxDecoder.def)

    target_link_libraries(DivxDecoder
        INTERFACE
            ${DIVXDECODER_IMPORT_LIB}
    )

    set(DIVXDECODER_DLL ${CMAKE_CURRENT_SOURCE_DIR}/DivxDecoder/DivxDecoder.dll)

    if (EXISTS ${DIVXDECODER_DLL})
        add_custom_target(DivxDecoderDll
            COMMAND ${CMAKE_COMMAND} -E make_directory ${WOWDECOMP_BIN_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DIVXDECODER_DLL} ${WOWDECOMP_BIN_DIR}/DivxDecoder.dll
        )

        add_dependencies(DivxDecoder DivxDecoderDll)
    else ()
        message(WARNING "DivxDecoder: ${DIVXDECODER_DLL} is missing. Copy DivxDecoder.dll from a 3.3.5a (12340) client there; Win32 builds need it next to the executables.")
    endif ()
elseif (WIN32)
    target_link_libraries(DivxDecoder
        INTERFACE
            XvidDecoder
    )
endif ()
if (APPLE OR NINTENDO_SWITCH)
    # xvidcore 1.3.7 from source, plain C for arm64 (see xvidcore/README.md): the MPEG-4 decoder behind the movies.
    set(XVIDCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/xvidcore/src)
    set(XVIDCORE_SOURCES
        decoder.c encoder.c xvid.c
        bitstream/bitstream.c bitstream/cbp.c bitstream/mbcoding.c
        dct/fdct.c dct/idct.c dct/simple_idct.c
        image/colorspace.c image/image.c image/interpolate8x8.c image/font.c image/postprocessing.c image/qpel.c
        image/reduced.c
        motion/estimation_bvop.c motion/estimation_common.c motion/estimation_gmc.c motion/estimation_pvop.c
        motion/estimation_rd_based.c motion/estimation_rd_based_bvop.c motion/gmc.c motion/motion_comp.c
        motion/vop_type_decision.c motion/sad.c
        prediction/mbprediction.c
        plugins/plugin_single.c plugins/plugin_2pass1.c plugins/plugin_2pass2.c plugins/plugin_lumimasking.c
        plugins/plugin_dump.c plugins/plugin_psnr.c plugins/plugin_ssim.c plugins/plugin_psnrhvsm.c
        quant/quant_h263.c quant/quant_matrix.c quant/quant_mpeg.c
        utils/emms.c utils/mbtransquant.c utils/mem_align.c utils/mem_transfer.c utils/timer.c
    )
    list(TRANSFORM XVIDCORE_SOURCES PREPEND ${XVIDCORE_SRC}/)

    add_library(xvidcore STATIC ${XVIDCORE_SOURCES})
    target_include_directories(xvidcore PRIVATE ${XVIDCORE_SRC})
    # What xvidcore's configure picks for a 64-bit little-endian CPU without assembly support.
    target_compile_definitions(xvidcore PRIVATE ARCH_IS_GENERIC ARCH_IS_64BIT ARCH_IS_LITTLE_ENDIAN)
    target_compile_options(xvidcore PRIVATE -w)

    # The DivxDecoder entry points on xvidcore: Tools/XvidDecoder, the Windows x64 build's decoder DLL, linked in.
    add_library(DivxDecoderMac STATIC ${CMAKE_SOURCE_DIR}/Tools/XvidDecoder/XvidDecoder.cpp)
    target_include_directories(DivxDecoderMac PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/xvidcore/include)
    target_link_libraries(DivxDecoderMac PRIVATE xvidcore)
    target_link_libraries(DivxDecoder INTERFACE DivxDecoderMac)
endif ()

if (WIN32)
    add_library(lgLcd STATIC
        lgLcd/lglcd.cpp
    )

    target_include_directories(lgLcd
        PUBLIC
            ${CMAKE_SOURCE_DIR}
    )
endif ()
