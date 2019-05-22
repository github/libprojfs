/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the NOTICE file distributed with this library for additional
   information regarding copyright ownership.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library, in the file COPYING; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include <config.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <attr/xattr.h>
#include <unistd.h>

#include "fdtable.h"
#include "projfs.h"

#define FUSE_USE_VERSION 32
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

// TODO: make this value configurable
#define PROJ_WAIT_MSEC 5000

struct projfs_config {
	int initial;
	char *log;
};

#define PROJFS_OPT(t, p, v) { t, offsetof(struct projfs_config, p), v }

static struct fuse_opt projfs_opts[] = {
	PROJFS_OPT("initial",	initial, 1),
	PROJFS_OPT("--initial",	initial, 1),

	PROJFS_OPT("log=%s",	log, 0),
	PROJFS_OPT("--log=%s",	log, 0),

	FUSE_OPT_END
};

struct projfs {
	char *lowerdir;
	char *mountdir;
	struct projfs_handlers handlers;
	void *user_data;
	struct fuse_args args;
	struct projfs_config config;
	pthread_mutex_t mutex;
	struct fuse_session *session;
	FILE *log_file;
	int lowerdir_fd;
	pthread_t thread_id;
	struct fdtable *fdtable;
	int error;
};

typedef int (*projfs_handler_t)(struct projfs_event *);

struct projfs_dir {
	DIR *dir;
	long loc;
	struct dirent *ent;
};

// NOTE: only functional within a FUSE file operation!
static inline struct projfs *get_fuse_context_projfs(void)
{
	return (struct projfs *)fuse_get_context()->private_data;
}

// NOTE: only functional within a FUSE file operation!
static inline int get_fuse_context_lowerdir_fd(void)
{
	return get_fuse_context_projfs()->lowerdir_fd;
}

// ceil(log10(INT_MAX)) = ceil(log10(2) * sizeof(int) * CHAR_BIT)
//			<     (   1/3   * sizeof(int) * CHAR_BIT) + 1
#define INT_FMT_LEN ((sizeof(int) * CHAR_BIT) / 3 + 1)

#define PROC_STATUS_PATH_FMT "/proc/%d/status"
#define MAX_PROC_STATUS_PATH_LEN \
	(sizeof(PROC_STATUS_PATH_FMT) + INT_FMT_LEN - 3)

#define PROC_STATUS_BUF_SIZE 32

#define PROC_STATUS_TGID_KEY "Tgid:"
#define PROC_STATUS_TGID_KEY_LEN (sizeof(PROC_STATUS_TGID_KEY) - 1)

// NOTE: only functional within a FUSE file operation!
static pid_t get_fuse_context_tgid(void)
{
	pid_t pid = fuse_get_context()->pid;
	char path[MAX_PROC_STATUS_PATH_LEN + 1];
	char buf[PROC_STATUS_BUF_SIZE];
	FILE *file;
	int found = 0;

	// do not report IO or parsing errors
	sprintf(path, PROC_STATUS_PATH_FMT, pid);
	file = fopen(path, "r");
	if (file == NULL)
		return pid;		// best effort
	while (fgets(buf, sizeof(buf), file) != NULL) {
		if (strncmp(buf, PROC_STATUS_TGID_KEY,
			    PROC_STATUS_TGID_KEY_LEN) == 0)
		{
			found = 1;
			break;
		}
		while (strchr(buf, '\n') == NULL &&
		       fgets(buf, sizeof(buf), file) != NULL);
	}
	fclose(file);

	if (found) {
		char *s = buf + PROC_STATUS_TGID_KEY_LEN;
		unsigned long val = 0;

		while (isblank(*s))
			++s;
		while (isdigit(*s) && val < INT_MAX)
			val = val * 10 + (*s++ - '0');
		while (isblank(*s))
			++s;
		if (*s == '\n' && val < INT_MAX)
			pid = val;
	}

	return pid;
}

enum log_stderr_opt {
	LOG_STDERR_NONE,
	LOG_STDERR_ONLY,
	LOG_STDERR_BOTH,
	LOG_STDERR_FALLBACK
};

static void log_file_printf(FILE *file, const char *fmt, va_list ap)
{
	if (file == stderr)
		fprintf(file, "projfs: ");
	vfprintf(file, fmt, ap);
	fprintf(file, "\n");
}

static void log_printf(struct projfs *fs, enum log_stderr_opt stderr_opt,
		       const char *fmt, ...)
{
	FILE *log_file = NULL;
	int use_stderr = (stderr_opt != LOG_STDERR_NONE);
	va_list ap;

	if (fs == NULL || fs->log_file == NULL) {
		if (!use_stderr)
			return;
	} else if (stderr_opt != LOG_STDERR_ONLY) {
		log_file = fs->log_file;
		if (stderr_opt == LOG_STDERR_FALLBACK)
			use_stderr = 0;
	}

	if (log_file != NULL) {
		va_start(ap, fmt);
		log_file_printf(log_file, fmt, ap);
		va_end(ap);
	}

	if (use_stderr) {
		va_start(ap, fmt);
		log_file_printf(stderr, fmt, ap);
		va_end(ap);
	}
}

// NOTE: only functional within a FUSE file operation!
static void log_printf_fuse_context(const char *fmt, ...)
{
	FILE *log_file;
	va_list ap;

	log_file = get_fuse_context_projfs()->log_file;
	if (log_file == NULL)
		return;

	va_start(ap, fmt);
	log_file_printf(log_file, fmt, ap);
	va_end(ap);
}

static int log_open(struct projfs *fs)
{
	if (fs->config.log == NULL)
		return 0;

	fs->log_file = fopen(fs->config.log, "a");
	if (fs->log_file == NULL) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "error opening log file: %s: %s",
			   strerror(errno), fs->config.log);
		return -1;
	}

	return 0;
}

static void log_close(struct projfs *fs)
{
	if (fs->log_file != NULL)
		fclose(fs->log_file);
}

/**
 * @return 0 or a negative errno
 */
static int send_event(projfs_handler_t handler, uint64_t mask, pid_t pid,
		      const char *path, const char *target_path,
		      int fd, int perm)
{
	struct projfs_event event;
	int err;

	if (handler == NULL)
		return 0;

	if (pid == 0)
		pid = get_fuse_context_tgid();

	event.fs = get_fuse_context_projfs();
	event.mask = mask;
	event.pid = pid;
	event.path = path;
	event.target_path = target_path;
	event.fd = fd;

	err = handler(&event);
	if (err < 0) {
		log_printf_fuse_context("event handler failed: %s; "
					"mask 0x%04" PRIx64 "-%08" PRIx64 ", "
					"pid %d, path %s%s%s",
					strerror(-err),
					mask >> 32, mask & 0xFFFFFFFF,
					pid, path,
					(target_path == NULL)
						? "" : ", target path ",
					(target_path == NULL)
						? "" : target_path);
	}
	else if (perm) {
		err = (err == PROJFS_ALLOW) ? 0 : -EPERM;
	}

	return err;
}

