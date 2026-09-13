# Builds libacoustical_dsp.so — the shared C DSP core wrapped in a thin JNI
# layer. The C core (dsp/acoustical_dsp.c) is the same code the Windows app
# links, so both platforms run identical FFT / band-aggregation.
cmake_minimum_required(VERSION 3.22.1)
project(acoustical_dsp LANGUAGES C CXX)

# dsp/ lives at the repo root: cpp -> main -> src -> app -> android-acoustical -> root
set(DSP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../dsp)

add_library(acoustical_dsp SHARED
    acoustical_jni.cpp
    ${DSP_ROOT}/acoustical_dsp.c
)

target_include_directories(acoustical_dsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DSP_ROOT}
)

target_compile_options(acoustical_dsp PRIVATE -O2)

find_library(log-lib log)
target_link_libraries(acoustical_dsp
    ${log-lib}
    m
)
