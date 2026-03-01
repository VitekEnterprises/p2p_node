cmake_minimum_required(VERSION 3.10)
project(p2p_node CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-specific settings
if(WIN32)
    # Windows
    set(PLATFORM_LIBS ws2_32 iphlpapi)
    message(STATUS "Building for Windows")
else()
    # Linux/Unix
    set(PLATFORM_LIBS pthread)
    message(STATUS "Building for Unix-like system")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/util/sha256.cpp
    src/network/udp_socket.cpp
)

# Header directories
include_directories(src)

# Create executable
add_executable(p2p_node ${SOURCES})

# Link libraries
target_link_libraries(p2p_node PUBLIC ${PLATFORM_LIBS})

# Compiler flags
if(MSVC)
    # Visual Studio
    target_compile_options(p2p_node PRIVATE /W4)
else()
    # GCC/Clang
    target_compile_options(p2p_node PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Output directory
set_target_properties(p2p_node PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "P2P Node build configured successfully")
