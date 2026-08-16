cmake_minimum_required(VERSION 3.20)

project(hd_tool VERSION 0.1.0 LANGUAGES CXX)

option(HD_TOOL_STATIC_LINK "Link hd-tool as a self-contained static executable" ON)
set(HD_TOOL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../out" CACHE PATH
    "Directory where the hd-tool executable is written")

add_executable(hd-tool
    src/app/application.cpp
    src/injector/injector.cpp
    src/main.cpp
    src/memory/process_memory.cpp
    src/memory/proc_maps.cpp
)

target_compile_features(hd-tool PRIVATE cxx_std_20)
target_include_directories(hd-tool PRIVATE src)
target_compile_definitions(hd-tool PRIVATE _GNU_SOURCE)
target_compile_options(hd-tool PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
)

if(HD_TOOL_STATIC_LINK)
    target_link_options(hd-tool PRIVATE -static)
endif()
set_target_properties(hd-tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${HD_TOOL_OUTPUT_DIRECTORY}"
)

install(TARGETS hd-tool RUNTIME DESTINATION bin)
