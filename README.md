# Atlas System Agent / Atlas Titus Agent / Atlas K8s Agent

[![Build](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml/badge.svg)](https://github.com/Netflix-Skunkworks/atlas-system-agent/actions/workflows/build.yml)

An agent that reports metrics for EC2 instances, [Titus] containers, or
[Kubernetes] containers.

The build produces one of three mutually-exclusive flavors, selected at compile
time via the `AGENT_FLAVOR` environment variable (`system` (default), `titus`, or
`k8s`) consumed by `build.sh`.

[Titus]: https://github.com/Netflix/titus/
[Kubernetes]: https://kubernetes.io/

## Local & IDE Configuration

This agent was designed for Linux systems, and as a result, it does not compile cleanly on MacOS. It
is best to build on a Linux machine:

```shell
# required to build amd metrics
sudo apt install libdrm-dev
sudo apt install pkg-config

# setup python venv and activate, to gain access to conan cli
./setup-venv.sh
source venv/bin/activate

./build.sh  # [clean|clean --confirm|skiptest]

# build a specific flavor (default is system)
AGENT_FLAVOR=titus ./build.sh
AGENT_FLAVOR=k8s ./build.sh
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

## NVIDIA GPU utilization averages

The system and Titus flavors publish `gpu.utilizationAvg` once per approximately
60-second collection interval, tagged with `provider=nvidia` and `gpu=<index>`.
It is a percentage (0-100), computed as the sum of all newly retrieved GPU
utilization samples divided by their count across the interval. Poll averages
are not weighted equally: a poll with more samples contributes more weight.
This is an interval mean, not an exponential moving average or a rolling window.
The existing `gpu.utilization` and `gpu.memoryActivity` metrics are unchanged.

A dedicated, shutdown-aware worker drains
[`nvmlDeviceGetSamples(NVML_GPU_UTILIZATION_SAMPLES)`](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html)
every five seconds, independently of slow metric collectors. The minute
publisher also drains any remaining samples before publishing. Each GPU keeps
its own exclusive last-seen timestamp. The first successful poll establishes a
baseline without counting retained history from before collection started.

An interval with no new samples emits no average, rather than zero or the
previous average. Unsupported sampling (including drivers without the symbol)
disables this new metric for that device; legacy GPU collection continues.
Transient failures retain the cursor so subsequent polls can recover samples
still in the driver ring. The cadence is a scheduling target, not a real-time
guarantee: a process suspension, slow NVML call, or extended failure can exceed
the driver's roughly 14-second retention. Overwritten samples cannot be
recovered; the mean then covers only the samples actually retrieved, not an
inferred full minute. Validate sampling support and cadence on the target GPU
and driver before relying on this metric.

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
