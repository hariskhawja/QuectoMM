cmake_minimum_required(VERSION 3.20)

project(QuectoMM
    VERSION 0.1.0
    DESCRIPTION "QuectoMM - discrete-event market-making research engine"
    LANGUAGES CXX)

# ---- Global C++ settings ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # use -std=c++20, not -std=gnu++20

# Default to a Release-ish build if the user didn't pick one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# Put all binaries in build/bin so they're easy to find. Multi-config
# generators (Visual Studio, Xcode) would otherwise add a per-config subfolder
# (build/bin/Release/...), so pin every config to the same directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Warnings: strict, because this is research code we need to trust.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

# On MinGW, statically link the GCC/g++ runtimes so our executables don't
# depend on libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll being
# on PATH at launch. Makes the binaries self-contained and portable.
if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# ---- Subdirectories ----
add_subdirectory(engine)
add_subdirectory(apps)
