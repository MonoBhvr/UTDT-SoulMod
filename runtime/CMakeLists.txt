cmake_minimum_required(VERSION 3.22)
project(SoulModRuntime VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure x64 architecture
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "UTDT SoulMod Runtime requires a 64-bit build target (x64).")
endif()

# The promoted runtime modules consume the Win32 API (windows.h, psapi) and
# GameMaker x64 internals. Non-Windows hosts cannot compile the product target;
# fail loudly instead of silently selecting a scratch/POC fallback.
if(NOT WIN32)
    message(FATAL_ERROR
        "SoulMod.Runtime promoted modules require the Win32 API (windows.h, psapi). "
        "Configure on a Windows x64 host with MSVC v143. The production target does not "
        "fall back to scratch or POC sources on this platform."
    )
endif()

enable_testing()

add_subdirectory(SoulMod.Runtime)
