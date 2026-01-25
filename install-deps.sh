#!/bin/bash

set -euo pipefail

echo "Installing OpenMW dependencies..."

if [ "$EUID" -ne 0 ]; then
    echo "Root privileges required. Using sudo..."
    SUDO="sudo"
else
    SUDO=""
fi

$SUDO apt-get update -qq

install_package() {
    local category="$1"
    shift
    
    for pkg in "$@"; do
        if apt-cache show "$pkg" &> /dev/null 2>&1; then
            echo "  Installing $pkg..."
            if DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends "$pkg" 2>&1 | grep -v "is already the newest version" || true; then
                return 0
            fi
        fi
    done
    
    echo "  Warning: No available package found for $category"
    return 1
}

echo ""
echo "Installing FFmpeg libraries..."
install_package "FFmpeg" libavcodec-dev libavcodec60 libavcodec59 libavcodec58
install_package "FFmpeg format" libavformat-dev libavformat60 libavformat59 libavformat58
install_package "FFmpeg util" libavutil-dev libavutil58 libavutil57 libavutil56
install_package "FFmpeg swresample" libswresample-dev libswresample4 libswresample3
install_package "FFmpeg swscale" libswscale-dev libswscale7 libswscale6 libswscale5

echo ""
echo "Installing Boost libraries..."
install_package "Boost program-options" libboost-program-options-dev libboost-program-options1.83.0 libboost-program-options1.81.0 libboost-program-options1.74.0

echo ""
echo "Installing Bullet Physics..."
install_package "Bullet" libbullet-dev libbullet3.24t64 libbullet3.24 libbullet3.17

echo ""
echo "Installing graphics libraries..."
install_package "OpenGL" libgl1 libgl1-mesa-glx libgl-dev
install_package "OpenSceneGraph" libopenscenegraph-dev libopenscenegraph161 libopenscenegraph3.6
install_package "OpenThreads" libopenthreads-dev libopenthreads21

echo ""
echo "Installing GUI libraries..."
install_package "SDL2" libsdl2-dev libsdl2-2.0-0

echo ""
echo "Installing audio libraries..."
install_package "OpenAL" libopenal-dev libopenal1

echo ""
echo "Installing system libraries..."
install_package "ICU" libicu-dev libicu74 libicu72 libicu70
install_package "LuaJIT" libluajit-5.1-dev libluajit-5.1-2 libluajit2-5.1-2
install_package "LZ4" liblz4-dev liblz4-1
install_package "SQLite" libsqlite3-dev libsqlite3-0
install_package "TinyXML" libtinyxml-dev libtinyxml2.6.2v5
install_package "TinyXML2" libtinyxml2-dev libtinyxml2.6.2v5 libtinyxml2-10 libtinyxml2-9
install_package "YAML-CPP" libyaml-cpp-dev libyaml-cpp0.8 libyaml-cpp0.7
install_package "zlib" zlib1g-dev zlib1g

echo ""
echo "Installing navigation library..."
install_package "RecastNavigation" librecast-dev librecast1

echo ""
echo "Verifying standard libraries..."
install_package "libc6" libc6
install_package "libgcc" libgcc-s1 libgcc1
install_package "libstdc++" libstdc++6

echo "All available dependencies have been installed."