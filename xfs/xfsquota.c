#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>

/* Kernel headers */
#include <linux/fs.h>

/* quota */
#include "xfsquota.h"
#include <linux/dqblk_xfs.h>
#include <linux/quota.h>
#include <sys/quota.h>
#include <limits.h>

/* formatting / error handling */
#include <string.h>
#include <errno.h>
#include <stdio.h>

/* fs ops */
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/types.h>

/* namespaces */
#include <sched.h>

/* waitpid */
#include <sys/wait.h>

/* Older versions of the Linux headers (pre-4.4) don't have these */

#ifndef XFS_IOC_FSGETXATTR
#define XFS_IOC_FSGETXATTR _IOR ('X', 31, struct fsxattr)
#endif

#ifndef PRJQUOTA
#define PRJQUOTA 2
#endif

#ifndef FS_XFLAG_PROJINHERIT
/*
 * http://elixir.free-electrons.com/linux/v4.13.2/source/include/uapi/linux/fs.h#L154
 */
/*
 * Structure for FS_IOC_FSGETXATTR[A] and FS_IOC_FSSETXATTR.
 */
struct fsxattr {
	__u32		fsx_xflags;	/* xflags field value (get/set) */
	__u32		fsx_extsize;	/* extsize field value (get/set)*/
	__u32		fsx_nextents;	/* nextents field value (get)	*/
	__u32		fsx_projid;	/* project identifier (get/set) */
	__u32		fsx_cowextsize;	/* CoW extsize field value (get/set)*/
	unsigned char	fsx_pad[8];
};

#endif

struct quotactx {
	dev_t dev;
	__u32 projid;
};

int setup_quotactx(int fd, struct quotactx **ctxptr) {
	char hiddenfile[PATH_MAX], blkdev[PATH_MAX], blkdev_proc[PATH_MAX];
	struct quotactx *ctx;
	struct fsxattr fsx;
	struct stat st;
	int fd2, err;

	memset(hiddenfile, 0, sizeof(hiddenfile));
	memset(blkdev, 0, sizeof(blkdev));
	memset(blkdev_proc, 0, sizeof(blkdev_proc));

	/* 0. Allocate memory */
	*ctxptr = malloc(sizeof(struct quotactx));
	ctx = *ctxptr;

	snprintf(hiddenfile, sizeof(hiddenfile), ".xfsquota-%d", rand());
	err = unlinkat(fd, hiddenfile, 0);
	if (err && errno != ENOENT) {
		fprintf(stderr, "Unable to remove existing hidden file: %s\n", strerror(errno));
		goto free_memory;
	}

	fd2 = openat(fd, hiddenfile, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd2 < 0) {
		fprintf(stderr, "Unable to make temporary file: %s\n", strerror(errno));
		goto free_memory;
	}

	if (fstat(fd2, &st)) {
		fprintf(stderr, "Unable to fstat hidden file: %s\n", strerror(errno));
		goto unlink_hiddenfile;
	}
	ctx->dev = st.st_dev;

	if (ioctl(fd2, XFS_IOC_FSGETXATTR, &fsx)) {
		fprintf(stderr, "Could not get fsxattr (project id): %s\n", strerror(errno));
		goto unlink_hiddenfile;
	}
	ctx->projid = fsx.fsx_projid;

	if (unlinkat(fd, hiddenfile, 0)) {
		fprintf(stderr, "Unable to remove hidden file: %s\n", strerror(errno));
		goto free_memory;
	}
	close(fd2);

	return 0;

unlink_hiddenfile:
	if (unlinkat(fd, hiddenfile, 0))
		fprintf(stderr, "Unable to remove hidden file: %s\n", strerror(errno));
	close(fd2);
free_memory:
	free(ctx);
	return 1;
}

static int do_child(struct quotactx *ctx, struct container_handle *c, fs_disk_quota_t *d) {
	char blkdev[PATH_MAX];
	int ret = 0;

	/* Terminate in 10 seconds if we can't finish */
	alarm(10);
	if (seteuid(0)) {
		fprintf(stderr, "Unable to set effective UID to 0: %s\n", strerror(errno));
		return 1;
	}
	if (setegid(0)) {
		fprintf(stderr, "Unable to set effective GID to 0: %s\n", strerror(errno));
		return 1;
	}

	snprintf(blkdev, sizeof(blkdev), "/tmp/blkdev-%d", rand());
	if (setns(c->mount_ns_fd, CLONE_NEWNS)) {
		fprintf(stderr, "Unable to set mount ns fd: %s\n", strerror(errno));
		return 1;
	}

	if (mknod(blkdev, S_IFBLK | 0700, ctx->dev)) {
		fprintf(stderr, "Unable to make blockdev: %s\n", strerror(errno));
		return 1;
	}

	/* This is a best effort */
	quotactl(QCMD(Q_XQUOTASYNC, PRJQUOTA), blkdev, 0, NULL);

	if (quotactl(QCMD(Q_XGETQUOTA, PRJQUOTA), blkdev, ctx->projid, (caddr_t)d)) {
		fprintf(stderr, "Cannot get quota: %s\n", strerror(errno));
		ret = 1;
	}
	/* We don't really care if this fails */
	unlink(blkdev);

	return ret;
}

/*
 * This is to get around GCC's clobbered variable detection. It doesn't know that the
 * function can't return until the vfork returns -- and the vfork sets the ret value
 */
static int do_vfork(struct quotactx *ctx, struct container_handle *c, fs_disk_quota_t *d) __attribute__((returns_twice));
static int do_vfork(struct quotactx *ctx, struct container_handle *c, fs_disk_quota_t *d) {
	int status, ret = 0;
	pid_t p, p2;

	p = vfork();
	if (p == 0) {
		ret = do_child(ctx, c, d);
		_exit(0);
	}
	/* We have to collect our child */
	p2 = waitpid(p, &status, 0);
	if (p2 == -1)
		fprintf(stderr, "Unable to get xfs vfork child status: %s\n", strerror(errno));
	else if (p2 > 0) {
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			fprintf(stderr, "Child terminated abnormally, with exit status: %d\n", WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			fprintf(stderr, "Child terminated due to signal: %s\n", strsignal(WTERMSIG(status)));
	}

	return ret;
}

int get_quota(struct quotactx *ctx, struct container_handle *c, fs_disk_quota_t *d) {
	memset(d, 0, sizeof(*d));
	if (!c) {
		fprintf(stderr, "Mount NS FD unset\n");
		return 1;
	}

	return do_vfork(ctx, c, d);
}

int free_quotactx(struct quotactx *ctx) {
	free(ctx);

	return 0;
}


