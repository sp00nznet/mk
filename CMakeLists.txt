cmake_minimum_required(VERSION 3.16)
project(SuperMarioKartRecomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
)
target_link_libraries(smk_game PUBLIC snesrecomp)

# --- smk_launcher (executable) ---
add_executable(smk_launcher src/main/main.c)
target_link_libraries(smk_launcher PRIVATE
    smk_game
    snesrecomp
    SDL2::SDL2main
)

# --- oam_dumper (debug tool: runs original ROM in LakeSnes, dumps OAM) ---
add_executable(oam_dumper tools/oam_dumper.c)
target_include_directories(oam_dumper PRIVATE ext/snesrecomp/ext/LakeSnes)
target_link_libraries(oam_dumper PRIVATE lakesnes_hw)
