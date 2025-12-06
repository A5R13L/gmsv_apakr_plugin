if [ -n "$1" ]; then
    PRESET="$1"
else
    ARCH=$(uname -m)

    if [[ "$ARCH" == "x86_64" ]]; then
        PRESET="x86_64"
    else
        PRESET="x86"
    fi
fi

if [[ "$PRESET" == "x86_64" ]]; then
    CONFIG="releasewithsymbols_x86_64"
    CXXFLAGS="-std=c++20"
    BUILD_PATH="projects/x64/linux/gmake2"
    ARTIFACT_PATH="projects/x64/linux/gmake2/x86_64/ReleaseWithSymbols/gmsv_apakr_64_linux64.dll"
    ARTIFACT_NAME="gmsv_apakr_plugin_64.so"
else
    CONFIG="releasewithsymbols_x86"
    CXXFLAGS="-std=c++17"
    BUILD_PATH="projects/x32/linux/gmake2"
    ARTIFACT_PATH="projects/x32/linux/gmake2/x86/ReleaseWithSymbols/gmsv_apakr_32_linux.dll"
    ARTIFACT_NAME="gmsv_apakr_plugin_32.so"
fi

if [[ "$ARCH" == "x86" ]]; then
    dpkg --add-architecture i386
fi

apt-get update && apt-get install -y build-essential ninja-build curl zip unzip tar pkg-config wget gcc-multilib g++-multilib libcurl4-openssl-dev cmake make
premake5 gmake2
cd $BUILD_PATH
make config=$CONFIG CXX=g++-10 CXXFLAGS="$CXXFLAGS" LDFLAGS="-lpthread -lcurl"