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

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <attr/xattr.h>
#include <unistd.h>

#include "projfs.h"
#include "projfs_i.h"

#include <fuse3/fuse.h>

#ifdef PROJFS_VFSAPI
#include "projfs_vfsapi.h"
#endif

#define lowerdir_fd() (projfs_context_fs()->lowerdir_fd)
#define PROJ_DIR_MODE 0777

// TODO: make this value configurable
#define PROJ_WAIT_MSEC 5000

struct projfs_dir {
	DIR *dir;
	long loc;
	struct dirent *ent;
};

static struct projfs *projfs_context_fs(void)
{
	return (struct projfs *)fuse_get_context()->private_data;
}

/**
 * @return 0 or a negative errno
 */
static int projfs_fuse_send_event(projfs_handler_t handler,
                                  uint64_t mask,
                                  const char *path,
                                  const char *target_path,
                                  int fd,
                                  int perm)
{
	struct projfs_event event;
	int err;

	if (handler == NULL)
		return 0;

	event.fs = projfs_context_fs();
	event.mask = mask;
	event.pid = fuse_get_context()->pid;
	event.path = path;
	event.target_path = target_path ? (target_path + 1) : NULL;
	event.fd = fd;

	err = handler(&event);
	if (err < 0) {
		fprintf(stderr, "projfs: event handler failed: %s; "
		                "event mask 0x%04" PRIx64 "-%08" PRIx64 ", "
		                "pid %d\n",
		        strerror(-err), mask >> 32, mask & 0xFFFFFFFF,
		        event.pid);
	}
	else if (!fd && perm)
		err = (err == PROJFS_ALLOW) ? 0 : -EPERM;

	return err;
}

/**
 * @return 0 or a negative errno
 */
static int projfs_fuse_proj_event(uint64_t mask,
                                  const char *path,
                                  int fd)
{
	projfs_handler_t handler =
		projfs_context_fs()->handlers.handle_proj_event;

	return projfs_fuse_send_event(
		handler, mask, path, NULL, fd, 0);
}

/**
 * @return 0 or a negative errno
 */
static int projfs_fuse_notify_event(uint64_t mask,
                                    const char *path,
                                    const char *target_path)
{
	projfs_handler_t handler =
		projfs_context_fs()->handlers.handle_notify_event;

	return projfs_fuse_send_event(
		handler, mask, path + 1, target_path, 0, 0);
}

/**
 * @return 0 or a negative errno
 */
static int projfs_fuse_perm_event(uint64_t mask,
                                  const char *path,
                                  const char *target_path)
{
	projfs_handler_t handler =
		projfs_context_fs()->handlers.handle_perm_event;

	return projfs_fuse_send_event(
		handler, mask, path + 1, target_path, 0, 1);
}

struct node_userdata
{
	int fd;

	uint8_t proj_flag; // USER_PROJECTION_EMPTY
};

#define USER_PROJECTION_EMPTY "user.projection.empty"
#define USER_PROJECTION_TEST  "user.projection.test"

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
 * Acquires a lock on path and populates the supplied node_userdata argument
 * with the open and locked fd, and proj_flag based on the
 * USER_PROJECTION_EMPTY xattr.
 *
 * @param user userdata to fill out (zeroed by this function)
 * @param path path relative to lowerdir to lock and open
 * @param mode filemode to open path with; the fd populated in user will have
 *             this mode
 * @return 0 or an errno
 */
static int get_path_userdata(struct node_userdata *user,
			     const char *path,
			     mode_t mode)
{
	ssize_t sz;
	int err, wait_ms;
	struct timespec ts;

	memset(user, 0, sizeof(*user));

	user->fd = openat(lowerdir_fd(), path, mode);
	if (user->fd == -1)
		return errno;

	wait_ms = PROJ_WAIT_MSEC;

retry_flock:
	err = flock(user->fd, LOCK_EX | LOCK_NB);
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
		close(user->fd);
		return err;
	}

	// fill cache from xattrs
	sz = fgetxattr(user->fd, USER_PROJECTION_EMPTY, NULL, 0);
	err = errno;
	if (sz == -1 && err != ENOATTR) {
		close(user->fd);
		return err;
	}
	user->proj_flag = sz > 0;

	return 0;
}

