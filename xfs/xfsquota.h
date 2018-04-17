#ifndef _XFSQUOTA_H
#define _XFSQUOTA_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __linux__
#include <linux/dqblk_xfs.h>
#include "contain.h"
struct quotactx;
/* fd should be the root of the filesystem */
int setup_quotactx(int fd, struct quotactx **ctx);
int get_quota(struct quotactx *ctx, struct container_handle *c, fs_disk_quota_t *d);
int free_quotactx(struct quotactx *ctx);

#else
struct quotactx;
typedef int fs_disk_quota_t;
static inline int setup_quotactx(int, struct quotactx**) { return -1; }
static inline int get_quota(struct quotactx*, struct container_handle*, fs_disk_quota_t*) { return -1; }
static inline int free_quotactx(struct quotactx*) { return -1; }

#endif
#ifdef __cplusplus
}
#endif
#endif
