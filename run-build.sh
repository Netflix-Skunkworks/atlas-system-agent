#!/bin/bash

set -e
RED='\033[0;31m' # Red
BB='\033[0;34m'  # Blue
NC='\033[0m' # No Color
BG='\033[0;32m' # Green

error() { >&2 echo -e "${RED}$1${NC}"; }
showinfo() { echo -e "${BG}$1${NC}"; }
workingprocess() { echo -e "${BB}$1${NC}"; }


showinfo "Installing bazel"
curl -fsSL https://bazel.build/bazel-release.pub.gpg | sudo apt-key add -
echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list

sudo apt update && sudo apt install bazel-3.5.0
alias bazel=bazel-3.5.0

if [ -z $TITUS_AGENT ]; then
  showinfo "Building atlas-system-agent"
  bazel build --config asan //...
else
  showinfo "Building atlas-titus-agent"
  bazel build --config asan //... --define titus_agent=yes
fi

# If command fails it outputs number between 0 to 255
if [ $? -ne 0 ]; then
    error "Error: there are compile errors!"
	# Terminate script and outputs 3
    exit 3
fi

showinfo "Running tests ..."
./bazel-bin/sysagent_test


if [ $? -ne 0 ]; then
    error "Error: there are failed tests!"
    exit 4
fi

workingprocess "All tests compile and pass."