/**
 * @return 0 or a negative errno
 */
static int send_proj_event(uint64_t mask, const char *path, int fd)
{
	projfs_handler_t handler =
		get_fuse_context_projfs()->handlers.handle_proj_event;

	return send_event(handler, mask, 0, path, NULL, fd, 0);
}

/**
 * @return 0 or a negative errno
 */
static int send_notify_event(uint64_t mask, pid_t pid, const char *path,
			     const char *target_path)
{
	projfs_handler_t handler =
		get_fuse_context_projfs()->handlers.handle_notify_event;

	return send_event(handler, mask, pid, path, target_path, 0, 0);
}

/**
 * @return 0 or a negative errno
 */
static int send_perm_event(uint64_t mask, const char *path,
			   const char *target_path)
{
	projfs_handler_t handler =
		get_fuse_context_projfs()->handlers.handle_perm_event;

	return send_event(handler, mask, 0, path, target_path, 0, 1);
}

#define PROJ_XATTR_PRE_NAME "user.projection."
#define PROJ_XATTR_PRE_LEN (sizeof(PROJ_XATTR_PRE_NAME) - 1)

#define PROJ_STATE_XATTR_NAME PROJ_XATTR_PRE_NAME"empty"

static int xattr_name_has_prefix(const char *name)
{
	if (strncmp(name, PROJ_XATTR_PRE_NAME, PROJ_XATTR_PRE_LEN) == 0)
		return 1;

	return 0;
}

static int xattr_name_is_reserved(const char *name)
{
	if (strcmp(name, PROJ_STATE_XATTR_NAME) == 0)
		return 1;
	// add other reserved names as they are defined

	return 0;
}

static int get_xattr(int fd, const char *name, void *value, ssize_t *size)
{
	if (fgetxattr(fd, name, value, *size) == -1) {
		if (errno != ENOATTR)
			return -1;
		*size = -1;
	}
	return 0;
}

static int set_xattr(int fd, const char *name, const void *value,
		     ssize_t *size, int flags)
{
	if (value == NULL || *size == 0) {
		if (fremovexattr(fd, name) == -1) {
			if (errno != ENOATTR)
				return -1;
			*size = -1;
		}
		return 0;
	}

	return fsetxattr(fd, name, value, *size, flags);
}

enum proj_state {
	PROJ_STATE_ERROR = -1,	/* invalid state */
	PROJ_STATE_EMPTY,	/* unopened (sparse file with metadata) */
	PROJ_STATE_POPULATED,	/* unmodified (hydrated with data) */
	PROJ_STATE_MODIFIED	/* no longer projected (fully local) */
};

#define PROJ_STATE_XATTR_VALUE_EMPTY 'y'
#define PROJ_STATE_XATTR_VALUE_POPULATED 'n'
/* The PROJ_STATE_XATTR_NAME xattr is removed for the MODIFIED state. */

static enum proj_state get_proj_state_xattr(int fd)
{
	char value;
	ssize_t size = sizeof(value);

	if (get_xattr(fd, PROJ_STATE_XATTR_NAME, &value, &size) == -1)
		return PROJ_STATE_ERROR;

	if (size == -1)
		return PROJ_STATE_MODIFIED;

	switch (value) {
	case PROJ_STATE_XATTR_VALUE_POPULATED:
		return PROJ_STATE_POPULATED;
	case PROJ_STATE_XATTR_VALUE_EMPTY:
		return PROJ_STATE_EMPTY;
	default:
		errno = EINVAL;
		return PROJ_STATE_ERROR;
	}
}

static int set_proj_state_xattr(int fd, enum proj_state state, int flags)
{
	char value;
	void *valuep = &value;
	ssize_t size = sizeof(value);

	switch (state) {
	case PROJ_STATE_POPULATED:
		value = PROJ_STATE_XATTR_VALUE_POPULATED;
		break;
	case PROJ_STATE_EMPTY:
		value = PROJ_STATE_XATTR_VALUE_EMPTY;
		break;
	case PROJ_STATE_MODIFIED:
		valuep = NULL;
		size = 0;
		break;
	default:
	case PROJ_STATE_ERROR:
		errno = EINVAL;
		return -1;
	}

	return set_xattr(fd, PROJ_STATE_XATTR_NAME, valuep, &size, flags);
}

struct proj_state_lock {
	int lock_fd;
	enum proj_state state;
};

/**
 * Acquires a lock on path and populates the supplied proj_state_lock argument
 * with the open and locked fd, and state based on the
 * PROJ_STATE_XATTR_NAME xattr.
 *
 * @param state_lock structure to fill out (zeroed by this function)
 * @param path path relative to lowerdir to lock and open
 * @param flags file flags with which to open the locked fd
 * @return 0 or an errno
 */
static int acquire_proj_state_lock(struct proj_state_lock *state_lock,
				   const char *path, int flags)
{
	enum proj_state state;
	int err, wait_ms;
	struct timespec ts;

	memset(state_lock, 0, sizeof(*state_lock));

	state_lock->lock_fd = openat(get_fuse_context_lowerdir_fd(),
				     path, flags);
	if (state_lock->lock_fd == -1)
		return errno;

	wait_ms = PROJ_WAIT_MSEC;

retry_flock:
	// TODO: may conflict with locks held by clients; use internal locks
	err = flock(state_lock->lock_fd, LOCK_EX | LOCK_NB);
	if (err == -1) {
		if (errno == EWOULDBLOCK && wait_ms > 0) {
			/* sleep 100ms, retry */
			ts.tv_sec = 0;
			ts.tv_nsec = 1000 * 1000 * 100;
			nanosleep(&ts, NULL);
			wait_ms -= 100;
			goto retry_flock;
		}

		err = errno;
		goto out_close;
	}

	state = get_proj_state_xattr(state_lock->lock_fd);
	if (state == PROJ_STATE_ERROR) {
		err = errno;
		goto out_close;
	}

	state_lock->state = state;
	return 0;

out_close:
	close(state_lock->lock_fd);
	state_lock->lock_fd = -1;
	return err;
}

/**
 * Closes the open fd associated with state_lock, which in turn releases any
 * locks associated with the lock_fd.
 *
 * @param state_lock projection state structure to clean up
 */
static void release_proj_state_lock(struct proj_state_lock *state_lock)
{
	if (state_lock->lock_fd == -1)
		return;

	close(state_lock->lock_fd);
	state_lock->lock_fd = -1;
}

