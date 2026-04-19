# Atlas System Agent / Atlas Titus Agent

[![Build](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml/badge.svg)](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml)

An agent that reports metrics for EC2 instances or [Titus] containers.

[Titus]: https://github.com/Netflix/titus/

## Portability

The agent is distributed as a native binary, so the glibc version linked at
build time sets a floor on the glibc versions it can run against. Building on
an older Linux environment produces a binary that also runs on newer ones;
glibc's forward-compatible symbol versioning handles the rest. Building on a
newer environment produces a binary that will fail to start on older ones
with `version 'GLIBC_x.y' not found` errors.

If you are packaging for an environment with a specific glibc floor, two
opt-in knobs are available:

```shell
# Rebuild every Conan dependency (and its transitives) from source, so the
# final binary only references symbols available in the current build
# environment. Use this when Conan Center prebuilts target a newer glibc
# than your deployment environment provides.
BUILD_FROM_SOURCE=1 ./build.sh

# Fail the build if the final binary requires any GLIBC_x.y symbol newer
# than the declared threshold. Guards against regressions from future
# Conan dependency updates.
MAX_GLIBC=2.34 ./build.sh

# Both together for packaging pipelines targeting an older glibc:
BUILD_FROM_SOURCE=1 MAX_GLIBC=2.34 ./build.sh
```

The fast default (neither flag set) uses Conan Center prebuilts where
available and rebuilds only the few dependencies known to link against a
newer glibc (`m4`, `boost`, `elfutils`).

### Inspecting a binary manually

To see the highest `GLIBC_x.y` symbol a binary needs, and compare against the
glibc available on a host:

```shell
# highest required GLIBC symbol in the binary
objdump -T cmake-build/bin/atlas_system_agent \
  | grep -Eo 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -n1

# full list of required GLIBC / GLIBCXX / CXXABI versions
objdump -T cmake-build/bin/atlas_system_agent \
  | grep -Eo '(GLIBC|GLIBCXX|CXXABI)_\S+' | sort -uVr

# glibc version provided by the current host
ldd --version | head -n1
```

The binary will run on any host whose `ldd --version` is greater than or
equal to the highest required `GLIBC_x.y` it reports.

## Local & IDE Configuration

This agent was designed for Linux systems, and as a result, it does not compile cleanly on MacOS. It
is best to build on a Linux machine:

```shell
# setup python venv and activate, to gain access to conan cli
./setup-venv.sh
source venv/bin/activate

./build.sh  # [clean|clean --confirm|skiptest]
```

* Install the Conan plugin for CLion.
  * CLion > Settings > Plugins > Marketplace > Conan > Install
* Configure the Conan plugin.
  * The easiest way to configure CLion to work with Conan is to build the project first from the command line.
    * This will establish the `$PROJECT_HOME/CMakeUserPresets.json` file, which will allow you to choose the custom
    CMake configuration created by Conan when creating a new CMake project. Using this custom profile will ensure
    that sources are properly indexed and explorable.
  * Open the project. The wizard will show three CMake profiles.
    * Disable the default Cmake `Debug` profile.
    * Enable the CMake `conan-debug` profile.
  * CLion > View > Tool Windows > Conan > (gear) > Conan Executable: `$PROJECT_HOME/venv/bin/conan`

## Debugging

```
# attach gdb to the test process
gdb ./cmake-build/bin/sysagent_test

# set a break point at a specific line
b /home/nfsuper/atlas-system-agent/lib/cgroup_test.cpp:86

# enable the terminal ui, so you can see the source code as you step
tui enable

# run the program, with the debugger attached
run

# next line
n

# step into a function
s 
```
