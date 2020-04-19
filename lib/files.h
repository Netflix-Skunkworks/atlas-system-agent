#pragma once

#include "logger.h"
#include <cstdio>
#include <dirent.h>

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
  explicit UnixFile(int fd) : fd_(fd) {}

  explicit UnixFile(const char* name) noexcept : fd_(-1) { open(name); }

  UnixFile(const UnixFile&) = delete;
  UnixFile(UnixFile&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  void open(const char* name) {
    if (fd_ >= 0) {
      close(fd_);
    }

    fd_ = ::open(name, O_RDONLY);
    if (fd_ < 0) {
      Logger()->warn("Unable to open {}", name);
    }
  }

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

class DirHandle {
 public:
  explicit DirHandle(const char* name) noexcept : dh_{opendir(name)} {
    if (dh_ == nullptr) {
      Logger()->warn("Unable to opendir {}: {}", name, strerror(errno));
    }
  }
  DirHandle(const DirHandle&) = delete;
  ~DirHandle() {
    if (dh_ != nullptr) {
      closedir(dh_);
    }
  }

  operator DIR*() const { return dh_; }

 private:
  DIR* dh_;
};
}  // namespace atlasagent