/**
 * Projects a path by notifying the provider with the given event mask.  If the
 * provider succeeds, updates the projection state on the path.
 *
 * @param state_lock current projection state and lock held on inode
 * @param path the path of the inode whose projection state should be updated
 * @param isdir 1 if the path is a directory; 0 otherwise
 * @param state projection state to which the inode should be updated
 * @return 0 or an errno
 */
static int project_locked_path(struct proj_state_lock *state_lock,
			       const char *path, int isdir,
			       enum proj_state state)
{
	int res;

	if (isdir || state == PROJ_STATE_POPULATED) {
		uint64_t event_mask = PROJFS_CREATE;

		if (isdir)
			event_mask |= PROJFS_ONDIR;
		res = send_proj_event(event_mask, path, state_lock->lock_fd);
	} else {
		res = send_perm_event(PROJFS_OPEN_PERM, path, NULL);
	}

	if (res < 0)
		return -res;

	if (state == PROJ_STATE_POPULATED) {
		res = set_proj_state_xattr(state_lock->lock_fd, state,
					   XATTR_REPLACE);
	} else {
		res = set_proj_state_xattr(state_lock->lock_fd, state, 0);
	}

	if (res == -1)
		return errno;

	state_lock->state = state;
	return 0;
}

/**
 * Return a copy of path with the last component removed (e.g. "x/y/z" will
 * yield "x/y").  If path has only one component, returns ".".
 *
 * The caller is responsible for freeing the returned string.
 *
 * @param path path to get parent directory of
 * @return name of parent of path; may be NULL if strdup or strndup fails
 */
static char *get_path_parent(char const *path)
{
	const char *last = strrchr(path, '/');
	if (!last)
		return strdup(".");
	else
		return strndup(path, last - path);
}

/**
 * Project a directory. Takes the path, and a flag indicating whether the
 * directory is the parent of the path, or the path itself.
 *
 * @param op op name (for debugging)
 * @param path path within lowerdir (from make_relative_path())
 * @param parent 1 if we should look at the parent directory containing path, 0
 *               if we look at path itself
 * @return 0 or an errno
 */
