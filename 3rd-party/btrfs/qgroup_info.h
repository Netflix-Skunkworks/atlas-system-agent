#pragma once

#include <stddef.h>
#include <stdint.h>

struct btrfs_qgroup_info {
	uint64_t qgroupid;

	/*
	 * info_item
	 */
	uint64_t generation;
	uint64_t rfer;	/*referenced*/
	uint64_t rfer_cmpr;	/*referenced compressed*/
	uint64_t excl;	/*exclusive*/
	uint64_t excl_cmpr;	/*exclusive compressed*/

	/*
	 *limit_item
	 */
	uint64_t flags;	/*which limits are set*/
	uint64_t max_rfer;
	uint64_t max_excl;
	uint64_t rsv_rfer;
	uint64_t rsv_excl;
};

