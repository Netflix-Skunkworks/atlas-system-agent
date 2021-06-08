#pragma once

static constexpr const char* kNvidiaLib =

#if defined(__APPLE__)

    "libnvidia-ml.dylib";

#elif defined(__linux__)

    "libnvidia-ml.so.1";

#else

#error Unsupported OS

#endif
