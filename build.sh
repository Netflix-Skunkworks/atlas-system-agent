#!/bin/bash

ATLAS_SYSTEM_AGENT_IMAGE="atlas-system-agent/builder:latest"
ATLAS_SYSTEM_AGENT_IMAGE_ID=$(docker images --quiet $ATLAS_SYSTEM_AGENT_IMAGE)

if [[ -z "$ATLAS_SYSTEM_AGENT_IMAGE_ID" ]]; then
  if [[ -z "$BASEOS_IMAGE" ]]; then
    echo "set BASEOS_IMAGE to a reasonable value, such as ubuntu:bionic" && exit 1
  fi

  sed -i -e "s,BASEOS_IMAGE,$BASEOS_IMAGE,g" Dockerfile
  docker build --tag $ATLAS_SYSTEM_AGENT_IMAGE . || exit 1
  git checkout Dockerfile
else
  echo "using image $ATLAS_SYSTEM_AGENT_IMAGE $ATLAS_SYSTEM_AGENT_IMAGE_ID"
fi

# option to start an interactive shell in the source directory
if [[ "$1" == "shell" ]]; then
  docker run --rm --interactive --tty --mount type=bind,source="$(pwd)",target=/src --workdir /src $ATLAS_SYSTEM_AGENT_IMAGE /bin/bash
  exit 0
fi

# option to activate atlas-titus-agent code paths in the build
if [[ "$1" == "titus" ]]; then
  ATLAS_TITUS_AGENT="--define titus_agent=yes"
  BINARY="atlas-titus-agent"
  CACHE=".cache-t"
else
  ATLAS_TITUS_AGENT=""
  BINARY="atlas-system-agent"
  CACHE=".cache-a"
fi

# recommend 8GB RAM allocation for docker desktop, to allow the test build with asan to succeed
cat >start-build <<EOF
echo "-- build tests with address sanitizer enabled"
bazel --output_user_root=$CACHE build --config=asan sysagent_test $ATLAS_TITUS_AGENT

echo "-- run tests"
bazel-bin/sysagent_test

echo "-- build optimized daemon"
bazel --output_user_root=$CACHE build --compilation_mode=opt atlas_system_agent $ATLAS_TITUS_AGENT

echo "-- check shared library dependencies"
ldd bazel-bin/atlas_system_agent || true

echo "-- copy binary to local filesystem"
rm -f $BINARY
cp -p bazel-bin/atlas_system_agent $BINARY
EOF

chmod 755 start-build

docker run --rm --tty --mount type=bind,source="$(pwd)",target=/src $ATLAS_SYSTEM_AGENT_IMAGE /bin/bash -c "cd src && ./start-build"

rm start-build

# adjust symlinks to point to the local .cache directory
for link in bazel-*; do
  ln -nsf "$(readlink "$link" |sed -e "s:^/src/::g")" "$link"
done