static int project_dir(const char *op, const char *path, int parent)
{
	struct proj_state_lock state_lock;
	char *lock_path;
	int log = 0;
	int res;

	if (parent)
		lock_path = get_path_parent(path);
	else
		lock_path = strdup(path);
	if (lock_path == NULL)
		return errno;

	res = acquire_proj_state_lock(&state_lock, lock_path,
				      O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (res != 0)
		goto out;

	if (state_lock.state != PROJ_STATE_EMPTY)
		goto out_release;

	// directories skip intermediate state; either empty or fully local
	res = project_locked_path(&state_lock, lock_path, 1,
				  PROJ_STATE_MODIFIED);
	log = (res == 0);

out_release:
	release_proj_state_lock(&state_lock);

	if (log) {
		log_printf_fuse_context("directory projected to "
					"'modified' state in '%s' op: %s",
					op, lock_path);
	}

out:
	free(lock_path);

	return res;
}

/**
 * Project a file. Takes the lower path.
 *
 * @param op op name (for debugging)
 * @param path the lower path (from lowerpath)
 * @param state the projection state to apply (populated or modified)
 * @return 0 or an errno
 */
static int project_file(const char *op, const char *path,
			enum proj_state state)
{
	struct proj_state_lock state_lock;
	int log = 0;
	int res;

	/* Pass O_NOFOLLOW so we receive ELOOP if path is an existing symlink,
	 * which we want to ignore, and request a write mode so we receive
	 * EISDIR if path is a directory.
	 */
	res = acquire_proj_state_lock(&state_lock, path,
				      O_RDWR | O_NOFOLLOW | O_NONBLOCK);
	if (res != 0) {
		if (res == ELOOP)
			return 0;
		else
			return res;
	}

	// hydrate empty placeholder file
	if (state_lock.state == PROJ_STATE_EMPTY) {
		struct stat st;
		int reset_mtime;

		reset_mtime = (fstat(state_lock.lock_fd, &st) == 0);

		res = project_locked_path(&state_lock, path, 0,
					  PROJ_STATE_POPULATED);
		log = (res == 0);

		if (res == 0 && reset_mtime) {
			struct timespec times[2];

			times[0].tv_nsec = UTIME_OMIT;
			memcpy(&times[1], &st.st_mtim, sizeof(times[1]));

			futimens(state_lock.lock_fd, times);	// best effort
		}
	}

	// if requested, convert hydrated file to fully local, modified file
	if (res == 0 && state_lock.state == PROJ_STATE_POPULATED &&
	    state == PROJ_STATE_MODIFIED) {
		res = project_locked_path(&state_lock, path, 0, state);
		log = (res == 0);
	}

	release_proj_state_lock(&state_lock);

	if (log) {
		log_printf_fuse_context("file projected to '%s' state "
					"in '%s' op: %s",
					(state == PROJ_STATE_POPULATED)
						? "populated" : "modified",
					op, path);
	}

	return res;
}

/**
 * Makes a path from FUSE usable as a relative path to lowerdir_fd.  Removes
 * any leading forward slashes.  If the resulting path is empty, returns ".".
 * */
static inline const char *make_relative_path(const char *path)
{
	while (*path == '/')
		++path;
	if (*path == '\0')
		path = ".";
	return path;
}

// filesystem ops

static int projfs_op_getattr(char const *path, struct stat *attr,
                             struct fuse_file_info *fi)
{
	int res;

	if (fi)
		res = fstat(fi->fh, attr);
	else {
		path = make_relative_path(path);
		if (strcmp(path, ".") != 0) {
			res = project_dir("getattr", path, 1);
			if (res)
				return -res;
		}
		res = fstatat(get_fuse_context_lowerdir_fd(), path, attr,
			      AT_SYMLINK_NOFOLLOW);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_readlink(char const *path, char *buf, size_t size)
{
	int res;

	path = make_relative_path(path);
	res = project_dir("readlink", path, 1);
	if (res)
		return -res;
	res = readlinkat(get_fuse_context_lowerdir_fd(), path, buf, size - 1);
	if (res == -1)
		return -errno;
	buf[res] = 0;
	return 0;
}

static int projfs_op_link(char const *src, char const *dst)
{
	int lowerdir_fd;
	int res;

	/* NOTE: We require lowerdir to be a directory, so this should
	 *       fail when src is an empty path, as we expect.
	 */
	src = make_relative_path(src);
	res = project_dir("link", src, 1);
	if (res)
		return -res;

	/* hydrate the source file before adding a hard link to it, otherwise
	 * a user could access the newly created link and end up modifying the
	 * non-hydrated placeholder */
	res = project_file("link", src, PROJ_STATE_POPULATED);
	if (res)
		return -res;

	dst = make_relative_path(dst);
	res = project_dir("link2", dst, 1);
	if (res)
		return -res;

	lowerdir_fd = get_fuse_context_lowerdir_fd();
	res = linkat(lowerdir_fd, src, lowerdir_fd, dst, 0);
	if (res == -1)
		return -errno;

	// do not report event handler errors after successful link op
	(void)send_notify_event(PROJFS_CREATE | PROJFS_ONLINK, 0, src, dst);
	return 0;
}

static void *projfs_op_init(struct fuse_conn_info *conn,
                            struct fuse_config *cfg)
{
	(void)conn;

	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;
	cfg->use_ino = 1;

	return get_fuse_context_projfs();
}

#define has_write_mode(fi) ((fi)->flags & (O_WRONLY | O_RDWR))

static int projfs_op_flush(char const *path, struct fuse_file_info *fi)
{
	int res, err;

	res = close(dup(fi->fh));
	err = errno;		// errno may be changed by fdtable realloc

	if (has_write_mode(fi)) {
		// do not report table realloc errors after successful close op
		(void)fdtable_replace(get_fuse_context_projfs()->fdtable,
				      fi->fh, get_fuse_context_tgid());
	}

	return res == -1 ? -err : 0;
}

static int projfs_op_fsync(char const *path, int datasync,
                           struct fuse_file_info *fi)
{
	int res;

	(void)path;
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mknod(char const *path, mode_t mode, dev_t rdev)
{
	int res;

	(void)rdev;

	path = make_relative_path(path);
	res = project_dir("mknod", path, 1);
	if (res)
		return -res;
	if (S_ISFIFO(mode))
		res = mkfifoat(get_fuse_context_lowerdir_fd(), path, mode);
	else
		return -ENOSYS;
	return res == -1 ? -errno : 0;
}

static int projfs_op_symlink(char const *link, char const *path)
{
	int res;

	path = make_relative_path(path);
	res = project_dir("symlink", path, 1);
	if (res)
		return -res;
	res = symlinkat(link, get_fuse_context_lowerdir_fd(), path);
	return res == -1 ? -errno : 0;
}

static int projfs_op_create(char const *path, mode_t mode,
                            struct fuse_file_info *fi)
{
	/* FUSE sets fi.flags = O_CREAT | O_EXCL | O_WRONLY in fuse_lib_mknod,
	 * untouched in fuse_lib_create where it comes straight from
	 * FUSE_CREATE.  In Linux 4.9 at least, we never hit a codepath where
	 * we send FUSE_CREATE without first checking that O_CREAT is set.
	 * There's no guarantee O_EXCL (or O_TRUNC) are set, though, so we need
	 * to hydrate it if it exists. */

	int flags = fi->flags & ~O_NOFOLLOW;
	int res;
	int fd;

	path = make_relative_path(path);
	res = project_dir("create", path, 1);
	if (res)
		return -res;
	res = project_file("create", path, PROJ_STATE_POPULATED);
	if (res && res != ENOENT)
		return -res;
	fd = openat(get_fuse_context_lowerdir_fd(), path, flags, mode);

	if (fd == -1)
		return -errno;
	fi->fh = fd;

	if (has_write_mode(fi)) {
		// do not report table realloc errors after successful open op
		(void)fdtable_insert(get_fuse_context_projfs()->fdtable,
				     fd, get_fuse_context_tgid());
	 }

	// do not report event handler errors after successful open op
	(void)send_notify_event(PROJFS_CREATE, 0, path, NULL);
	return 0;
}

static int projfs_op_open(char const *path, struct fuse_file_info *fi)
{
	int flags = fi->flags & ~O_NOFOLLOW;
	int res;
	int fd;

	path = make_relative_path(path);
	res = project_dir("open", path, 1);
	if (res)
		return -res;

	/* Per above, allow hydration to fail with ENOENT; if the file
	 * operation should fail for that reason (i.e. O_CREAT is not specified
	 * and the file doesn't exist), we'll return the failure from openat(2)
	 * below.
	 */
	res = project_file("open", path,
			   has_write_mode(fi) ? PROJ_STATE_MODIFIED
					      : PROJ_STATE_POPULATED);
	if (res) {
		// if path was a directory, try projecting it instead
		if (res == EISDIR)
			res = project_dir("open", path, 0);
		if (res != ENOENT)
			return -res;
	}

	fd = openat(get_fuse_context_lowerdir_fd(), path, flags);
	if (fd == -1)
		return -errno;

	if (has_write_mode(fi)) {
		// do not report table realloc errors after successful open op
		(void)fdtable_insert(get_fuse_context_projfs()->fdtable,
				     fd, get_fuse_context_tgid());
	}

	fi->fh = fd;
	return 0;
}

static int projfs_op_statfs(char const *path, struct statvfs *buf)
{
	int res;

	(void)path;
	// TODO: should we return our own filesystem's global info?
	res = fstatvfs(get_fuse_context_lowerdir_fd(), buf);
	return res == -1 ? -errno : 0;
}

static int projfs_op_read_buf(char const *path, struct fuse_bufvec **bufp,
			      size_t size, off_t off,
			      struct fuse_file_info *fi)
{
	struct fuse_bufvec *src = malloc(sizeof(*src));

	(void) path;

	if (!src)
		return -errno;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = off;

	*bufp = src;

	return 0;
}

static int projfs_op_write_buf(char const *path, struct fuse_bufvec *src,
			       off_t off, struct fuse_file_info *fi)
{
	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(fuse_buf_size(src));

	(void)path;
	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = off;

	return fuse_buf_copy(&buf, src, FUSE_BUF_SPLICE_NONBLOCK);
}

static int projfs_op_release(char const *path, struct fuse_file_info *fi)
{
	int res, err;
	pid_t pid = 0;

	res = close(fi->fh);
	err = errno;		// errno may be changed by fdtable realloc

	if (has_write_mode(fi)) {
		// do not report table realloc errors after successful close op
		(void)fdtable_remove(get_fuse_context_projfs()->fdtable,
				     fi->fh, &pid);
	}

	// return value is ignored by libfuse, but be consistent anyway
	if (res == -1)
		return -err;

	if (has_write_mode(fi)) {
		// do not report event handler errors after successful close op
		(void)send_notify_event(PROJFS_CLOSE_WRITE, pid,
					make_relative_path(path), NULL);
	}
	return 0;
}

static int projfs_op_unlink(char const *path)
{
	int res;

	path = make_relative_path(path);
	res = send_perm_event(PROJFS_DELETE_PERM, path, NULL);
	if (res < 0)
		return res;
	res = project_dir("unlink", path, 1);
	if (res)
		return -res;

	res = unlinkat(get_fuse_context_lowerdir_fd(), path, 0);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mkdir(char const *path, mode_t mode)
{
	int res;

	path = make_relative_path(path);
	res = project_dir("mkdir", path, 1);
	if (res)
		return -res;
	res = mkdirat(get_fuse_context_lowerdir_fd(), path, mode);
	if (res == -1)
		return -errno;

	// do not report event handler errors after successful mkdir op
	(void)send_notify_event(PROJFS_CREATE | PROJFS_ONDIR, 0, path, NULL);
	return 0;
}

static int projfs_op_rmdir(char const *path)
{
	int res;

	path = make_relative_path(path);
	res = send_perm_event(PROJFS_DELETE_PERM | PROJFS_ONDIR, path, NULL);
	if (res < 0)
		return res;
	res = project_dir("rmdir", path, 1);
	if (res)
		return -res;

	res = unlinkat(get_fuse_context_lowerdir_fd(), path, AT_REMOVEDIR);
	return res == -1 ? -errno : 0;
}

static int projfs_op_rename(char const *src, char const *dst,
                            unsigned int flags)
{
	uint64_t mask = PROJFS_MOVE;
	int lowerdir_fd;
	int res;

	src = make_relative_path(src);
	res = project_dir("rename", src, 1);
	if (res)
		return -res;
	// always convert to fully local file before renaming
	res = project_file("rename", src, PROJ_STATE_MODIFIED);
	if (res == EISDIR)
		mask |= PROJFS_ONDIR;
	else if (res)
		return -res;

	dst = make_relative_path(dst);
	res = project_dir("rename2", dst, 1);
	if (res)
		return -res;

	// TODO: for non Linux, use renameat(); fail if flags != 0
	lowerdir_fd = get_fuse_context_lowerdir_fd();
	res = syscall(SYS_renameat2, lowerdir_fd, src, lowerdir_fd, dst,
		      flags);
	if (res == -1)
		return -errno;

	// do not report event handler errors after successful rename op
	(void)send_notify_event(mask, 0, src, dst);
	return 0;
}

static int projfs_op_opendir(char const *path, struct fuse_file_info *fi)
{
	int flags = O_DIRECTORY | O_NOFOLLOW | O_RDONLY;
	struct projfs_dir *d;
	int fd;
	int res = 0;
	int err = 0;

	path = make_relative_path(path);
	res = project_dir("opendir", path, 1);
	if (res)
		return -res;
	res = project_dir("opendir2", path, 0);
	if (res)
		return -res;

	d = calloc(1, sizeof(*d));
	if (!d) {
		res = -1;
		goto out;
	}

	fd = openat(get_fuse_context_lowerdir_fd(), path, flags);
	if (fd == -1) {
		res = -1;
		goto out_free;
	}

	d->dir = fdopendir(fd);
	if (!d->dir) {
		res = -1;
		err = errno;
		goto out_close;
	}

	fi->fh = (uintptr_t)d;
	goto out;

out_close:
	close(fd);	// report fopendir() error and ignore any from close()
out_free:
	free(d);
out:
	return res == -1 ? -(err > 0 ? err : errno) : res;
}

static int projfs_op_readdir(char const *path, void *buf,
                             fuse_fill_dir_t filler, off_t off,
                             struct fuse_file_info *fi,
                             enum fuse_readdir_flags flags)
{
	int err = 0;
	struct projfs_dir *d = (struct projfs_dir *)fi->fh;

	(void)path;

	if (off != d->loc) {
		seekdir(d->dir, off);
		d->ent = NULL;
		d->loc = off;
	}

	while (1) {
		struct stat attr;
		enum fuse_fill_dir_flags filled = 0;

		if (!d->ent) {
			errno = 0;
			d->ent = readdir(d->dir);
			if (!d->ent) {
				err = errno;
				break;
			}
		}

		if (flags & FUSE_READDIR_PLUS) {
			int res = fstatat(
					dirfd(d->dir), d->ent->d_name, &attr,
					AT_SYMLINK_NOFOLLOW);
			// TODO: break and report errors from fstatat()?
			if (res != -1)
				filled = FUSE_FILL_DIR_PLUS;
		}
		if (filled == 0) {
			memset(&attr, 0, sizeof(attr));
			attr.st_ino = d->ent->d_ino;
			attr.st_mode = d->ent->d_type << 12;
		}

		if (filler(buf, d->ent->d_name, &attr, d->ent->d_off, filled))
			break;

		d->loc = d->ent->d_off;
		d->ent = NULL;
	}

	return -err;
}

static int projfs_op_releasedir(char const *path, struct fuse_file_info *fi)
{
	struct projfs_dir *d = (struct projfs_dir *)fi->fh;
	int res = closedir(d->dir);

	(void)path;
	free(d);
	// return value is ignored by libfuse, but be consistent anyway
	return res == -1 ? -errno : 0;
}

static int projfs_op_chmod(char const *path, mode_t mode,
                           struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = fchmod(fi->fh, mode);
	else {
		path = make_relative_path(path);
		res = project_dir("chmod", path, 1);
		if (res)
			return -res;
		res = fchmodat(get_fuse_context_lowerdir_fd(), path, mode, 0);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_chown(char const *path, uid_t uid, gid_t gid,
                           struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = fchown(fi->fh, uid, gid);
	else {
		path = make_relative_path(path);
		res = project_dir("chown", path, 1);
		if (res)
			return -res;
		// disallow chown() on lowerdir itself, so no AT_EMPTY_PATH
		res = fchownat(get_fuse_context_lowerdir_fd(), path, uid, gid,
			       AT_SYMLINK_NOFOLLOW);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_truncate(char const *path, off_t off,
                              struct fuse_file_info *fi)
{
	int res, err = 0;
	if (fi)
		res = ftruncate(fi->fh, off);
	else {
		int fd;

		path = make_relative_path(path);
		res = project_dir("truncate", path, 1);
		if (res)
			return -res;
		// convert to fully local file before truncating
		res = project_file("truncate", path, PROJ_STATE_MODIFIED);
		if (res)
			return -res;

		fd = openat(get_fuse_context_lowerdir_fd(), path, O_WRONLY);
		if (fd == -1) {
			res = -1;
			goto out;
		}
		res = ftruncate(fd, off);
		if (res == -1)
			err = errno;
		// report error from close() unless prior ftruncate() error
		if (close(fd) == -1)
			res = -1;
	}
out:
	return res == -1 ? -(err > 0 ? err : errno) : 0;
}

static int projfs_op_utimens(char const *path, const struct timespec tv[2],
                             struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = futimens(fi->fh, tv);
	else {
		path = make_relative_path(path);
		res = project_dir("utimens", path, 1);
		if (res)
			return -res;
		res = utimensat(get_fuse_context_lowerdir_fd(), path, tv,
				AT_SYMLINK_NOFOLLOW);
	}
	return res == -1 ? -errno : 0;
}


static int projfs_op_setxattr(char const *path, char const *name,
                              char const *value, size_t size, int flags)
{
	int res = -1;
	int err = 0;
	int fd;

	if (xattr_name_has_prefix(name))
		return -EPERM;

	path = make_relative_path(path);
	res = project_dir("setxattr", path, 1);
	if (res)
		return -res;

	fd = openat(get_fuse_context_lowerdir_fd(), path,
		    O_WRONLY | O_NONBLOCK);
	if (fd == -1)
		goto out;
	res = fsetxattr(fd, name, value, size, flags);
	if (res == -1)
		err = errno;
	// report error from close() unless prior fsetxattr() error
	if (close(fd) == -1)
		res = -1;
out:
	return res == -1 ? -(err > 0 ? err : errno) : 0;
}

static int projfs_op_getxattr(char const *path, char const *name,
                              char *value, size_t size)
{
	ssize_t res = -1;
	int err = 0;
	int fd;

	path = make_relative_path(path);
	res = project_dir("getxattr", path, 1);
	if (res)
		return -res;

	fd = openat(get_fuse_context_lowerdir_fd(), path,
		    O_RDONLY | O_NONBLOCK);
	if (fd == -1)
		goto out;
	res = fgetxattr(fd, name, value, size);
	if (res == -1)
		err = errno;
	// report error from close() unless prior fgetxattr() error
	if (close(fd) == -1)
		res = -1;
out:
	return res == -1 ? -(err > 0 ? err : errno) : res;
}

static int projfs_op_listxattr(char const *path, char *list, size_t size)
{
	ssize_t res = -1;
	int err = 0;
	int fd;

	path = make_relative_path(path);
	res = project_dir("listxattr", path, 1);
	if (res)
		return -res;

	fd = openat(get_fuse_context_lowerdir_fd(), path,
		    O_RDONLY | O_NONBLOCK);
	if (fd == -1)
		goto out;
	res = flistxattr(fd, list, size);
	if (res == -1)
		err = errno;
	// report error from close() unless prior flistxattr() error
	if (close(fd) == -1)
		res = -1;
out:
	return res == -1 ? -(err > 0 ? err : errno) : res;
}

static int projfs_op_removexattr(char const *path, char const *name)
{
	int res = -1;
	int err = 0;
	int fd;

	if (xattr_name_has_prefix(name))
		return -EPERM;

	path = make_relative_path(path);
	res = project_dir("removexattr", path, 1);
	if (res)
		return -res;

	fd = openat(get_fuse_context_lowerdir_fd(), path,
		    O_WRONLY | O_NONBLOCK);
	if (fd == -1)
		goto out;
	res = fremovexattr(fd, name);
	if (res == -1)
		err = errno;
	// report error from close() unless prior fremovexattr() error
	if (close(fd) == -1)
		res = -1;
out:
	return res == -1 ? -(err > 0 ? err : errno) : 0;
}

static int projfs_op_access(char const *path, int mode)
{
	int res;

	path = make_relative_path(path);
	res = project_dir("access", path, 1);
	if (res)
		return -res;
	res = faccessat(get_fuse_context_lowerdir_fd(), path, mode,
			AT_SYMLINK_NOFOLLOW);
	return res == -1 ? -errno : 0;
}

static int projfs_op_flock(char const *path, struct fuse_file_info *fi, int op)
{
	int res = flock(fi->fh, op);

	(void)path;
	return res == -1 ? -errno : 0;
}

static int projfs_op_fallocate(char const *path, int mode, off_t off,
                               off_t len, struct fuse_file_info *fi)
{
	(void)path;
	if (mode)
		return -EOPNOTSUPP;
	return -posix_fallocate(fi->fh, off, len);
}

static struct fuse_operations projfs_ops = {
	.getattr	= projfs_op_getattr,
	.readlink	= projfs_op_readlink,
	.mknod		= projfs_op_mknod,
	.mkdir		= projfs_op_mkdir,
	.unlink		= projfs_op_unlink,
	.rmdir		= projfs_op_rmdir,
	.symlink	= projfs_op_symlink,
	.rename		= projfs_op_rename,
	.link		= projfs_op_link,
	.chmod		= projfs_op_chmod,
	.chown		= projfs_op_chown,
	.truncate	= projfs_op_truncate,
	.open		= projfs_op_open,
	.statfs		= projfs_op_statfs,
	.flush		= projfs_op_flush,
	.release	= projfs_op_release,
	.fsync		= projfs_op_fsync,
	.setxattr	= projfs_op_setxattr,
	.getxattr	= projfs_op_getxattr,
	.listxattr	= projfs_op_listxattr,
	.removexattr	= projfs_op_removexattr,
	.opendir	= projfs_op_opendir,
	.readdir	= projfs_op_readdir,
	.releasedir	= projfs_op_releasedir,
	.init		= projfs_op_init,
	.access		= projfs_op_access,
	.create		= projfs_op_create,
	.utimens	= projfs_op_utimens,
	.write_buf	= projfs_op_write_buf,
	.read_buf	= projfs_op_read_buf,
	.flock		= projfs_op_flock,
	.fallocate	= projfs_op_fallocate,
	// copy_file_range
};

static void projfs_set_session(struct projfs *fs, struct fuse_session *se)
{
	if (fs == NULL)
		return;

	pthread_mutex_lock(&fs->mutex);
	fs->session = se;
	pthread_mutex_unlock(&fs->mutex);
}

struct projfs *projfs_new(const char *lowerdir, const char *mountdir,
		const struct projfs_handlers *handlers,
		size_t handlers_size, void *user_data,
		int argc, const char **argv)
{
	struct projfs *fs = NULL;
	size_t len;
	int i;

	// TODO: prevent failure with relative lowerdir
	if (lowerdir == NULL) {
		log_printf(fs, LOG_STDERR_ONLY, "no lowerdir specified");
		goto out;
	}

	// TODO: debug failure to exit when given a relative mountdir
	if (mountdir == NULL) {
		log_printf(fs, LOG_STDERR_ONLY, "no mountdir specified");
		goto out;
	}

	if (sizeof(struct projfs_handlers) < handlers_size) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "warning: library too old, some handlers "
			   "may be ignored");
		handlers_size = sizeof(struct projfs_handlers);
	}

	fs = calloc(1, sizeof(struct projfs));
	if (fs == NULL) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "failed to allocate projfs object");
		goto out;
	}

	fs->lowerdir = strdup(lowerdir);
	if (fs->lowerdir == NULL) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "failed to allocate lower path");
		goto out_handle;
	}
	len = strlen(fs->lowerdir);
	if (len && fs->lowerdir[len - 1] == '/')
		fs->lowerdir[len - 1] = 0;

	fs->mountdir = strdup(mountdir);
	if (fs->mountdir == NULL) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "failed to allocate mount path");
		goto out_lower;
	}
	len = strlen(fs->mountdir);
	if (len && fs->mountdir[len - 1] == '/')
		fs->mountdir[len - 1] = 0;

	if (handlers != NULL)
		memcpy(&fs->handlers, handlers, handlers_size);

	fs->user_data = user_data;

	if (pthread_mutex_init(&fs->mutex, NULL) > 0)
		goto out_mount;

	fs->fdtable = fdtable_create();
	if (fs->fdtable == NULL) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "failed to allocate file descriptor table");
		goto out_mutex;
	}

	if (fuse_opt_add_arg(&fs->args, "projfs") != 0) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "failed to allocate argument");
		goto out_fdtable;
	}

	for (i = 0; i < argc; ++i) {
		if (fuse_opt_add_arg(&fs->args, argv[i]) != 0) {
			log_printf(fs, LOG_STDERR_ONLY,
				   "failed to allocate argument");
			goto out_fdtable;
		}
	}

	if (fuse_opt_parse(&fs->args, &fs->config, projfs_opts, NULL) == -1) {
		log_printf(fs, LOG_STDERR_ONLY,
			   "unable to parse arguments");
		goto out_fdtable;
	}

	return fs;