/**
 * Closes the open fd associated with user, which in turn releases any locks
 * associated with the fd.
 *
 * @param user userdata to clean up
 */
static void finalize_userdata(struct node_userdata *user)
{
	if (user->fd == -1)
		return;

	close(user->fd);
	user->fd = -1;
}

/**
 * Projects a path by notifying the provider with the given event mask.  If the
 * provider succeeds, clears the projection flag on the path.
 *
 * @param event_mask the event mask passed up to the provider
 * @param user userdata collected from get_path_userdata
 * @param path the path the userdata was collected for
 * @return 0 or an errno
 */
static int projfs_fuse_proj_locked(uint64_t event_mask,
				   struct node_userdata *user,
				   const char *path)
{
	int res = projfs_fuse_proj_event(event_mask, path, user->fd);

	if (res < 0)
		return -res;

	res = fremovexattr(user->fd, USER_PROJECTION_EMPTY);
	if (res == 0 || (res == -1 && errno == ENOATTR))
		user->proj_flag = 0;
	else
		return errno;

	return 0;
}

/**
 * Project a directory. Takes the lower path, and a flag indicating whether the
 * directory is the parent of the path, or the path itself.
 *
 * @param op op name (for debugging)
 * @param path the lower path (from lowerpath)
 * @param parent 1 if we should look at the parent directory containing path, 0
 *               if we look at path itself
 * @return 0 or an errno
 */
static int projfs_fuse_proj_dir(const char *op, const char *path, int parent)
{
	struct node_userdata user;
	int res;
	char *target_path;

	(void)op;

	if (parent)
		target_path = get_path_parent(path);
	else
		target_path = strdup(path);
	if (!target_path)
		return errno;

	res = get_path_userdata(&user, target_path, O_RDONLY | O_DIRECTORY);
	if (res != 0)
		goto out;

	if (!user.proj_flag)
		goto out_finalize;

	/* pass mapped path (i.e. containing directory we want to project) to
	 * provider */
	res = projfs_fuse_proj_locked(
		PROJFS_CREATE_SELF | PROJFS_ONDIR, &user, target_path);

out_finalize:
	finalize_userdata(&user);

out:
	free(target_path);

	return res;
}

/**
 * Project a file. Takes the lower path.
 *
 * @param op op name (for debugging)
 * @param path the lower path (from lowerpath)
 * @return 0 or an errno
 */
static int projfs_fuse_proj_file(const char *op, const char *path)
{
	struct node_userdata user;
	int res;

	(void)op;

	res = get_path_userdata(&user, path, O_RDWR | O_NOFOLLOW);
	if (res == EISDIR) {
		/* tried to project a directory as a file, ignore
		 * XXX should we just always project dirs as dirs and files as
		 * files? */
		res = 0;
		goto out;
	} else if (res == ELOOP) {
		/* tried to project a symlink. it already exists as a symlink
		 * on disk so we have nothing to do */
		res = 0;
		goto out;
	} else if (res != 0)
		goto out;

	if (!user.proj_flag)
		goto out_finalize;

	/* `path` relative to lowerdir exists and is a placeholder -- hydrate it */
	res = projfs_fuse_proj_locked(PROJFS_CREATE_SELF, &user, path);

out_finalize:
	finalize_userdata(&user);

out:
	return res;
}

static const char *dotpath = ".";

/**
 * Makes a path from FUSE usable as a relative path to lowerdir_fd.  Removes
 * any leading forward slashes.  If the resulting path is empty, returns ".".
 * */
