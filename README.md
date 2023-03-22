# Atlas System Agent / Atlas Titus Agent

[![Build](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml/badge.svg)](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml)

An agent that reports metrics for EC2 instances or [Titus] containers.

[Titus]: https://github.com/Netflix/titus/

## Local Development

Due to the nature of what this agent is, it does not compile cleanly on MacOS. It is best to build on a Linux
machine:

```shell
./setup-venv.sh
source venv/bin/activate
./build.sh  # [clean|skiptest]
```

* CLion version 2022.1.3 required until the Conan plugin is updated
* CLion > Preferences > Plugins > Marketplace > Conan > Install
* CLion > Preferences > Build, Execution, Deploy > Conan > Conan Executable: $PROJECT_HOME/venv/bin/conan
* CLion > Bottom Bar: Conan > Left Button: Match Profile > CMake Profile: Debug, Conan Profile: default
