if [ -n "$1" ]; then
    PRESET="$1"
else
    ARCH=$(uname -m)

    if [ "$ARCH" = "x86_64" ]; then
        PRESET="x86_64"
    else
        PRESET="x86"
    fi
fi

if [ "$PRESET" = "x86_64" ]; then
    CONFIG="releasewithsymbols_x86_64"
    CXXFLAGS="-std=c++20"
    BUILD_PATH="projects/apakr_64/linux/gmake2"
    ARTIFACT_PATH="projects/apakr_64/linux/gmake2/x86_64/ReleaseWithSymbols/gmsv_apakr_64_linux64.dll"
    ARTIFACT_NAME="gmsv_apakr_plugin_64.so"
else
    CONFIG="releasewithsymbols_x86"
    CXXFLAGS="-std=c++17"
    BUILD_PATH="projects/apakr_32/linux/gmake2"
    ARTIFACT_PATH="projects/apakr_32/linux/gmake2/x86/ReleaseWithSymbols/gmsv_apakr_32_linux.dll"
    ARTIFACT_NAME="gmsv_apakr_plugin_32.so"
fi

apt-get update
dpkg --add-architecture i386
apt-get update

apt-get install -y \
  build-essential \
  ninja-build \
  zip \
  unzip \
  tar \
  pkg-config \
  wget \
  gcc-multilib \
  g++-multilib \
  cmake \
  make \
  curl \
  libcurl4-openssl-dev \
  libcurl4-openssl-dev:i386

wget https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-linux.tar.gz
tar xf premake-5.0.0-beta2-linux.tar.gz
mv premake5 /usr/local/bin/
premake5 gmake2
cd $BUILD_PATH
make config=$CONFIG CXX=g++-10 CXXFLAGS="$CXXFLAGS -I/usr/include -I/usr/include/x86_64-linux-gnu -I/usr/include/i386-linux-gnu"