out_fdtable:
	fuse_opt_free_args(&fs->args);
	fdtable_destroy(fs->fdtable);

out_mutex:
	pthread_mutex_destroy(&fs->mutex);
out_mount:
	free(fs->mountdir);
out_lower:
	free(fs->lowerdir);
out_handle:
	free(fs);
out:
	return NULL;
}

void *projfs_get_user_data(struct projfs *fs)
{
	return fs->user_data;
}

#define SPARSE_TEST_FILENAME ".libprojfs-sparse-test"
#define SPARSE_TEST_SIZE_BYTES 1048576

/**
 * Test that using ftruncate to create a sparse file works.
 *
 * @return 1 if sparse files function as expected, 0 if not, -1 if an error
 * occurred during testing (in which case errno is set).
 */
static int test_sparse_support(int lowerdir_fd)
{
	struct stat attrs;
	int fd, res;

	fd = openat(lowerdir_fd, SPARSE_TEST_FILENAME,
		    (O_CREAT | O_TRUNC | O_RDWR), 0600);
	if (fd == -1)
		return -1;

	res = fstat(fd, &attrs);
	if (res == -1)
		goto err_close;

	if (attrs.st_size != 0) {
		errno = EINVAL;
		goto err_close;
	}

	res = ftruncate(fd, SPARSE_TEST_SIZE_BYTES);
	if (res == -1)
		goto err_close;

	res = fstat(fd, &attrs);
	if (res == -1)
		goto err_close;

	if (attrs.st_size != SPARSE_TEST_SIZE_BYTES) {
		errno = EINVAL;
		goto err_close;
	}

	res = attrs.st_blocks == 0;

	goto close;

err_close:
	res = -1;

close:
	close(fd);
	unlinkat(lowerdir_fd, SPARSE_TEST_FILENAME, 0);

	return res;
}

