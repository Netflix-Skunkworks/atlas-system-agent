#ifdef linux
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <stdbool.h>
#include <linux/types.h>
#include <errno.h>
/* setns */
#include <sched.h>
/* read */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/* PATH_MAX */
#include <limits.h>
/* Formatting */
#include <stdio.h>
#include <string.h>
/* FD Trickery */
#include <fcntl.h>
/* Forking */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
/* Mounting */
#include <sys/mount.h>
/* Dynamic loading */
#include <netdb.h>
/* Stat */
#include <sys/stat.h>
/* capabilities */
#include <linux/capability.h>
#include <sys/capability.h>

#include "contain.h"

/* This is here to work with older linuxes */
#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_RAISE 2
#endif

/*
 * This checks if TITUS_PID_1_DIR is set
 * TITUS_PID_1_DIR is generated by the executor by bind mounting
 * /proc/${INIT OF CONTAINER}/ to some directory. Then we can go ahead
 * and open ${TITUS_PID_1_DIR}/ns/... and go into their namespaces.
 * In addition, we read the environment variables of ${TITUS_PID_1_DIR}/environ
 * and try to load the environment variables
 *
 * On success (either contained or not) it returns 0
 * On failure, it returns errno
 */

#define ARRAY_SIZE(array) \
    (sizeof(array) / sizeof(*array))
#define PID_1_DIR_ENV "TITUS_PID_1_DIR"
/* 4 MB */
#define READ_SIZE 4096
#define MAX_ENV_SIZE 64*1024*1024

struct ns {
	const char *name;
	int nstype;
};

#define FD_OFFSET 133

static struct ns namespaces[] = {
	/*
	 * We purposely omit the following:
	 * cgroup - Could be unsupported. Also, more complex path mangling required
	 * user - Isolation can break some stuff
	 *
	 * Order matters. Join the Pid namespace last.
	 */
	{
		.name = "uts",
		.nstype = CLONE_NEWUTS,
	},
	{
		.name = "net",
		.nstype = CLONE_NEWNET,
	},
	{
		.name = "ipc",
		.nstype = CLONE_NEWIPC,

	},
	{
		.name = "pid",
		.nstype = CLONE_NEWPID,
	},
	{
		.name = "mnt",
		.nstype = CLONE_NEWNS,
	},
};

static cap_value_t cap_list[] = {CAP_SYS_ADMIN};

static int setup_capabilities() {
	int err = 0;
	cap_t caps;

	caps = cap_get_proc();
	if (!caps) {
		err = errno;
		goto out;
	}

	if (cap_set_flag(caps, CAP_INHERITABLE, ARRAY_SIZE(cap_list), cap_list, CAP_SET)) {
		err = errno;
		goto out;
	}

	if (cap_set_proc(caps)) {
		err = errno;
		goto out;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_ADMIN, 0, 0)) {
		err = errno;
		goto out;
	}
out:
	return err;
}

static int open_fds(char *pid1dir) {
	int namespace_fds[ARRAY_SIZE(namespaces)];
	char nsfile[PATH_MAX];
	int err = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(namespaces); i++)
		namespace_fds[i] = -1;

	for (i = 0; i < ARRAY_SIZE(namespaces); i++) {
		if (snprintf(nsfile, sizeof(nsfile), "%s/ns/%s", pid1dir, namespaces[i].name) < 0) {
			err = errno;
			fprintf(stderr, "Could not format environfile\n");
			goto out;
		}
		namespace_fds[i] = open(nsfile, O_RDONLY);
		if (namespace_fds[i] == -1) {
			err = errno;
			goto out;
		}
	}

	for (i = 0; i < ARRAY_SIZE(namespaces); i++) {
		err = dup2(namespace_fds[i], FD_OFFSET + i);
		if (err == -1) {
			err = errno;
			goto out;
		}
		close(namespace_fds[i]);
	}

	return 0;
out:
	for (i = 0; i < ARRAY_SIZE(namespaces); i++) {
		if (namespace_fds[i] >= 0) {
			close(namespace_fds[i]);
		}
	}
	return err;
}

