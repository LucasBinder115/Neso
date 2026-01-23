cmake_minimum_required(VERSION 3.4.1)
project(Neso)

# Cria a biblioteca libneso.so
# Optimization Flags & Strict Audit (Layer F)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math -flto -Wall -Wextra -Werror -Wno-unused-parameter -Wshadow")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O3 -g")

add_library( neso
             SHARED
             cpu.cpp
             ppu.cpp
             apu.cpp
             rom.cpp
             mapper.cpp
             renderer.cpp
             jni_bridge.cpp )

# Links necessários
target_link_libraries( neso
                       android
                       log ) 