static void *projfs_loop(void *data)
{
	struct projfs *fs = (struct projfs *)data;
	struct fuse_loop_config loop;
	struct fuse *fuse;
	struct fuse_session *se;
	int res = 0;
	int err;

	// TODO: verify the way we're setting signal handlers on the underlying
	// session works correctly when using the high-level API

	/* open lower directory file descriptor to resolve relative paths
	 * in file ops
	 */
	fs->lowerdir_fd = open(fs->lowerdir,
			       O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fs->lowerdir_fd == -1) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "failed to open lowerdir: %s: %s",
			   fs->lowerdir, strerror(errno));
		res = 1;
		goto out;
	}

	if (get_proj_state_xattr(fs->lowerdir_fd) == PROJ_STATE_ERROR &&
	    errno == ENOTSUP) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "xattr support check on lowerdir failed: %s: %s",
			fs->lowerdir, strerror(errno));
		res = 2;
		goto out_close;
	}

	res = test_sparse_support(fs->lowerdir_fd);
	if (res == -1) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "unable to test sparse file support: %s/%s: %s",
			   fs->lowerdir, SPARSE_TEST_FILENAME,
			   strerror(errno));
		res = 3;
		goto out_close;
	} else if (res == 0) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "sparse files may not be supported by "
			   "lower filesystem: %s", fs->lowerdir);
	} else if (res == 1) {
		res = 0;
	}

	if (fs->config.initial == 1) {
		if (set_proj_state_xattr(fs->lowerdir_fd,
					 PROJ_STATE_EMPTY, 0) == -1) {
			log_printf(fs, LOG_STDERR_FALLBACK,
				   "could not set projection flag "
				   "xattr: %s: %s",
				   fs->lowerdir, strerror(errno));
			res = 4;
			goto out_close;
		}
	}

	fuse = fuse_new(&fs->args, &projfs_ops, sizeof(projfs_ops), fs);
	if (fuse == NULL) {
		res = 5;
		goto out_close;
	}

	se = fuse_get_session(fuse);
	projfs_set_session(fs, se);

	// TODO: defer all signal handling to user, once we remove FUSE
	if (fuse_set_signal_handlers(se) != 0) {
		res = 6;
		goto out_session;
	}

	// TODO: mount with x-gvfs-hide option and maybe others for KDE, etc.
	if (fuse_mount(fuse, fs->mountdir) != 0) {
		res = 7;
		goto out_signal;
	}

	// TODO: support configs; ideally libfuse's full suite
	loop.clone_fd = 0;
	loop.max_idle_threads = 10;

	// TODO: output strsignal() only for dev purposes
	if ((err = fuse_loop_mt(fuse, &loop)) != 0) {
		if (err > 0) {
			log_printf(fs, LOG_STDERR_FALLBACK, "%s signal",
				   strsignal(err));
		}
		res = 8;
	}

	fuse_session_unmount(se);
