#ifndef _CONTAIN_H
#define _CONTAIN_H
#ifdef __cplusplus
extern "C" {
#endif

struct container_handle {
	int mount_ns_fd;
};

#include <stdbool.h>

#ifdef __linux__
bool maybe_reexec(char* argv);
int maybe_contain(struct container_handle *);
#else
inline bool maybe_reexec(char* /*binary_path*/) {
	return false;
}

inline int maybe_contain(struct container_handle *) {
	return 0;
}

#endif
#ifdef __cplusplus
}
#endif
#endif
