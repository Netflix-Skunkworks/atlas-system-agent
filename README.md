[![Build Status](https://travis-ci.com/Netflix-Skunkworks/atlas-system-agent.svg?branch=master)](https://travis-ci.com/Netflix-Skunkworks/atlas-system-agent)
# Atlas System Agent / Atlas Titus Agent

> :warning: Experimental

An agent that reports metrics for EC2 instances or [Titus] containers.

[Titus]: https://github.com/Netflix/titus/

## Local Development

### Builds

* If you are running Docker Desktop, then allocate 8GB RAM to allow builds to succeed.
* Set the `BASEOS_IMAGE` environment variable to a reasonable value, such as `ubuntu:bionic`.
* Run the `atlas-system-agent` build: `./build.sh`
* Run the `atlas-titus-agent` build: `./build.sh titus`
* Start an interactive shell in the source directory: `./build.sh shell`

### CLion

* Use JetBrains Toolbox to install version 2020.1.3 (latest is >= 2020.3.1).
* The older version of CLion is required to gain access to the [Bazel plugin] released by Google.
* You can build the Bazel plugin from source, to get the latest, which may fix more issues.
    ```
    git clone https://github.com/bazelbuild/intellij.git
    git checkout v2021.01.05
    bazel build //clwb:clwb_bazel_zip --define=ij_product=clion-beta
    bazel-bin/clwb/clwb_bazel.zip
    ```
* When loading a new project, use the `Import Bazel Project` from the `BUILD file` feature.
* If you need to remove the latest version of CLion and install an older one, disable JetBrains
settings sync and clear out all CLion locally cached data.
    ```
    rm -rf ~/Library/Application Support/CLion
    rm -rf ~/Library/Application Support/JetBrains/CLion*
    rm -rf $WORKSPACE/.idea
    ```

[Bazel plugin]: https://plugins.jetbrains.com/plugin/9554-bazel/versions
