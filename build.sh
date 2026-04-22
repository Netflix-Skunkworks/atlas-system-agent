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
  if [[ "$BUILD_TYPE" == "Debug" || "$BUILD_FROM_SOURCE" == "1" ]]; then
    # Build every dependency (and its transitive deps) from source. Use this
    # path when targeting an environment with an older glibc than the Conan
    # Center prebuilts were compiled against; rebuilding everything locally
    # guarantees the final binary only references symbols available in the
    # current build environment.
    conan install . --output-folder="$BUILD_DIR" --build="*" --settings=build_type="$BUILD_TYPE"
  else
    # Fast default. Force m4 to build from source; its Conan Center prebuilt
    # is linked against a newer glibc than some build environments provide
    # (e.g. older base images), so rebuilding it locally avoids runtime
    # errors during the subsequent autotools-based package builds.
    #
    # Broader rebuilds (boost, elfutils, transitive deps like b2) are
    # opt-in via BUILD_FROM_SOURCE=1. Forcing them here would drag in from-
    # source builds of b2, whose Conan Center prebuilt can't run on older
    # glibc either, regressing older-base-image consumers.
    conan install . --output-folder="$BUILD_DIR" --build=missing --build=m4/*
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

# Optional portability gate. When MAX_GLIBC is set, fail the build if the
# binary references a GLIBC symbol newer than the declared threshold. This
# catches regressions where a rebuilt (or newly added) dependency pulls in
# prebuilt artifacts compiled against a newer glibc than the target
# environment provides. Example: MAX_GLIBC=2.34 ./build.sh
if [[ -n "$MAX_GLIBC" ]]; then
  BIN="$BUILD_DIR/bin/atlas_system_agent"
  echo -e "${BLUE}==== check required GLIBC symbols against MAX_GLIBC=$MAX_GLIBC ====${NC}"
  MAX_REQUIRED=$(objdump -T "$BIN" | grep -Eo 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -n1)
  if [[ -z "$MAX_REQUIRED" ]]; then
    echo "warning: no GLIBC symbols found in $BIN; skipping check"
  elif [[ "$(printf '%s\n' "GLIBC_$MAX_GLIBC" "$MAX_REQUIRED" | sort -V | tail -n1)" != "GLIBC_$MAX_GLIBC" ]]; then
    echo "ERROR: $BIN requires $MAX_REQUIRED, exceeds MAX_GLIBC=$MAX_GLIBC"
    objdump -T "$BIN" | grep -Eo 'GLIBC_\S+' | sort -uVr | head
    exit 1
  else
    echo "OK: highest required symbol is $MAX_REQUIRED (<= GLIBC_$MAX_GLIBC)"
  fi
fi
