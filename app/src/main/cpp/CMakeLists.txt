cmake_minimum_required(VERSION 3.4.1)

# Cria a biblioteca libneso.so
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