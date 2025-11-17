cmake_minimum_required(VERSION 3.15)
project(Timetabling CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Common include dir
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

###########################
#   COMMON SOURCE FILES   #
###########################
set(TIMETABLING_CORE_SOURCES
        src/constraints.cpp
        src/formatting.cpp
        src/demo_instances.cpp
        sequential/sequential_solver.cpp
        threads/threaded_solver.cpp
)

###########################
#   SEQUENTIAL EXECUTABLE #
###########################
add_executable(timetable_seq
        ${TIMETABLING_CORE_SOURCES}
        sequential/sequential_main.cpp
)

if (MSVC)
    target_compile_options(timetable_seq PRIVATE /W4 /permissive-)
else()
    target_compile_options(timetable_seq PRIVATE -Wall -Wextra -Wpedantic)
endif()

###########################
#   THREADED EXECUTABLE   #
###########################
add_executable(timetable_thr
        ${TIMETABLING_CORE_SOURCES}
        threads/threaded_main.cpp
)

if (MSVC)
    target_compile_options(timetable_thr PRIVATE /W4 /permissive-)
else()
    target_compile_options(timetable_thr PRIVATE -Wall -Wextra -Wpedantic)
endif()

###########################
#        MPI SETUP       #
###########################
set(MPI_HOME "C:/Program Files (x86)/Microsoft SDKs/MPI")
set(MPIEXEC_EXECUTABLE "C:/Program Files/Microsoft MPI/Bin/mpiexec.exe")

set(MPI_CXX_INCLUDE_DIRS "${MPI_HOME}/Include")
set(MPI_CXX_LIBRARIES    "${MPI_HOME}/Lib/x64/msmpi.lib")

message(STATUS "Manually configured MPI paths for Windows")
message(STATUS "MPI Include: ${MPI_CXX_INCLUDE_DIRS}")
message(STATUS "MPI Library: ${MPI_CXX_LIBRARIES}")

###########################
#     MPI EXECUTABLE     #
###########################
add_executable(timetable_mpi
        ${TIMETABLING_CORE_SOURCES}
        mpi/mpi_solver.cpp
        mpi/mpi_main.cpp
)

target_include_directories(timetable_mpi PRIVATE
        ${MPI_CXX_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timetable_mpi PRIVATE
        ${MPI_CXX_LIBRARIES}
)

if (MSVC)
    target_compile_options(timetable_mpi PRIVATE /W4 /permissive-)
else()
    target_compile_options(timetable_mpi PRIVATE -Wall -Wextra -Wpedantic)
endif()

###########################
#       OPENCL SETUP      #
###########################
# MSYS2 MinGW64 OpenCL
set(OpenCL_INCLUDE_DIR "C:/msys64/mingw64/include")
set(OpenCL_LIBRARY     "C:/msys64/mingw64/lib/libOpenCL.dll.a")

message(STATUS "OpenCL include dir: ${OpenCL_INCLUDE_DIR}")
message(STATUS "OpenCL library    : ${OpenCL_LIBRARY}")

###########################
#    OPENCL EXECUTABLE    #
###########################
add_executable(timetable_ocl
        ${TIMETABLING_CORE_SOURCES}
        opencl/opencl_evaluator.cpp
        opencl/opencl_solver.cpp
        opencl/opencl_main.cpp
)

target_include_directories(timetable_ocl PRIVATE
        "${OpenCL_INCLUDE_DIR}"
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timetable_ocl PRIVATE
        "${OpenCL_LIBRARY}"
)

if (MSVC)
    target_compile_options(timetable_ocl PRIVATE /W4 /permissive-)
else()
    target_compile_options(timetable_ocl PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
