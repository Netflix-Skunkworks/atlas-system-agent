/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2008 Morey Roof.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <fcntl.h>
#include <unistd.h>
#include <mntent.h>
#include <ctype.h>
#include <linux/loop.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <limits.h>
#include <blkid/blkid.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <linux/magic.h>
#include <getopt.h>

#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "crc32c.h"
#include "utils.h"
#include "volumes.h"
#include "ioctl.h"

#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12,119)
#endif

static int btrfs_scan_done = 0;

static char argv0_buf[ARGV0_BUF_SIZE] = "btrfs";

static int rand_seed_initlized = 0;
static unsigned short rand_seed[3];

/*
 * Note: this function uses a static per-thread buffer. Do not call this
 * function more than 10 times within one argument list!
 */
const char *pretty_size_mode(u64 size, unsigned mode)
{
	static __thread int ps_index = 0;
	static __thread char ps_array[10][32];
	char *ret;

	ret = ps_array[ps_index];
	ps_index++;
	ps_index %= 10;
	(void)pretty_size_snprintf(size, ret, 32, mode);

	return ret;
}


/*
 * Test if path is a subvolume
 * Returns:
 *   0 - path exists but it is not a subvolume
 *   1 - path exists and it is  a subvolume
 * < 0 - error
 */
int test_issubvolume(const char *path)
{
	struct stat	st;
	struct statfs stfs;
	int		res;

	res = stat(path, &st);
	if (res < 0)
		return -errno;

	if (st.st_ino != BTRFS_FIRST_FREE_OBJECTID || !S_ISDIR(st.st_mode))
		return 0;

	res = statfs(path, &stfs);
	if (res < 0)
		return -errno;

	return (int)stfs.f_type == BTRFS_SUPER_MAGIC;
}

u64 parse_qgroupid(const char *p)
{
	char *s = strchr(p, '/');
	const char *ptr_src_end = p + strlen(p);
	char *ptr_parse_end = NULL;
	u64 level;
	u64 id;
	int fd;
	int ret = 0;

	if (p[0] == '/')
		goto path;

	/* Numeric format like '0/257' is the primary case */
	if (!s) {
		id = strtoull(p, &ptr_parse_end, 10);
		if (ptr_parse_end != ptr_src_end)
			goto path;
		return id;
	}
	level = strtoull(p, &ptr_parse_end, 10);
	if (ptr_parse_end != s)
		goto path;

	id = strtoull(s + 1, &ptr_parse_end, 10);
	if (ptr_parse_end != ptr_src_end)
		goto  path;

	return (level << BTRFS_QGROUP_LEVEL_SHIFT) | id;

path:
	/* Path format like subv at 'my_subvol' is the fallback case */
	ret = test_issubvolume(p);
	if (ret < 0 || !ret)
		goto err;
	fd = open(p, O_RDONLY);
	if (fd < 0)
		goto err;
	ret = lookup_ino_rootid(fd, &id);
	if (ret)
		error("failed to lookup root id: %s", strerror(-ret));
	close(fd);
	if (ret < 0)
		goto err;
	return id;

err:
	error("invalid qgroupid or subvolume path: %s", p);
	exit(-1);
}

static const char* unit_suffix_binary[] =
	{ "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
static const char* unit_suffix_decimal[] =
	{ "B", "kB", "MB", "GB", "TB", "PB", "EB"};

int pretty_size_snprintf(u64 size, char *str, size_t str_size, unsigned unit_mode)
{
	int num_divs;
	float fraction;
	u64 base = 0;
	int mult = 0;
	const char** suffix = NULL;
	u64 last_size;

	if (str_size == 0)
		return 0;

	if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_RAW) {
		snprintf(str, str_size, "%llu", size);
		return 0;
	}

	if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_BINARY) {
		base = 1024;
		mult = 1024;
		suffix = unit_suffix_binary;
	} else if ((unit_mode & ~UNITS_MODE_MASK) == UNITS_DECIMAL) {
		base = 1000;
		mult = 1000;
		suffix = unit_suffix_decimal;
	}

	/* Unknown mode */
	if (!base) {
		fprintf(stderr, "INTERNAL ERROR: unknown unit base, mode %d\n",
				unit_mode);
		assert(0);
		return -1;
	}

	num_divs = 0;
	last_size = size;
	switch (unit_mode & UNITS_MODE_MASK) {
	case UNITS_TBYTES: base *= mult; num_divs++;
	case UNITS_GBYTES: base *= mult; num_divs++;
	case UNITS_MBYTES: base *= mult; num_divs++;
	case UNITS_KBYTES: num_divs++;
			   break;
	case UNITS_BYTES:
			   base = 1;
			   num_divs = 0;
			   break;
	default:
		while (size >= mult) {
			last_size = size;
			size /= mult;
			num_divs++;
		}
		/*
		 * If the value is smaller than base, we didn't do any
		 * division, in that case, base should be 1, not original
		 * base, or the unit will be wrong
		 */
		if (num_divs == 0)
			base = 1;
	}

	if (num_divs >= ARRAY_SIZE(unit_suffix_binary)) {
		str[0] = '\0';
		printf("INTERNAL ERROR: unsupported unit suffix, index %d\n",
				num_divs);
		assert(0);
		return -1;
	}
	fraction = (float)last_size / base;

	return snprintf(str, str_size, "%.2f%s", fraction, suffix[num_divs]);
}

/*
 * For a given:
 * - file or directory return the containing tree root id
 * - subvolume return its own tree id
 * - BTRFS_EMPTY_SUBVOL_DIR_OBJECTID (directory with ino == 2) the result is
 *   undefined and function returns -1
 */
int lookup_ino_rootid(int fd, u64 *rootid)
{
	struct btrfs_ioctl_ino_lookup_args args;
	int ret;

	memset(&args, 0, sizeof(args));
	args.treeid = 0;
	args.objectid = BTRFS_FIRST_FREE_OBJECTID;

	ret = ioctl(fd, BTRFS_IOC_INO_LOOKUP, &args);
	if (ret < 0)
		return -errno;

	*rootid = args.treeid;

	return 0;
}

