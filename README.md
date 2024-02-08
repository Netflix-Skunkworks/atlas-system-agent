# Atlas System Agent / Atlas Titus Agent

[![Build](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml/badge.svg)](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml)

An agent that reports metrics for EC2 instances or [Titus] containers.

[Titus]: https://github.com/Netflix/titus/

## Local Development

This agent was designed for Linux systems, and as a result, it does not compile cleanly on macOS. It
is best to build on a Linux machine:

```shell
# setup python venv and activate, to gain access to conan cli
./setup-venv.sh
source venv/bin/activate

# link clion default build directory to our build directory
ln -s cmake-build cmake-build-debug

./build.sh  # [clean|clean --force|skiptest]
```

* CLion > Preferences > Plugins > Marketplace > Conan > Install
* CLion > Preferences > Build, Execution, Deploy > Conan > Conan Executable: $PROJECT_HOME/venv/bin/conan

## Debugging

```
# attach gdb to the test process
gdb ./cmake-build/bin/sysagent_test

# set a break point at a specific line
b /home/nfsuper/atlas-system-agent/lib/cgroup_test.cc:86

# enable the terminal ui, so you can see the source code as you step
tui enable

# run the program, with the debugger attached
run

# next line
n

# step into a function
s 
```
