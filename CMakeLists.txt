cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
project(nxnative C ASM)

if(NOT DEFINED GAME OR "${GAME}" STREQUAL "")
    message(FATAL_ERROR "GAME is not set: pass -DGAME=<base game title ID> (e.g. -DGAME=010015100B514000)")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DM_GAMES_DIR "$ENV{DM_GAMES_DIR}" CACHE STRING "Extra game profile directories")
set(GAME_H "${CMAKE_BINARY_DIR}/gen/${GAME}/game_profile.h")
set(GAME_TSV_DEP "${CMAKE_SOURCE_DIR}/dm/games/game.py")
if(EXISTS "${CMAKE_SOURCE_DIR}/dm/games/${GAME}/game.tsv")
    list(APPEND GAME_TSV_DEP "${CMAKE_SOURCE_DIR}/dm/games/${GAME}/game.tsv")
endif()
add_custom_command(
    OUTPUT "${GAME_H}"
    COMMAND "${CMAKE_COMMAND}" -E env "DM_GAMES_DIR=${DM_GAMES_DIR}" "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/dm/games/game.py" header "${GAME}" "${GAME_H}"
    DEPENDS ${GAME_TSV_DEP}
    COMMENT "Generating game_profile.h for ${GAME}"
)
add_custom_target(game_profile_hdr DEPENDS "${GAME_H}")

# Third-party: libvpx (VP8 / VP9 decoder)
set(VPX_DIR "${CMAKE_SOURCE_DIR}/dm/third_party/libvpx")
file(GLOB_RECURSE VPX_SRCS "${VPX_DIR}/*.c")
add_library(vpx STATIC ${VPX_SRCS})
if(WIN32)
    target_include_directories(vpx BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/dm/win/include")
    target_compile_definitions(vpx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
target_include_directories(vpx PRIVATE "${VPX_DIR}" "${VPX_DIR}/config")
target_compile_options(vpx PRIVATE -O2 -Wno-everything)

# Host and graphics sources
file(GLOB HOST_HLE_SRCS "${CMAKE_SOURCE_DIR}/dm/host/hle_*.c")
if(WIN32)
    list(REMOVE_ITEM HOST_HLE_SRCS "${CMAKE_SOURCE_DIR}/dm/host/hle_movie_avc.c")
endif()

set(HOST_SRCS
    "${CMAKE_SOURCE_DIR}/dm/host/main.c"
    "${CMAKE_SOURCE_DIR}/dm/host/loader.c"
    "${CMAKE_SOURCE_DIR}/dm/host/hle.c"
    "${CMAKE_SOURCE_DIR}/dm/host/crash.c"
    "${CMAKE_SOURCE_DIR}/dm/host/probe.c"
    "${CMAKE_SOURCE_DIR}/dm/host/thread.c"
    "${CMAKE_SOURCE_DIR}/dm/host/entry.S"
    ${HOST_HLE_SRCS}
)

file(GLOB GFX_SRCS
    "${CMAKE_SOURCE_DIR}/dm/gfx/*.c"
    "${CMAKE_SOURCE_DIR}/dm/gfx/nvn/*.c"
    "${CMAKE_SOURCE_DIR}/dm/gfx/tex/*.c"
)

if(WIN32)
    set(NVN_THUNKS_S "${CMAKE_BINARY_DIR}/gen/nvn_thunks_win.S")
    add_custom_command(
        OUTPUT "${NVN_THUNKS_S}"
        COMMAND "${Python3_EXECUTABLE}" -c
            "import pathlib; p = pathlib.Path(r'${CMAKE_SOURCE_DIR}/dm/gfx/nvn/nvn_thunks.S').read_text(); p = p.replace('_nvn_slot@PAGEOFF', ':lo12:nvn_slot').replace('_nvn_slot@PAGE', 'nvn_slot').replace('_nvnth_', 'nvnth_').replace('_nvn_thunk', 'nvn_thunk').replace('_nvn_names', 'nvn_names').replace('.cstring', '.data'); out = pathlib.Path(r'${NVN_THUNKS_S}'); out.parent.mkdir(parents=True, exist_ok=True); out.write_text(p)"
        DEPENDS "${CMAKE_SOURCE_DIR}/dm/gfx/nvn/nvn_thunks.S"
        COMMENT "Generating COFF ARM64 nvn_thunks_win.S"
    )
    list(APPEND HOST_SRCS
        "${CMAKE_SOURCE_DIR}/dm/win/win_compat.c"
        "${CMAKE_SOURCE_DIR}/dm/win/front_win.c"
        "${CMAKE_SOURCE_DIR}/dm/win/nvn_win_stub.c"
        "${CMAKE_SOURCE_DIR}/dm/win/win_audio.c"
        "${CMAKE_SOURCE_DIR}/dm/win/vk_win.c"
    )
else()
    set(NVN_THUNKS_S "${CMAKE_SOURCE_DIR}/dm/gfx/nvn/nvn_thunks.S")
endif()

add_executable(dmhost
    ${HOST_SRCS}
    ${GFX_SRCS}
    "${CMAKE_SOURCE_DIR}/dm/gfx/nvn/nvn_entry.S"
    "${NVN_THUNKS_S}"
)
add_dependencies(dmhost game_profile_hdr)

if(WIN32)
    target_include_directories(dmhost BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/dm/win/include")
    target_compile_definitions(dmhost PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
    target_compile_options(dmhost PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include \"${CMAKE_SOURCE_DIR}/dm/win/include/win_posix.h\">"
    )
endif()

target_include_directories(dmhost PRIVATE
    "${CMAKE_SOURCE_DIR}/dm/host"
    "${CMAKE_SOURCE_DIR}/dm/gfx/nvn"
    "${CMAKE_SOURCE_DIR}/dm/gfx/nvn/include"
    "${CMAKE_SOURCE_DIR}/dm/gfx/tex"
    "${CMAKE_SOURCE_DIR}/dm/third_party/vulkan-headers/include"
    "${CMAKE_BINARY_DIR}/gen/${GAME}"
    "${VPX_DIR}"
    "${CMAKE_SOURCE_DIR}/dm/third_party/stb_vorbis"
)

target_compile_options(dmhost PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O1 -g -Wall -Wno-unused-function -Wno-deprecated-declarations>
)

target_link_libraries(dmhost PRIVATE vpx user32 gdi32 winmm ole32)
