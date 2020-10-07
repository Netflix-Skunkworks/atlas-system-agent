#!/bin/bash

set -e
RED='\033[0;31m' # Red
BB='\033[0;34m'  # Blue
NC='\033[0m' # No Color
BG='\033[0;32m' # Green

error() { >&2 echo -e "${RED}$1${NC}"; }
showinfo() { echo -e "${BG}$1${NC}"; }
workingprocess() { echo -e "${BB}$1${NC}"; }

alias bazel=bazel-3.5.0

if [ -z $TITUS_AGENT ]; then
  showinfo "Building atlas-system-agent"
  bazel --output_user_root=$HOME/.cache/bazel-a --batch build --config asan //... --verbose_failures
else
  showinfo "Building atlas-titus-agent"
  bazel --output_user_root=$HOME/.cache/bazel-t --batch build --config asan //... --define titus_agent=yes --verbose_failures
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