static inline const char *lowerpath(const char *path)
{
	while (*path == '/')
		++path;
	if (*path == '\0')
		path = dotpath;
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
		res = projfs_fuse_proj_dir("getattr", lowerpath(path), 1);
		if (res)
			return -res;
		res = fstatat(lowerdir_fd(), lowerpath(path), attr,
			      AT_SYMLINK_NOFOLLOW);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_readlink(char const *path, char *buf, size_t size)
{
	int res = projfs_fuse_proj_dir("readlink", lowerpath(path), 1);
	if (res)
		return -res;
	res = readlinkat(lowerdir_fd(), lowerpath(path), buf, size - 1);
	if (res == -1)
		return -errno;
	buf[res] = 0;
	return 0;
}

static int projfs_op_link(char const *src, char const *dst)
{
	int lowerdir_fd = lowerdir_fd();

	/* NOTE: We require lowerdir to be a directory, so this should
	 *       fail when src is an empty path, as we expect.
	 */
	int res = projfs_fuse_proj_dir("link", lowerpath(src), 1);
	if (res)
		return -res;

	/* hydrate the source file before adding a hard link to it, otherwise
	 * a user could access the newly created link and end up modifying the
	 * non-hydrated placeholder */
	res = projfs_fuse_proj_file("link", lowerpath(src));
	if (res)
		return -res;

	res = projfs_fuse_proj_dir("link2", lowerpath(dst), 1);
	if (res)
		return -res;

	res = linkat(lowerdir_fd, lowerpath(src),
	             lowerdir_fd, lowerpath(dst), 0);
	return res == -1 ? -errno : 0;
}

static void *projfs_op_init(struct fuse_conn_info *conn,
                            struct fuse_config *cfg)
{
	(void)conn;

	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;
	cfg->use_ino = 1;

	return projfs_context_fs();
}

static int projfs_op_flush(char const *path, struct fuse_file_info *fi)
{
	int res = close(dup(fi->fh));

	(void)path;
	return res == -1 ? -errno : 0;
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

	res = projfs_fuse_proj_dir("mknod", lowerpath(path), 1);
	if (res)
		return -res;
	if (S_ISFIFO(mode))
		res = mkfifoat(lowerdir_fd(), lowerpath(path), mode);
	else
		return -ENOSYS;
	return res == -1 ? -errno : 0;
}

static int projfs_op_symlink(char const *link, char const *path)
{
	int res =  projfs_fuse_proj_dir("symlink", lowerpath(path), 1);
	if (res)
		return -res;
	res = symlinkat(link, lowerdir_fd(), lowerpath(path));
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

	res = projfs_fuse_proj_dir("create", lowerpath(path), 1);
	if (res)
		return -res;
	res = projfs_fuse_proj_file("create", lowerpath(path));
	if (res && res != ENOENT)
		return -res;
	fd = openat(lowerdir_fd(), lowerpath(path), flags, mode);

	if (fd == -1)
		return -errno;
	fi->fh = fd;

	res = projfs_fuse_notify_event(
		PROJFS_CREATE_SELF,
		path,
		NULL);
	return res;
}

static int projfs_op_open(char const *path, struct fuse_file_info *fi)
{
	int flags = fi->flags & ~O_NOFOLLOW;
	int res;
	int fd;

	res = projfs_fuse_proj_dir("open", lowerpath(path), 1);
	if (res)
		return -res;

	/* Per above, allow hydration to fail with ENOENT; if the file
	 * operation should fail for that reason (i.e. O_CREAT is not specified
	 * and the file doesn't exist), we'll return the failure from openat(2)
	 * below */
	res = projfs_fuse_proj_file("open", lowerpath(path));
	if (res && res != ENOENT)
		return -res;

	fd = openat(lowerdir_fd(), lowerpath(path), flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int projfs_op_statfs(char const *path, struct statvfs *buf)
{
	int res;

	(void)path;
	// TODO: should we return our own filesystem's global info?
	res = fstatvfs(lowerdir_fd(), buf);
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
	int res = close(fi->fh);

	(void)path;
	// return value is ignored by libfuse, but be consistent anyway
	return res == -1 ? -errno : 0;
}

static int projfs_op_unlink(char const *path)
{
	int res = projfs_fuse_perm_event(
		PROJFS_DELETE_SELF,
		path,
		NULL);
	if (res < 0)
		return res;
	res = projfs_fuse_proj_dir("unlink", lowerpath(path), 1);
	if (res)
		return -res;

	res = unlinkat(lowerdir_fd(), lowerpath(path), 0);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mkdir(char const *path, mode_t mode)
{
	int res = projfs_fuse_proj_dir("mkdir", lowerpath(path), 1);
	if (res)
		return -res;
	res = mkdirat(lowerdir_fd(), lowerpath(path), mode);
	if (res == -1)
		return -errno;

	res = projfs_fuse_notify_event(
		PROJFS_CREATE_SELF | PROJFS_ONDIR,
		path,
		NULL);
	return res;
}

static int projfs_op_rmdir(char const *path)
{
	int res = projfs_fuse_perm_event(
		PROJFS_DELETE_SELF | PROJFS_ONDIR,
		path,
		NULL);
	if (res < 0)
		return res;
	res = projfs_fuse_proj_dir("rmdir", lowerpath(path), 1);
	if (res)
		return -res;

	res = unlinkat(lowerdir_fd(), lowerpath(path), AT_REMOVEDIR);
	return res == -1 ? -errno : 0;
}

static int projfs_op_rename(char const *src, char const *dst,
                            unsigned int flags)
{
	int res = projfs_fuse_proj_dir("rename", lowerpath(src), 1);
	if (res)
		return -res;
	res = projfs_fuse_proj_file("rename", lowerpath(src));
	if (res)
		return -res;
	res = projfs_fuse_proj_dir("rename2", lowerpath(dst), 1);
	if (res)
		return -res;
	// TODO: may prevent us compiling on BSD; would renameat() suffice?
	res = syscall(
		SYS_renameat2,
		lowerdir_fd(),
		lowerpath(src),
		lowerdir_fd(),
		lowerpath(dst),
		flags);
	return res == -1 ? -errno : 0;
}

static int projfs_op_opendir(char const *path, struct fuse_file_info *fi)
{
	int flags = O_DIRECTORY | O_NOFOLLOW | O_RDONLY;
	struct projfs_dir *d;
	int fd;
	int res = 0;
	int err = 0;

	res = projfs_fuse_proj_dir("opendir", lowerpath(path), 1);
	if (res)
		return -res;
	res = projfs_fuse_proj_dir("opendir2", lowerpath(path), 0);
	if (res)
		return -res;

	d = calloc(1, sizeof(*d));
	if (!d) {
		res = -1;
		goto out;
	}

	fd = openat(lowerdir_fd(), lowerpath(path), flags);
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
		res = projfs_fuse_proj_dir("chmod", lowerpath(path), 1);
		if (res)
			return -res;
		res = fchmodat(lowerdir_fd(), lowerpath(path), mode, 0);
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
		res = projfs_fuse_proj_dir("chown", lowerpath(path), 1);
		if (res)
			return -res;
		// disallow chown() on lowerdir itself, so no AT_EMPTY_PATH
		res = fchownat(lowerdir_fd(), lowerpath(path), uid, gid,
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

		res = projfs_fuse_proj_dir("truncate", lowerpath(path), 1);
		if (res)
			return -res;
		res = projfs_fuse_proj_file("truncate", lowerpath(path));
		if (res)
			return -res;

		fd = openat(lowerdir_fd(), lowerpath(path),
				O_WRONLY);
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
		res = projfs_fuse_proj_dir("utimens", lowerpath(path), 1);
		if (res)
			return -res;
		res = utimensat(lowerdir_fd(), lowerpath(path), tv,
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

	path = lowerpath(path);

	// TODO: filter user.projection.* attrs

	res = projfs_fuse_proj_dir("setxattr", path, 1);
	if (res)
		return -res;

	fd = openat(lowerdir_fd(), path, O_WRONLY | O_NONBLOCK);
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

	path = lowerpath(path);

	// TODO: filter user.projection.* attrs

	res = projfs_fuse_proj_dir("getxattr", path, 1);
	if (res)
		return -res;

	fd = openat(lowerdir_fd(), path, O_RDONLY | O_NONBLOCK);
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

	path = lowerpath(path);

	// TODO: filter user.projection.* attrs

	res = projfs_fuse_proj_dir("listxattr", path, 1);
	if (res)
		return -res;

	fd = openat(lowerdir_fd(), path, O_RDONLY | O_NONBLOCK);
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

	path = lowerpath(path);

	// TODO: filter user.projection.* attrs

	res = projfs_fuse_proj_dir("removexattr", path, 1);
	if (res)
		return -res;

	fd = openat(lowerdir_fd(), path, O_WRONLY | O_NONBLOCK);
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
	int res = projfs_fuse_proj_dir("access", lowerpath(path), 1);
	if (res)
		return -res;
	res = faccessat(lowerdir_fd(), lowerpath(path), mode,
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

#ifdef PROJFS_DEBUG
#define DEBUG_ARGV "--debug",
#define DEBUG_ARGC 1
#else
#define DEBUG_ARGV
#define DEBUG_ARGC 0
#endif

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
		size_t handlers_size, void *user_data)
{
	struct projfs *fs;
	size_t len;

	// TODO: prevent failure with relative lowerdir
	if (lowerdir == NULL) {
		fprintf(stderr, "projfs: no lowerdir specified\n");
		goto out;
	}

	// TODO: debug failure to exit when given a relative mountdir
	if (mountdir == NULL) {
		fprintf(stderr, "projfs: no mountdir specified\n");
		goto out;
	}

	if (sizeof(struct projfs_handlers) < handlers_size) {
		fprintf(stderr, "projfs: warning: library too old, "
				"some handlers may be ignored\n");
		handlers_size = sizeof(struct projfs_handlers);
	}

	fs = calloc(1, sizeof(struct projfs));
	if (fs == NULL) {
		fprintf(stderr, "projfs: failed to allocate projfs object\n");
		goto out;
	}

	fs->lowerdir = strdup(lowerdir);
	if (fs->lowerdir == NULL) {
		fprintf(stderr, "projfs: failed to allocate lower path\n");
		goto out_handle;
	}
	len = strlen(fs->lowerdir);
	if (len && fs->lowerdir[len - 1] == '/')
		fs->lowerdir[len - 1] = 0;

	fs->mountdir = strdup(mountdir);
	if (fs->mountdir == NULL) {
		fprintf(stderr, "projfs: failed to allocate mount path\n");
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

	return fs;

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

/**
 * @return 1 if dir is empty, 0 if not, -1 if an error occurred (errno set)
 */
static int check_dir_empty(const char *path)
{
	int err, is_empty = 1;
	struct dirent *e;
	DIR *d = opendir(path);
	if (!d)
		return -1;

	while (1) {
		errno = 0;
		e = readdir(d);
		if (e == NULL) {
			err = errno;
			closedir(d);
			if (err == 0)
				return is_empty;
			errno = err;
			return -1;
		}

		if (strcmp(e->d_name, ".") == 0 ||
				strcmp(e->d_name, "..") == 0)
			; /* ignore */
		else
			is_empty = 0;
	}
}

static void *projfs_loop(void *data)
{
	struct projfs *fs = (struct projfs *)data;
	const char *argv[] = { "projfs", DEBUG_ARGV NULL };
	int argc = 1 + DEBUG_ARGC;
	struct fuse_args args = FUSE_ARGS_INIT(argc, (char **)argv);
	struct fuse_loop_config loop;
	struct fuse *fuse;
	struct fuse_session *se;
	char v = 1;
	int res = 0;
	int err;

	// TODO: verify the way we're setting signal handlers on the underlying
	// session works correctly when using the high-level API

	/* open lower directory file descriptor to resolve relative paths
	 * in file ops
	 */
	fs->lowerdir_fd = open(fs->lowerdir, O_DIRECTORY | O_NOFOLLOW);
	if (fs->lowerdir_fd == -1) {
		fprintf(stderr, "projfs: failed to open lowerdir: %s: %s\n",
			fs->lowerdir, strerror(errno));
		res = 1;
		goto out;
	}

	/* mark lowerdir as needing projection if it's empty (because the
	 * provider creates it for us before we start running) -- probably want
	 * to modify this behaviour in the future */
	res = check_dir_empty(fs->lowerdir);
	if (res == -1) {
		res = 2;
		goto out_close;
	}

	/* if the dir is empty, mark as such, and test that the xattr was
	 * actually set.  if not, set and check a test xattr, so that in either
	 * case we can be sure the lower fs supports xattrs.
	 * exit code 3 can be summarised as "bad or no xattr support" */
	if (res == 1) {
		res = fsetxattr(
			fs->lowerdir_fd, USER_PROJECTION_EMPTY, &v, 1, 0);
		if (res == -1) {
			res = 3;
			goto out_close;
		}

		res = fgetxattr(fs->lowerdir_fd, USER_PROJECTION_EMPTY, &v, 1);
		if (res != 1 || v != 1) {
			res = 3;
			goto out_close;
		}

		res = 0;
	} else {
		res = fsetxattr(
			fs->lowerdir_fd, USER_PROJECTION_TEST, &v, 1, 0);
		if (res == -1) {
			res = 3;
			goto out_close;
		}

		res = fgetxattr(fs->lowerdir_fd, USER_PROJECTION_TEST, &v, 1);
		if (res != 1 || v != 1) {
			res = 3;
			goto out_close;
		}

		res = fremovexattr(fs->lowerdir_fd, USER_PROJECTION_TEST);
		if (res != 0) {
			res = 3;
			goto out_close;
		}
	}

	fuse = fuse_new(&args, &projfs_ops, sizeof(projfs_ops), fs);
	if (fuse == NULL) {
		res = 4;
		goto out_close;
	}

	se = fuse_get_session(fuse);
	projfs_set_session(fs, se);

	// TODO: defer all signal handling to user, once we remove FUSE
	if (fuse_set_signal_handlers(se) != 0) {
		res = 5;
		goto out_session;
	}

	// TODO: mount with x-gvfs-hide option and maybe others for KDE, etc.
	if (fuse_mount(fuse, fs->mountdir) != 0) {
		res = 6;
		goto out_signal;
	}

	// TODO: support configs; ideally libfuse's full suite
	loop.clone_fd = 0;
	loop.max_idle_threads = 10;

	// TODO: output strsignal() only for dev purposes
	if ((err = fuse_loop_mt(fuse, &loop)) != 0) {
		if (err > 0)
			fprintf(stderr, "projfs: %s signal\n", strsignal(err));
		res = 7;
	}

	fuse_session_unmount(se);
out_signal:
	fuse_remove_signal_handlers(se);
out_session:
	projfs_set_session(fs, NULL);
	fuse_session_destroy(se);
out_close:
	if (close(fs->lowerdir_fd) == -1)
		fprintf(stderr, "projfs: failed to close lowerdir: %s: %s\n",
			fs->lowerdir, strerror(errno));
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
		fprintf(stderr, "projfs: error creating thread: %s\n",
		        strerror(res));
		return -1;
	}

	fs->thread_id = thread_id;

	return 0;
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
		fprintf(stderr, "projfs: error from event loop: %d\n",
		        fs->error);
	}

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

int projfs_create_proj_dir(struct projfs *fs, const char *path)
{
	if (!check_safe_rel_path(path))
		return EINVAL;

	return _projfs_make_dir(fs, path, PROJ_DIR_MODE, 1);
}

int projfs_create_proj_file(struct projfs *fs, const char *path, off_t size,
                            mode_t mode)
{
	int fd, res;
	char v = 1;

	if (!check_safe_rel_path(path))
		return EINVAL;

	fd = openat(fs->lowerdir_fd, path, O_WRONLY | O_CREAT | O_EXCL, mode);
	if (fd == -1)
		return errno;

	res = ftruncate(fd, size);
	if (res == -1) {
		res = errno;
		close(fd);

		/* best effort */
		(void)unlinkat(fs->lowerdir_fd, path, 0);

		return res;
	}

	res = fsetxattr(fd, USER_PROJECTION_EMPTY, &v, 1, 0);
	if (res == -1) {
		res = errno;
		close(fd);

		/* best effort */
		(void)unlinkat(fs->lowerdir_fd, path, 0);

		return res;
	}

	close(fd);

	return 0;
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

int _projfs_make_dir(struct projfs *fs, const char *path, mode_t mode,
                     uint8_t proj_flag)
{
	int res;

	(void)fs;

	res = mkdirat(fs->lowerdir_fd, path, mode);
	if (res == -1)
		return errno;
	if (proj_flag) {
		char v = 1;
		int err;
		int fd;

		fd = openat(lowerdir_fd(), path, O_RDONLY);
		if (fd == -1)
			return errno;

		res = fsetxattr(fd, USER_PROJECTION_EMPTY, &v, 1, 0);
		err = errno;
		close(fd);
		if (res == -1)
			return err;
	}
	return 0;
}

