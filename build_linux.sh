#!/bin/bash

# P2P Node Build Script for Linux

set -e  # Exit on error

echo "[*] P2P Node Build Script (Linux)"
echo ""

# Check requirements
if ! command -v cmake &> /dev/null; then
    echo "[!] CMake not found. Please install CMake 3.10+"
    exit 1
fi

if ! command -v make &> /dev/null; then
    echo "[!] Make not found"
    exit 1
fi

# Check C++ compiler
if command -v g++ &> /dev/null; then
    CXX_VERSION=$(g++ --version | head -n1)
    echo "[+] Found compiler: $CXX_VERSION"
elif command -v clang++ &> /dev/null; then
    CXX_VERSION=$(clang++ --version | head -n1)
    echo "[+] Found compiler: $CXX_VERSION"
else
    echo "[!] No C++ compiler found"
    exit 1
fi

# Create build directory
if [ ! -d "build" ]; then
    echo "[+] Creating build directory..."
    mkdir build
else
    echo "[*] Build directory already exists"
fi

cd build

# Configure
echo "[+] Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "[+] Building..."
cmake --build . --config Release -j$(nproc)

# Create output message
echo ""
echo "[+] Build completed successfully!"
echo ""
echo "Binary location: ./build/bin/p2p_node"
echo ""
echo "To run the node:"
echo "  ./build/bin/p2p_node [port] [storage_path]"
echo ""
echo "Example:"
echo "  ./build/bin/p2p_node 6881 ./storage"
