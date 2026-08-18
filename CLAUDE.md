# atlas-system-agent

## Never build or compile this project on this machine

This repo is developed on macOS, but the actual build requires gcc-15 on Linux plus a full Conan
dependency fetch (including vendored AMD ROCm). It does not work on macOS, and even where a
build step might nominally run, it is too heavy/slow to be worth attempting here.

Do not run any of the following (directly, via a script, or from within a subagent/workflow
prompt): `cmake`, `make`, `ctest`, `conan install`, `conan create`, `conan build`. Read-only Conan
inspection (`conan graph *`, `conan list *`) is fine.

Verify C++ changes by reading the code carefully, cross-checking against this codebase's existing
conventions and tests, and tracing logic by hand. When a real compile/test run is genuinely
needed, that has to happen on an actual Linux box (e.g. a NOP cluster node over SSH), not here —
ask before attempting anything like that.
