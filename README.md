# Atlas System Agent / Atlas Titus Agent

[![Build](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml/badge.svg)](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml)

An agent that reports metrics for EC2 instances or [Titus] containers.

[Titus]: https://github.com/Netflix/titus/

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
