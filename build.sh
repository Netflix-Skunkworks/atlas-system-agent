#!/usr/bin/env bash

set -e

# usage: ./build.sh [clean|clean --confirm|skiptest]

if [[ -z "$BUILD_DIR" ]]; then
  BUILD_DIR="cmake-build"
fi

if [[ -z "$BUILD_TYPE" ]]; then
  # Choose: Debug, Release, RelWithDebInfo and MinSizeRel. Use Debug for asan checking locally.
  BUILD_TYPE="Debug"
fi

BLUE="\033[0;34m"
NC="\033[0m"

if [[ "$1" == "clean" ]]; then
  echo -e "${BLUE}==== clean ====${NC}"
  rm -rf "$BUILD_DIR"
  rm -rf lib/spectator
  if [[ "$2" == "--confirm" ]]; then
    # remove all packages from the conan cache, to allow swapping between Release/Debug builds
    conan remove "*" --confirm
  fi
fi

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  source /etc/os-release
  if [[ "$NAME" == "Ubuntu" ]]; then
    if [[ -z "$CC" ]]; then export CC=gcc-13; fi
    if [[ -z "$CXX" ]]; then export CXX=g++-13; fi
  fi
fi

if [[ ! -f "$HOME/.conan2/profiles/default" ]]; then
  echo -e "${BLUE}==== create default profile ====${NC}"
  conan profile detect
fi

if [[ ! -d $BUILD_DIR ]]; then
  echo -e "${BLUE}==== install required dependencies ====${NC}"
  if [[ "$BUILD_TYPE" == "Debug" ]]; then
    conan install . --output-folder="$BUILD_DIR" --build="*" --settings=build_type="$BUILD_TYPE" --profile=./sanitized
  else
    conan install . --output-folder="$BUILD_DIR" --build=missing
  fi

  echo -e "${BLUE}==== install source dependencies ====${NC}"
  conan source .
fi

pushd "$BUILD_DIR"

echo -e "${BLUE}==== configure conan environment to access tools ====${NC}"
source conanbuild.sh

if [[ $OSTYPE == "darwin"* ]]; then
  export MallocNanoZone=0
fi

echo -e "${BLUE}==== generate build files ====${NC}"
if [[ "$TITUS_SYSTEM_SERVICE" != "ON" ]]; then
  TITUS_SYSTEM_SERVICE=OFF
fi
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DTITUS_SYSTEM_SERVICE="$TITUS_SYSTEM_SERVICE"

echo -e "${BLUE}==== build ====${NC}"
cmake --build .

if [[ "$1" != "skiptest" ]]; then
  echo -e "${BLUE}==== test ====${NC}"
  GTEST_COLOR=1 ctest --verbose
fi

popd
