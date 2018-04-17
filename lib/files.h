#pragma once

#include "logger.h"
#include <cstdio>

namespace atlasagent {
class StdIoFile {
 public:
  explicit StdIoFile(const char* name) noexcept : fp_(std::fopen(name, "r")) {
    if (fp_ == nullptr) {
      Logger()->warn("Unable to open {}", name);
    } else {
      setbuffer(fp_, buf, sizeof buf);
    }
  }
  StdIoFile(const StdIoFile&) = delete;
  StdIoFile(StdIoFile&& other) noexcept : fp_(other.fp_) { other.fp_ = nullptr; }

  ~StdIoFile() {
    if (fp_ != nullptr) {
      std::fclose(fp_);
      fp_ = nullptr;
    }
  }

  operator FILE*() const { return fp_; }

 private:
  FILE* fp_;
  char buf[65536];
};

class UnixFile {
 public:
  explicit UnixFile(const char* name) noexcept : fd_(open(name, O_RDONLY)) {
    if (fd_ < 0) {
      Logger()->warn("Unable to open {}", name);
    }
  }
  UnixFile(const UnixFile&) = delete;
  UnixFile(UnixFile&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  ~UnixFile() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  operator int() const { return fd_; }

 private:
  int fd_;
};
}  // namespace atlasagent