static int read_buf(char *pid1dir, char **env, int *length, struct stat *st) {
	/* No need to free anything here */
	int read_amount, offset = 0, fd;
	char environfile[PATH_MAX];
	char *envbuf;

	if (snprintf(environfile, sizeof(environfile), "%s/%s", pid1dir, "environ") < 0) {
		fprintf(stderr, "Could not format environfile\n");
		return EINVAL;
	}
	fd = open(environfile, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return errno;

	if (fstat(fd, st) == -1)
		return errno;

	*env = malloc(MAX_ENV_SIZE + READ_SIZE);
	if (*env == NULL) {
		return ENOMEM;
	}
	envbuf = *env;
	memset(envbuf, 0, MAX_ENV_SIZE + READ_SIZE);

	do {
		read_amount = read(fd, envbuf + offset, READ_SIZE);
		offset += read_amount;
		if (offset > MAX_ENV_SIZE) {
			fprintf(stderr, "Environment size is larger than expected: %d\n", MAX_ENV_SIZE);
			return E2BIG;
		}
	} while(read_amount > 0);

	if (read_amount == -1)
		return errno;

	if (offset <= 0) {
		fprintf(stderr, "Environment invalid, size: %d\n", offset);
		return EINVAL;
	}

	*length = offset;

	return 0;
}

static char *LD_LIBRARY_PATH = "/proc/self/lib/x86_64-linux-gnu:/proc/self/usr/local/lib";
static char *LD_BIND_NOW1 = "LD_BIND_NOW=1";

bool maybe_reexec(char* const* argv) {
	char user_gid_env[128], user_uid_env[128];
	char *pid1dir, *env = NULL;
	int err, offset = 0;
	int env_length = 0;
	char *envp[1024];
	struct stat st;
	unsigned int i;

	pid1dir = getenv(PID_1_DIR_ENV);
	if (!pid1dir)
		return false;

	/* 1. Get Environment Buffer */
	err = read_buf(pid1dir, &env, &env_length, &st);
	if (err)
		goto end;

	snprintf(user_uid_env, sizeof(user_uid_env), "USER_UID=%d", st.st_uid);
	snprintf(user_gid_env, sizeof(user_gid_env), "USER_GID=%d", st.st_gid);

	/* 2. Make it into an ENVP, saving space for four additional values */
	memset(envp, 0, sizeof(envp));
	i = 0;
	do {
		if (env[offset] != '\0') {
			envp[i++] = &env[offset];
			offset += strlen(&env[offset]) + 1;
		} else
			offset++;
	} while (offset < env_length && i < (ARRAY_SIZE(envp) - 5));

	if (i >= (ARRAY_SIZE(envp) - 5))
		fprintf(stderr, "Not capturing all environment variables.\n");

	envp[i++] = LD_LIBRARY_PATH;
	envp[i++] = LD_BIND_NOW1;
	envp[i++] = user_uid_env;
	envp[i++] = user_gid_env;

	/* 3. Open up ns FDs */
	err = open_fds(pid1dir);
	if (err)
		goto end;

	/* setup capabilities */
	err = setup_capabilities();
	if (err)
		goto end;

	execvpe(argv[0], (char * const*)argv, envp);
end:
	fprintf(stderr, "Could not reexec: %s\n", strerror(err));
	return true;
}

static int do_fork() {
	pid_t child_pid;
	int status;

	child_pid = fork();
	if (child_pid < 0) {
		/* Fork failed*/
		fprintf(stderr, "Unable to fork after entering child namespace\n");
		return 1;
	}
	if (child_pid == 0) {
		const char *id_env = getenv("USER_GID");
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (id_env) {
			if (setegid(atoi(id_env))) {
				fprintf(stderr, "Unable to set effective GID: %s", strerror(errno));
				exit(1);
			}
		}
		id_env = getenv("USER_UID");
		if (id_env) {
			if (seteuid(atoi(id_env))) {
				fprintf(stderr, "Unable to set effective UID: %s", strerror(errno));
				exit(1);
			}
		}
		return 0;
	}

	waitpid(child_pid, &status, 0);
	exit(WEXITSTATUS(status));
}

/* This is to trigger dynamic loading before we switch mount namespaces.
 * Unfortunately, C++ has no way of telling it to load everything.
 */
static int do_load() {
	gethostbyname("google.com");

	return 0;
}

int maybe_contain(struct container_handle *c) {
	unsigned int i;
	int err;

	err = do_load();
	if (err)
		return err;

	c->mount_ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	if (c->mount_ns_fd < 0) {
		fprintf(stderr, "Error opening up mount namespace fd: %s\n", strerror(errno));
		return 1;
	}

	for (i = 0; i < ARRAY_SIZE(namespaces); i++) {
		err = fcntl(FD_OFFSET + i, F_GETFD);
		if (err && errno == EBADF)
			/* We are not under containment*/
			return 0;

		if (err) {
			fprintf(stderr, "Error fetching file descriptors to determine containment: %s\n", strerror(errno));
			return 1;
		}
	}

	fprintf(stderr, "Beginning containment\n");
	for (i = 0; i < ARRAY_SIZE(namespaces); i++) {
		if (setns(FD_OFFSET + i, namespaces[i].nstype)) {
			fprintf(stderr, "Could not activate namespace %s, because: %s\n", namespaces[i].name, strerror(errno));
			return 1;
		}
		close(FD_OFFSET + i);
	}

	fprintf(stderr, "Contained\n");
	return do_fork();
}
#endif
