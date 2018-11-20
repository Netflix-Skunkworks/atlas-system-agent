#!/bin/bash

set -e

RED='\033[0;31m' # Red
BB='\033[0;34m'  # Blue
NC='\033[0m' # No Color
BG='\033[0;32m' # Green

error() { >&2 echo -e "${RED}$1${NC}"; }
showinfo() { echo -e "${BG}$1${NC}"; }
workingprocess() { echo -e "${BB}$1${NC}"; }
alert () { echo -e "${RED}$1${NC}"; }

NATIVE_CLIENT_VERSION=master

# Fetch and build libatlasclient
rm -rf nc
mkdir nc
cd nc
git init
git remote add origin https://github.com/Netflix/spectator-cpp.git
git fetch origin $SPECTATOR_CPP_VERSION
git reset --hard FETCH_HEAD
mkdir -p build root
cd build
cmake -DCMAKE_INSTALL_PREFIX=/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j4
make install DESTDIR=../root
cd ../..

# Building project
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo $TITUS_AGENT ..

make -j4
# Checks if last comand didn't output 0
# $? checks what last command outputed
# If output is 0 then command is succesfuly executed
# If command fails it outputs number between 0 to 255
if [ $? -ne 0 ]; then
    error "Error: there are compile errors!"
	# Terminate script and outputs 3
    exit 3
fi

showinfo "Running tests ..."

./runtests

if [ $? -ne 0 ]; then
    error "Error: there are failed tests!"
    exit 4
fi

workingprocess "All tests compile and pass."

