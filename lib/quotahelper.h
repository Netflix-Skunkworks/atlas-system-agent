#pragma once

#include "files.h"
#include <xfsquota.h>
#include <contain.h>

namespace atlasagent {

class QuotaHelper {
 public:
  explicit QuotaHelper(const char* mount_point) : fd_(UnixFile(mount_point)), state_(nullptr) {
    if (fd_ >= 0) {
      setup_quotactx(fd_, &state_);
    }
  }

  ~QuotaHelper() {
    if (fd_ >= 0) {
      free_quotactx(state_);
    }
  }

  bool get(container_handle* handle, fs_disk_quota_t* quota) {
    if (state_ == nullptr) {
      return true;
    }

    auto err = get_quota(state_, handle, quota);
    if (err) {
      Logger()->warn("Unable to populate quota");
    }
    return err != 0;
  }

 private:
  UnixFile fd_;
  quotactx* state_;
};
}  // namespace atlasagent
