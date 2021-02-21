#!/bin/bash

set -e

BLUE='\033[0;34m'
GREEN='\033[0;32m'
NO_COLOR='\033[0m'
RED='\033[0;31m'

error() { >&2 echo -e "${RED}$1${NO_COLOR}"; }
info() { echo -e "${GREEN}$1${NO_COLOR}"; }
complete() { echo -e "${BLUE}$1${NO_COLOR}"; }

if [[ -z "$TITUS_AGENT" ]]; then
  info "-- build atlas-system-agent"
  bazel --output_user_root="$HOME"/.cache/bazel-a --batch build --config asan //... --verbose_failures
else
  info "-- build atlas-titus-agent"
  bazel --output_user_root="$HOME"/.cache/bazel-t --batch build --config asan //... --define titus_agent=yes --verbose_failures
fi

if [[ $? -ne 0 ]]; then
    error "-- ERROR: build failed"
    exit 3
fi

info "-- run tests"
./bazel-bin/sysagent_test

if [[ $? -ne 0 ]]; then
    error "ERROR: tests failed"
    exit 4
fi

complete "All tests compile and pass."
