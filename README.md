[![Build Status](https://travis-ci.org/Netflix-Skunkworks/atlas-system-agent.svg?branch=master)](https://travis-ci.org/Netflix-Skunkworks/atlas-system-agent)
# Atlas System Agent / Atlas Titus Agent

> :warning: Experimental

An agent that reports metrics for ec2 instances or titus containers.

## Build Instructions

* This build requires a C++17 compiler and uses [bazel](https://bazel.build/)
  as its build system. This has been tested with clang 11 and g++ 10

* [Install bazel](https://docs.bazel.build/versions/master/install.html)

* To build the titus agent:

```sh

bazel build --define titus_agent=yes atlas_system_agent sysagent_test

# Run tests
./bazel-bin/sysagent_test
```

* To build the system agent:

```sh

bazel build atlas_system_agent sysagent_test

# Run tests
./bazel-bin/sysagent_test
```

## Titus Agent

## CPU Metrics

### cgroup.cpu.processingCapacity

Amount of processing time requested for the container. This value is computed
based on the number of shares allocated when creating the job. Note that this
is not a hard limit, if there is no contention a job can use more than the
requested capacity. However, a user should not rely on getting more than requested.

**Unit:** seconds/second

### cgroup.cpu.processingTime

Amount of time spent processing code in the container. This metric would typically
get used for one of two use-cases:

1. _Utilization_: to see how close it is coming to saturating the requested resources
   for the job you can divide the processing time by the
   [processing capacity](#cgroupcpuprocessingcapacity).
2. _Performance Regression_: for comparative analysis the sum can be used. Note you should ensure
   that both systems being compared have the same amount of resources.

**Unit:** seconds/second

### cgroup.cpu.shares

Number of [shares](https://docs.docker.com/engine/reference/run/#/cpu-share-constraint)
configured for the job. The Titus scheduler treats each CPU core as 100 shares. Generally
the [processing capacity](#cgroupcpuprocessingcapacity) is more relevant to the user as
it has been normalized to the same unit as the measured processing time.

**Unit:** num shares

### cgroup.cpu.usageTime

Amount of time spent processing code in the container in either the system or user
category.

**Unit:** seconds/second

**Dimensions:**

* `id`: category of usage, either `system` or `user`

## Memory Metrics

### cgroup.mem.failures

Counter indicating an allocation failure occurred. Typically this will be seen when
the application hits the memory limit.

**Unit:** failures/second

### cgroup.mem.limit

Memory limit for the cgroup.

**Unit:** bytes

### cgroup.mem.used

Memory usage for the cgroup.

**Unit:** bytes

### cgroup.mem.pageFaults

> Description from [kernel.org](https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt)

Counter indicating the number of times that a process of the cgroup triggered
a "page fault" and a "major fault", respectively. A page fault happens when a
process accesses a part of its virtual memory space which is nonexistent or
protected. The former can happen if the process is buggy and tries to access
an invalid address (it will then be sent a `SIGSEGV` signal, typically killing
it with the famous `Segmentation fault` message). The latter can happen when the
process reads from a memory zone which has been swapped out, or which corresponds
to a mapped file: in that case, the kernel will load the page from disk, and let
the CPU complete the memory access. It can also happen when the process writes to
a copy-on-write memory zone: likewise, the kernel will preempt the process,
duplicate the memory page, and resume the write operation on the process' own copy
of the page. "Major" faults happen when the kernel actually has to read the data
from disk. When it just has to duplicate an existing page, or allocate an empty
page, it is a regular (or "minor") fault.

**Unit:** faults/second

**Dimensions:**

* `id`: either `minor` or `major`.

### cgroup.mem.processUsage

Amount of memory used by processes running in the cgroup.

**Unit:** bytes

**Dimensions:**

* `id`: how the processes are using the memory. Values are `cache`, `rss`, `rss_huge`,
  and `mapped_file`.
