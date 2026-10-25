cmake_minimum_required(VERSION 3.16)
project(SuperMarioKartRecomp C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2
find_package(SDL2 REQUIRED)

# --- snesrecomp: real SNES hardware backend (LakeSnes + adapter) ---
set(SNESRECOMP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(ext/snesrecomp)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- smk_game (static lib: recompiled game code) ---
add_library(smk_game STATIC
    src/recomp/smk_boot.c
    src/recomp/smk_init.c
    src/recomp/smk_title.c
    src/recomp/smk_algorithms.c
)
target_link_libraries(smk_game PUBLIC snesrecomp)

# smk_autogen.c is autogen.py output generated from your ROM, so it is never
# committed (see docs/autogen.md). Built in when you have generated it locally.
if(EXISTS ${CMAKE_SOURCE_DIR}/src/recomp/smk_autogen.c)
    target_sources(smk_game PRIVATE src/recomp/smk_autogen.c)
    target_compile_definitions(smk_game PUBLIC SMK_HAVE_AUTOGEN=1)
endif()

# --- smk_launcher (executable) ---
add_executable(smk_launcher src/main/main.c)
target_link_libraries(smk_launcher PRIVATE
    smk_game
    snesrecomp
    SDL2::SDL2main
)
# snesrecomp now pulls in C++ (ImGui menu) — link the executable as C++ so the
# C++ runtime is included.
set_target_properties(smk_launcher PROPERTIES LINKER_LANGUAGE CXX)

# --- oam_dumper (debug tool: runs original ROM in LakeSnes, dumps OAM) ---
add_executable(oam_dumper tools/oam_dumper.c)
target_include_directories(oam_dumper PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(oam_dumper PRIVATE lakesnes_hw)

# --- lakesnes_ref (ground truth: runs original ROM via native snes_runFrame,
#     dumps SMKSNAP2 snapshot + BMP; used to isolate the Mode-7 garble) ---
add_executable(lakesnes_ref tools/lakesnes_ref.c)
target_include_directories(lakesnes_ref PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(lakesnes_ref PRIVATE lakesnes_hw)

option(SMK_BUILD_SEMANTIC_TESTS "Build semantic checks and ROM CPU oracle tests" OFF)
if(SMK_BUILD_SEMANTIC_TESTS)
    add_executable(test_word_mix_unit tools/test_word_mix_unit.c src/recomp/smk_algorithms.c)
    enable_testing()
    add_test(NAME word_mix_unit COMMAND test_word_mix_unit)

    add_executable(test_word_mix tools/test_word_mix.c src/recomp/smk_algorithms.c)
    target_include_directories(test_word_mix PRIVATE ext/snesrecomp/ext/LakeSnes)
    target_link_libraries(test_word_mix PRIVATE lakesnes_hw)
endif()
