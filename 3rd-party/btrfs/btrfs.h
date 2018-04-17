#pragma once

#include "qgroup_info.h"

#ifdef __cplusplus
extern "C" {
#endif

int get_qgroups(int fd, struct btrfs_qgroup_info* out, size_t* size);

#ifdef __cplusplus
}
#endif
