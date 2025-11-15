cmake_minimum_required(VERSION 3.25)
project(Lab8)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSYS2 MinGW64 OpenCL paths
# Headers:  C:/msys64/mingw64/include/CL/cl.h
# Library:  C:/msys64/mingw64/lib/libOpenCL.dll.a
set(OpenCL_INCLUDE_DIR "C:/msys64/mingw64/include")
set(OpenCL_LIBRARY     "C:/msys64/mingw64/lib/libOpenCL.dll.a")

add_executable(Lab8 main.cpp)

target_include_directories(Lab8 PRIVATE "${OpenCL_INCLUDE_DIR}")
target_link_libraries(Lab8 PRIVATE "${OpenCL_LIBRARY}")

target_compile_options(Lab8 PRIVATE -O3)

message(STATUS "OpenCL include dir: ${OpenCL_INCLUDE_DIR}")
message(STATUS "OpenCL library    : ${OpenCL_LIBRARY}")
