cmake_minimum_required(VERSION 3.16)
project(SuperMarioKartRecomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL2
find_package(SDL2 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- smk_runtime (static lib) ---
add_library(smk_runtime STATIC
    src/recomp/cpu.c
    src/recomp/memory.c
    src/recomp/func_table.c
)

# --- smk_hal (static lib) ---
add_library(smk_hal STATIC
    src/hal/ppu.c
    src/hal/apu.c
    src/hal/dma.c
    src/hal/dsp1.c
    src/hal/io.c
)
target_link_libraries(smk_hal PRIVATE smk_runtime)

# --- smk_platform (static lib) ---
add_library(smk_platform STATIC
    src/platform/sdl_backend.c
    src/platform/input.c
)
target_link_libraries(smk_platform PRIVATE SDL2::SDL2 smk_hal)

# --- smk_launcher (executable) ---
add_executable(smk_launcher src/main/main.c)
target_link_libraries(smk_launcher PRIVATE
    smk_runtime
    smk_hal
    smk_platform
    SDL2::SDL2
    SDL2::SDL2main
)
