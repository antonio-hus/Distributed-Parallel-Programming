cmake_minimum_required(VERSION 3.25)
project(Lab8_DSM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Set MPI paths for Microsoft MPI
set(MPI_HOME "C:/Program Files (x86)/Microsoft SDKs/MPI")
set(MPIEXEC_EXECUTABLE "C:/Program Files/Microsoft MPI/Bin/mpiexec.exe")

# C++ specific paths
set(MPI_CXX_INCLUDE_DIRS "${MPI_HOME}/Include")
set(MPI_CXX_INCLUDE_PATH "${MPI_HOME}/Include")
set(MPI_CXX_HEADER_DIR "${MPI_HOME}/Include")
set(MPI_CXX_LIB_NAMES "msmpi")
set(MPI_CXX_LIBRARIES "${MPI_HOME}/Lib/x64/msmpi.lib")
set(MPI_msmpi_LIBRARY "${MPI_HOME}/Lib/x64/msmpi.lib")

# C specific paths (sometimes needed)
set(MPI_C_INCLUDE_DIRS "${MPI_HOME}/Include")
set(MPI_C_INCLUDE_PATH "${MPI_HOME}/Include")
set(MPI_C_HEADER_DIR "${MPI_HOME}/Include")
set(MPI_C_LIB_NAMES "msmpi")
set(MPI_C_LIBRARIES "${MPI_HOME}/Lib/x64/msmpi.lib")

# Mark as found
set(MPI_CXX_FOUND TRUE)
set(MPI_C_FOUND TRUE)
set(MPI_FOUND TRUE)

message(STATUS "Manually configured MPI paths for Windows")
message(STATUS "MPI Include: ${MPI_CXX_INCLUDE_DIRS}")
message(STATUS "MPI Library: ${MPI_CXX_LIBRARIES}")

# Find MPI package (will use manual settings on Windows)
find_package(MPI REQUIRED)

# Verify MPI was found
if(NOT MPI_FOUND)
    message(FATAL_ERROR "MPI not found!")
endif()

# Create executable for DSM demo
add_executable(Lab8_DSM main.cpp)

# Link MPI to the executable
target_include_directories(Lab8_DSM PRIVATE ${MPI_CXX_INCLUDE_DIRS})
target_link_libraries(Lab8_DSM PRIVATE ${MPI_CXX_LIBRARIES})

# Print final configuration
message(STATUS "=== Final MPI Configuration ===")
message(STATUS "Include dirs: ${MPI_CXX_INCLUDE_DIRS}")
message(STATUS "Libraries: ${MPI_CXX_LIBRARIES}")
message(STATUS "Executable: mpiexec.exe")
