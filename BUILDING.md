# P2P Node Compilation - Windows + Linux

The project is ready for cross-platform compilation. Choose according to your operating system.

## Windows

### Option 1: Automatic Build (Recommended)

Run the batch script directly:

'''cmd

build_windows.bat

'''

The script automatically:

- Verifies CMake and Visual Studio

- Creates a 'build' folder

- Performs CMake configuration

- Compiles the project in Release mode

- Creates a binary in 'buildbinReleasep2p_node.exe'

### Option 2: Manual Build with Visual Studio

'''cmd

mkdir build

cd build

cmake .. -G "Visual Studio 16 2019" # or "Visual Studio 17 2022"

cmake --build . --config Release

'''

### Option 3: MinGW (without Visual Studio)

'''cmd

mkdir build

cd build

cmake .. -G "MinGW Makefiles"

cmake --build .

'''

## Linux

### Automatic Build (Recommended)

'''bash

chmod +x build_linux.sh

./build_linux.sh

'''

The script automatically:

- Verifies CMake and C++ compiler

- Creates a 'build' folder

- Performs configuration

- Compiles with all available cores

- Creates a binary in 'build/bin/p2p_node'

### Manual Build

'''bash

mkdir build

cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)

'''

## Execution

### Windows

'''cmd

cd buildbinRelease

p2p_node.exe 6881 C:pathtostorage

'''

### Linux

'''bash

./build/bin/p2p_node 6881 ./storage

'''

## Parameters

- '[port]' - Port for listening (default: 6881)

- '[storage_path]' - Path to the directory for storing blocks (default: ./storage)

Both values are optional.

## Compilation Requirements

### Windows

- **CMake** 3.10+ (https://cmake.org/download/)

- **Visual Studio** 2019+ (https://visualstudio.microsoft.com/) OR

- **MinGW/GCC** 9.0+

### Linux

'''bash

# Ubuntu/Debian

sudo apt-get update

sudo apt-get install build-essential cmake

# Fedora/RHEL

sudo dnf install gcc-c++ cmake make

# Arch

sudo pacman -S gcc cmake make

'''

## Troubleshooting

### CMake cannot find the compiler

**Windows:**

Make sure you have Visual Studio or MinGW:

'''cmd

where cl.exe # Visual Studio

where g++.exe # MinGW

'''

**Linux:**

'''bash

which g++

which clang++

'''

### Memory error or build fails

Try cleaning and rebuilding:

**Windows:**

'''cmd

rmdir /s /q build

build_windows.bat

'''

**Linux:**

'''bash

rm -rf build

./build_linux.sh

'''

## Notes

- The project uses C++17

- Automatically detects Windows vs. Linux

- Winsock2 on Windows, POSIX sockets on Linux

- Pthread library is automatically linked on Linux

Everything is ready for seamless compilation on both platforms!