out_signal:
	fuse_remove_signal_handlers(se);
out_session:
	projfs_set_session(fs, NULL);
	fuse_session_destroy(se);
out_close:
	if (close(fs->lowerdir_fd) == -1) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "failed to close lowerdir: %s: %s",
			   fs->lowerdir, strerror(errno));
	}
	fs->lowerdir_fd = 0;
out:
	fs->error = res;

	pthread_exit(NULL);
}

int projfs_start(struct projfs *fs)
{
	sigset_t oldset;
	sigset_t newset;
	pthread_t thread_id;
	int res;

	if (log_open(fs) != 0)
		return -1;

	// TODO: override stack size per fuse_start_thread()?

	// TODO: defer all signal handling to user, once we remove FUSE
	sigemptyset(&newset);
	sigaddset(&newset, SIGTERM);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGHUP);
	sigaddset(&newset, SIGQUIT);
	// TODO: handle error from pthread_sigmask()
	pthread_sigmask(SIG_BLOCK, &newset, &oldset);

	res = pthread_create(&thread_id, NULL, projfs_loop, fs);

	// TODO: report error from pthread_sigmask() but don't return -1
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);

	if (res != 0) {
		log_printf(fs, LOG_STDERR_FALLBACK,
			   "error creating thread: %s", strerror(res));
		goto out_close;
	}

	fs->thread_id = thread_id;
	return 0;

