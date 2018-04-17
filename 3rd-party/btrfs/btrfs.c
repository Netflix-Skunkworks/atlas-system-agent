#include "btrfs.h"
#include "qgroup.h"
#include "utils.h"
#include <sys/ioctl.h>

u64 btrfs_get_path_rootid(int fd) {
  int ret;
  struct btrfs_ioctl_ino_lookup_args args;

  memset(&args, 0, sizeof(args));
  args.objectid = BTRFS_FIRST_FREE_OBJECTID;

  ret = ioctl(fd, BTRFS_IOC_INO_LOOKUP, &args);
  if (ret < 0) {
    error("ERROR: can't perform the search - %s\n", strerror(errno));
    return ret;
  }
  return args.treeid;
}

int get_qgroups(int fd, struct btrfs_qgroup_info* out, size_t* size) {
  u64 qgroupid;
  int ret;

  struct btrfs_qgroup_comparer_set* comparer_set;
  struct btrfs_qgroup_filter_set* filter_set;
  filter_set = btrfs_qgroup_alloc_filter_set();
  comparer_set = btrfs_qgroup_alloc_comparer_set();

  btrfs_qgroup_setup_units(UNITS_DEFAULT);
  qgroupid = btrfs_get_path_rootid(fd);
  btrfs_qgroup_setup_filter(&filter_set, BTRFS_QGROUP_FILTER_PARENT, qgroupid);

  ret = btrfs_get_qgroups(fd, filter_set, comparer_set, out, size);
  if (ret < 0) {
    error("can't list qgroups: %s", strerror(errno));
  }

  return ret;
}
