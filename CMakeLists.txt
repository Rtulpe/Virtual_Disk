cmake_minimum_required(VERSION 3.31)
project(Virtual)

set(CMAKE_CXX_STANDARD 20)

add_executable(Virtual main.cpp
        VirtualFileSystem.h
        VirtualFileSystem.cpp)

set_target_properties(Virtual PROPERTIES
        RUNTIME_OUTPUT_NAME "vfs"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../"
)