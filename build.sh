#!/usr/bin/env bash

# usage: ./build.sh [clean|clean --force|skiptest]

BUILD_DIR=cmake-build
# Choose: Debug, Release, RelWithDebInfo and MinSizeRel
BUILD_TYPE=Debug

BLUE="\033[0;34m"
NC="\033[0m"

if [[ "$1" == "clean" ]]; then
  echo -e "${BLUE}==== clean ====${NC}"
  rm -rf $BUILD_DIR
  rm -f spectator-cpp-*.zip
  rm -rf lib/spectator
  if [[ "$2" == "--force" ]]; then
    # remove all packages and binaries from the local cache, to allow swapping between Debug/Release builds
    conan remove '*' --force
  fi
fi

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  export CC=gcc-11
  export CXX=g++-11
fi

if [[ ! -d $BUILD_DIR ]]; then
  if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo -e "${BLUE}==== configure default profile ====${NC}"
    conan profile new default --detect
    conan profile update settings.compiler.libcxx=libstdc++11 default
  fi

  echo -e "${BLUE}==== install required dependencies ====${NC}"
  if [[ "$BUILD_TYPE" == "Debug" ]]; then
    conan install . --build --install-folder $BUILD_DIR --profile ./sanitized
  else
    conan install . --build=missing --install-folder $BUILD_DIR
  fi

  echo -e "${BLUE}==== install source dependencies ====${NC}"
  conan source .
fi

pushd $BUILD_DIR || exit 1

echo -e "${BLUE}==== generate build files ====${NC}"
if [[ "$TITUS_SYSTEM_SERVICE" == "ON" ]]; then
  TITUS_SYSTEM_SERVICE="-DTITUS_SYSTEM_SERVICE=ON"
else
  TITUS_SYSTEM_SERVICE="-DTITUS_SYSTEM_SERVICE=OFF"
fi
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $TITUS_SYSTEM_SERVICE .. || exit 1

echo -e "${BLUE}==== build ====${NC}"
cmake --build . || exit 1

if [[ "$1" != "skiptest" ]]; then
  echo -e "${BLUE}==== test ====${NC}"
  GTEST_COLOR=1 ctest --verbose
fi

popd || exit 1
