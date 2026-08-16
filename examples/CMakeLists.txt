cmake_minimum_required(VERSION 3.20)

project(hd_guest_examples LANGUAGES CXX)

set(HD_GUEST_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../out" CACHE PATH
    "Directory where guest shared libraries are written")
set(HD_GUEST_LLD "$ENV{LLD_PATH}" CACHE FILEPATH
    "Path to the LLD executable used for freestanding guest libraries")

if(NOT HD_GUEST_LLD)
    find_program(HD_GUEST_LLD NAMES ld.lld)
endif()
if(NOT HD_GUEST_LLD OR NOT EXISTS "${HD_GUEST_LLD}")
    message(FATAL_ERROR "LLD was not found; set HD_GUEST_LLD or LLD_PATH")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Guest examples require Clang")
endif()

function(add_guest_example target source output_name)
    add_library(${target} SHARED ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE
        -O2
        -fPIC
        -ffreestanding
        -fno-exceptions
        -fno-rtti
        -fno-stack-protector
    )
    target_link_options(${target} PRIVATE
        "--ld-path=${HD_GUEST_LLD}"
        -nostdlib
        LINKER:--hash-style=both
        LINKER:--build-id=none
        LINKER:--pack-dyn-relocs=none
        LINKER:-z,max-page-size=16384
    )
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        LIBRARY_OUTPUT_DIRECTORY "${HD_GUEST_OUTPUT_DIRECTORY}"
    )
endfunction()

add_guest_example(hdguest-hook guest_hook.cpp hdguest-hook)
add_guest_example(hdguest-probe guest_probe.cpp hdguest-probe)
