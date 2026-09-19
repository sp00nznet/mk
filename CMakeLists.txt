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
    src/recomp/smk_autogen.c
    src/recomp/smk_algorithms.c
    src/recomp/smk_semantic_verify.c
)
target_link_libraries(smk_game PUBLIC snesrecomp)

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

# Exhaustive semantic reconstruction gate; the ROM stays user-supplied.
add_executable(test_word_mix tools/test_word_mix.c src/recomp/smk_algorithms.c)
target_include_directories(test_word_mix PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(test_word_mix PRIVATE lakesnes_hw)

add_executable(test_vector_direction tools/test_vector_direction.c src/recomp/smk_algorithms.c)
target_include_directories(test_vector_direction PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(test_vector_direction PRIVATE lakesnes_hw)

add_executable(test_projection tools/test_projection.c src/recomp/smk_algorithms.c)
target_include_directories(test_projection PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(test_projection PRIVATE lakesnes_hw)

add_executable(test_object_order tools/test_object_order.c src/recomp/smk_algorithms.c)
target_include_directories(test_object_order PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(test_object_order PRIVATE lakesnes_hw)

add_executable(test_tile_dma tools/test_tile_dma.c src/recomp/smk_algorithms.c)
target_include_directories(test_tile_dma PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(test_tile_dma PRIVATE lakesnes_hw)

