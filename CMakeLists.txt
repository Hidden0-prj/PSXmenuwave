cmake_minimum_required(VERSION 3.10)
project(PS1_Model_Test C ASM)

# Force CMake to use our cross-compiler instead of the Linux default 'cc'
set(CMAKE_C_COMPILER "mipsel-none-elf-gcc")
set(CMAKE_ASM_COMPILER "mipsel-none-elf-gcc")

# Define your source files and output executable
add_executable(game.psexe src/main.c)

# Tell the compiler where to find the PS1 SDK headers
target_include_directories(game.psexe PRIVATE ./include)

# Compilation flags tailored for PS1 hardware
target_compile_options(game.psexe PRIVATE -msoft-float -mips1 -Wall)
target_link_options(game.psexe PRIVATE -T linker.ld -nostartfiles)