out_close:
	log_close(fs);
	return -1;
}

void *projfs_stop(struct projfs *fs)
{
	struct stat buf;
	void *user_data;

	pthread_mutex_lock(&fs->mutex);
	if (fs->session != NULL)
		fuse_session_exit(fs->session);
	pthread_mutex_unlock(&fs->mutex);
	// TODO: barrier/fence to ensure all CPUs see exit flag?

	/* could send a USR1 signal and have a no-op handler installed by
	 * projfs_loop(), but this is a simpler way to trigger fuse_do_work()
	 * to exit, which semaphores fuse_session_loop_mt() to exit as well;
	 * we can ignore any errors
	 */
	stat(fs->mountdir, &buf);

	// TODO: use pthread_tryjoin_np() in a loop if avail (AC_CHECK_FUNCS)
	if (fs->thread_id) {
		pthread_join(fs->thread_id, NULL);
	}

	if (fs->error > 0) {
		// TODO: translate projfs_loop() codes into messages
		log_printf(fs, LOG_STDERR_ONLY, "error from event loop: %d",
			   fs->error);
	}

	log_close(fs);

	fuse_opt_free_args(&fs->args);

	fdtable_destroy(fs->fdtable);

	pthread_mutex_destroy(&fs->mutex);

	free(fs->mountdir);
	free(fs->lowerdir);

	user_data = fs->user_data;
	free(fs);
	return user_data;
}

static int check_safe_rel_path(const char *path)
{
	const char *s = path;
	const char *t;

	if (path == NULL || *path == '/')
		return 0;

	while ((s = strstr(s, "..")) != NULL) {
		t = s + 2;
		if ((*t == '\0' || *t == '/') &&
		    (s == path || *(s - 1) == '/'))
			return 0;
		s += 2;
	}

	return 1;
}

static char *make_user_xattr_name(const char *segments)
{
	char *name;

	name = malloc(PROJ_XATTR_PRE_LEN + strlen(segments) + 1);
	if (name == NULL)
		return NULL;

	memcpy(name, PROJ_XATTR_PRE_NAME, PROJ_XATTR_PRE_LEN);
	strcpy(name + PROJ_XATTR_PRE_LEN, segments);

	return name;
}

#define PROJ_XATTR_READ 0x00
#define PROJ_XATTR_WRITE 0x01
#define PROJ_XATTR_CREATE 0x02

static int iter_user_xattrs(int fd, struct projfs_attr *attrs,
			    unsigned int nattrs, unsigned int flags)
{
	int set_flags = (flags & PROJ_XATTR_CREATE) ? XATTR_CREATE : 0;
	int res;
	unsigned int i;

	if (attrs == NULL)
		return 0;

	for (i = 0; i < nattrs; i++) {
		char *name;
		struct projfs_attr *attr = &attrs[i];

		name = make_user_xattr_name(attr->name);
		if (name == NULL)
			return errno;

		if (flags & PROJ_XATTR_WRITE) {
			// do not permit alteration of our reserved xattrs
			if (xattr_name_is_reserved(name)) {
				errno = EPERM;
				res = -1;
			} else {
				res = set_xattr(fd, name,
						attr->value, &attr->size,
						set_flags);
			}
		} else {
			res = get_xattr(fd, name, attr->value, &attr->size);
		}

		free(name);
		if (res == -1)
			return errno;
	}

	return 0;
}

int projfs_create_proj_dir(struct projfs *fs, const char *path, mode_t mode,
			   struct projfs_attr *attrs, unsigned int nattrs)
{
	int fd, res;

	if (!check_safe_rel_path(path))
		return EINVAL;

	if (mkdirat(fs->lowerdir_fd, path, mode) == -1)
		return errno;

	fd = openat(fs->lowerdir_fd, path,
		    O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd == -1)
		return errno;

	if (set_proj_state_xattr(fd, PROJ_STATE_EMPTY, XATTR_CREATE) == -1) {
		res = errno;
		goto out_close;
	}

	res = iter_user_xattrs(fd, attrs, nattrs,
			       PROJ_XATTR_WRITE | PROJ_XATTR_CREATE);

out_close:
	close(fd);
	return res;
}

int projfs_create_proj_file(struct projfs *fs, const char *path, off_t size,
			    mode_t mode, struct projfs_attr *attrs,
			    unsigned int nattrs)
{
	int fd, res;

	if (!check_safe_rel_path(path))
		return EINVAL;

	fd = openat(fs->lowerdir_fd, path, O_WRONLY | O_CREAT | O_EXCL, mode);
	if (fd == -1)
		return errno;

	if (ftruncate(fd, size) == -1) {
		res = errno;
		goto out_close;
	}

	if (set_proj_state_xattr(fd, PROJ_STATE_EMPTY, XATTR_CREATE) == -1) {
		res = errno;
		goto out_close;
	}

	res = iter_user_xattrs(fd, attrs, nattrs,
			       PROJ_XATTR_WRITE | PROJ_XATTR_CREATE);

out_close:
	close(fd);
	if (res > 0)
		unlinkat(fs->lowerdir_fd, path, 0);	// best effort
	return res;
}

int projfs_create_proj_symlink(struct projfs *fs, const char *path,
			       const char *target)
{
	int res;

	if (!check_safe_rel_path(path))
		return EINVAL;

	res = symlinkat(target, fs->lowerdir_fd, path);
	if (res == -1)
		return errno;

	return 0;
}

static int iter_attrs(struct projfs *fs, const char *path,
		      struct projfs_attr *attrs, unsigned int nattrs,
		      unsigned int flags)
{
	int fd, res;
	struct stat st;

	if (!check_safe_rel_path(path) || attrs == NULL)
		return EINVAL;

	if (nattrs == 0)
		return 0;

	fd = openat(fs->lowerdir_fd, path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	if (fd == -1)
		return errno;

	if (fstat(fd, &st) == -1) {
		res = errno;
		goto out_close;
	}

	if (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))
		res = iter_user_xattrs(fd, attrs, nattrs, flags);
	else
		res = EPERM;

out_close:
	close(fd);
	return res;
}

int projfs_get_attrs(struct projfs *fs, const char *path,
		     struct projfs_attr *attrs, unsigned int nattrs)
{
	return iter_attrs(fs, path, attrs, nattrs, PROJ_XATTR_READ);
}

int projfs_set_attrs(struct projfs *fs, const char *path,
		     struct projfs_attr *attrs, unsigned int nattrs)
{
	return iter_attrs(fs, path, attrs, nattrs, PROJ_XATTR_WRITE);
}
