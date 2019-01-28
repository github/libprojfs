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
#include <sys/xattr.h>
#include <unistd.h>

#include "projfs.h"
#include "projfs_i.h"

#include <fuse3/fuse.h>

#ifdef PROJFS_VFSAPI
#include "projfs_vfsapi.h"
#endif

#define lowerdir_fd() (projfs_context_fs()->lowerdir_fd)

struct projfs_dir {
	DIR *dir;
	long loc;
	struct dirent *ent;
};

static struct projfs *projfs_context_fs(void)
{
	return (struct projfs *)fuse_get_context()->private_data;
}

static int projfs_fuse_send_event(projfs_handler_t handler,
                                  uint64_t mask,
                                  const char *path,
                                  const char *target_path,
                                  int perm)
{
	if (handler == NULL)
		return 0;

	struct projfs_event event;
	event.mask = mask;
	event.pid = fuse_get_context()->pid;
	event.user_data = projfs_context_fs()->user_data;
	event.path = path + 1;
	event.target_path = target_path ? (target_path + 1) : NULL;

	int err = handler(&event);
	if (err < 0) {
		fprintf(stderr, "projfs: event handler failed: %s; "
		                "event mask 0x%04" PRIx64 "-%08" PRIx64 ", "
		                "pid %d\n",
		        strerror(-err), mask >> 32, mask & 0xFFFFFFFF,
		        event.pid);
	}
	else if (perm)
		err = (err == PROJFS_ALLOW) ? 0 : -EPERM;

	return err;
}

static int projfs_fuse_notify_event(uint64_t mask,
                                    const char *path,
                                    const char *target_path)
{
	projfs_handler_t handler =
		projfs_context_fs()->handlers.handle_notify_event;

	return projfs_fuse_send_event(
		handler, mask, path, target_path, 0);
}

static int projfs_fuse_perm_event(uint64_t mask,
                                  const char *path,
                                  const char *target_path)
{
	projfs_handler_t handler =
		projfs_context_fs()->handlers.handle_perm_event;

	return projfs_fuse_send_event(
		handler, mask, path, target_path, 1);
}

static const char *lower_path(char const *path)
{
	if (*(++path) == '\0') {
		struct projfs *fs = projfs_context_fs();

		return fs->lowerdir;
	} else
		return path;
}

// filesystem ops

static const char *dotpath = ".";

static inline const char *lowerpath(const char *path)
{
	while (*path == '/')
		++path;
	if (*path == '\0')
		path = dotpath;
	return path;
}

static int projfs_op_getattr(char const *path, struct stat *attr,
                             struct fuse_file_info *fi)
{
	(void)fi;
	int res;
	if (fi)
		res = fstat(fi->fh, attr);
	else
		res = fstatat(lowerdir_fd(), lowerpath(path), attr,
			      AT_SYMLINK_NOFOLLOW);
	return res == -1 ? -errno : 0;
}

static int projfs_op_readlink(char const *path, char *buf, size_t size)
{
	int res = readlinkat(lowerdir_fd(), lowerpath(path), buf, size - 1);
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
	int res = linkat(lowerdir_fd, lowerpath(src),
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

	return projfs_context_fs();
}

static int projfs_op_flush(char const *path, struct fuse_file_info *fi)
{
	(void)path;
	int res = close(dup(fi->fh));
	return res == -1 ? -errno : 0;
}

static int projfs_op_fsync(char const *path, int datasync,
                           struct fuse_file_info *fi)
{
	(void)path;
	int res;
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mknod(char const *path, mode_t mode, dev_t rdev)
{
	int res;
	if (S_ISFIFO(mode))
		res = mkfifoat(lowerdir_fd(), lowerpath(path), mode);
	else
		res = mknodat(lowerdir_fd(), lowerpath(path), mode, rdev);
	return res == -1 ? -errno : 0;
}

static int projfs_op_symlink(char const *link, char const *path)
{
	int res = symlinkat(link, lowerdir_fd(), lowerpath(path));
	return res == -1 ? -errno : 0;
}

static int projfs_op_create(char const *path, mode_t mode,
                            struct fuse_file_info *fi)
{
	int flags = fi->flags & ~O_NOFOLLOW;
	int fd = openat(lowerdir_fd(), lowerpath(path), flags, mode);

	if (fd == -1)
		return -errno;
	fi->fh = fd;

	int res = projfs_fuse_notify_event(
		PROJFS_CREATE_SELF,
		path,
		NULL);
	return res;
}

static int projfs_op_open(char const *path, struct fuse_file_info *fi)
{
	int flags = fi->flags & ~O_NOFOLLOW;
	int fd = openat(lowerdir_fd(), lowerpath(path), flags);

	if (fd == -1)
		return -errno;
	fi->fh = fd;
	return 0;
}

static int projfs_op_statfs(char const *path, struct statvfs *buf)
{
	const char *lower = lower_path(path);
	int res = statvfs(lower, buf);
	return res == -1 ? -errno : 0;
}

static int projfs_op_read_buf(char const *path, struct fuse_bufvec **bufp, size_t size,
                          off_t off, struct fuse_file_info *fi)
{
	(void) path;

	struct fuse_bufvec *src = malloc(sizeof(*src));
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
	(void)path;
	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(fuse_buf_size(src));
	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = off;

	return fuse_buf_copy(&buf, src, FUSE_BUF_SPLICE_NONBLOCK);
}

static int projfs_op_release(char const *path, struct fuse_file_info *fi)
{
	(void)path;
	int res = close(fi->fh);
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

	res = unlinkat(lowerdir_fd(), lowerpath(path), 0);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mkdir(char const *path, mode_t mode)
{
	int res = mkdirat(lowerdir_fd(), lowerpath(path), mode);
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

	res = unlinkat(lowerdir_fd(), lowerpath(path), AT_REMOVEDIR);
	return res == -1 ? -errno : 0;
}

static int projfs_op_rename(char const *src, char const *dst,
                            unsigned int flags)
{
	const char *lower_src = lower_path(src);
	const char *lower_dst = lower_path(dst);

	int res = syscall(
		SYS_renameat2,
		0,
		lower_src,
		0,
		lower_dst,
		flags);
	return res == -1 ? -errno : 0;
}

static int projfs_op_opendir(char const *path, struct fuse_file_info *fi)
{
	struct projfs_dir *d = calloc(1, sizeof(*d));
	if (!d)
		return -ENOMEM;

	const char *lower = lower_path(path);

	d->dir = opendir(lower);
	if (!d->dir) {
		free(d);
		return -errno;
	}

	fi->fh = (uintptr_t)d;
	return 0;
}

static int projfs_op_readdir(char const *path, void *buf,
                             fuse_fill_dir_t filler, off_t off,
                             struct fuse_file_info *fi,
                             enum fuse_readdir_flags flags)
{
	(void)path;

	struct projfs_dir *d = (struct projfs_dir *)fi->fh;

	if (off != d->loc) {
		seekdir(d->dir, off);
		d->ent = NULL;
		d->loc = off;
	}

	while (1) {
		if (!d->ent) {
			errno = 0;
			d->ent = readdir(d->dir);
			if (!d->ent)
				break;
		}

		struct stat attr;

		enum fuse_fill_dir_flags filled = 0;
		if (flags & FUSE_READDIR_PLUS) {
			int res = fstatat(
					dirfd(d->dir), d->ent->d_name, &attr,
					AT_SYMLINK_NOFOLLOW);
			if (res != -1)
				filled = FUSE_FILL_DIR_PLUS;
		}
		if (filled == 0) {
			memset(&attr, 0, sizeof(attr));
			attr.st_ino = d->ent->d_ino,
				attr.st_mode = d->ent->d_type << 12;
		}

		if (filler(buf, d->ent->d_name, &attr, d->ent->d_off, filled))
			break;

		d->loc = d->ent->d_off;
		d->ent = NULL;
	}

	return -errno;
}

static int projfs_op_releasedir(char const *path, struct fuse_file_info *fi)
{
	(void)path;

	struct projfs_dir *d = (struct projfs_dir *)fi->fh;
	int res = closedir(d->dir);
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
	else
		// TODO: as AT_SYMLINK_NOFOLLOW is not implemented,
		//       should we open(..., O_NOFOLLOW) and then fchmod()?
		res = fchmodat(lowerdir_fd(), lowerpath(path), mode, 0);
	return res == -1 ? -errno : 0;
}

static int projfs_op_chown(char const *path, uid_t uid, gid_t gid,
                           struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = fchown(fi->fh, uid, gid);
	else
		// disallow chown() on lowerdir itself, so no AT_EMPTY_PATH
		res = fchownat(lowerdir_fd(), lowerpath(path), uid, gid,
			       AT_SYMLINK_NOFOLLOW);
	return res == -1 ? -errno : 0;
}

static int projfs_op_truncate(char const *path, off_t off,
                              struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = ftruncate(fi->fh, off);
	else {
		const char *lower = lower_path(path);

		res = truncate(lower, off);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_utimens(char const *path, const struct timespec tv[2],
                             struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = futimens(fi->fh, tv);
	else
		res = utimensat(lowerdir_fd(), lowerpath(path), tv,
				AT_SYMLINK_NOFOLLOW);
	return res == -1 ? -errno : 0;
}

static int projfs_op_setxattr(char const *path, char const *name,
                              char const *value, size_t size, int flags)
{
	const char *lower = lower_path(path);
	int res = lsetxattr(lower, name, value, size, flags);
	return res == -1 ? -errno : 0;
}

static int projfs_op_getxattr(char const *path, char const *name,
                              char *value, size_t size)
{
	const char *lower = lower_path(path);
	ssize_t res = lgetxattr(lower, name, value, size);
	return res == -1 ? -errno : 0;
}

static int projfs_op_listxattr(char const *path, char *list, size_t size)
{
	const char *lower = lower_path(path);
	ssize_t res = llistxattr(lower, list, size);
	return res == -1 ? -errno : 0;
}

static int projfs_op_removexattr(char const *path, char const *name)
{
	const char *lower = lower_path(path);
	int res = removexattr(lower, name);
	return res == -1 ? -errno : 0;
}

static int projfs_op_access(char const *path, int mode)
{
	const char *lower = lower_path(path);
	int res = faccessat(AT_FDCWD, lower, mode, AT_SYMLINK_NOFOLLOW);
	return res == -1 ? -errno : 0;
}

static int projfs_op_flock(char const *path, struct fuse_file_info *fi, int op)
{
	(void)path;
	int res = flock(fi->fh, op);
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

	fs = (struct projfs *)calloc(1, sizeof(struct projfs));
	if (fs == NULL) {
		fprintf(stderr, "projfs: failed to allocate projfs object\n");
		goto out;
	}

	fs->lowerdir = strdup(lowerdir);
	if (fs->lowerdir == NULL) {
		fprintf(stderr, "projfs: failed to allocate lower path\n");
		goto out_handle;
	}
	int len = strlen(fs->lowerdir);
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

	pthread_mutex_init(&fs->mutex, NULL);

	return fs;

out_lower:
	free(fs->lowerdir);
out_handle:
	free(fs);
out:
	return NULL;
}

static void *projfs_loop(void *data)
{
	struct projfs *fs = (struct projfs *)data;
	const char *argv[] = { "projfs", DEBUG_ARGV NULL };
	int argc = 1 + DEBUG_ARGC;
	struct fuse_args args = FUSE_ARGS_INIT(argc, (char **)argv);
	struct fuse_loop_config loop;
	int res = 0;

	// TODO: verify the way we're setting signal handlers on the underlying
	// session works correctly when using the high-level API

	/* open lower directory file descriptor to resolve relative paths
	 * in file ops
	 */
	fs->lowerdir_fd = open(fs->lowerdir, O_DIRECTORY | O_NOFOLLOW
							 | O_PATH);
	if (fs->lowerdir_fd == -1) {
		fprintf(stderr, "projfs: failed to open lowerdir: %s: %s\n",
			fs->lowerdir, strerror(errno));
		res = 1;
		goto out;
	}

	struct fuse *fuse =
		fuse_new(&args, &projfs_ops, sizeof(projfs_ops), fs);
	if (fuse == NULL) {
		res = 2;
		goto out_close;
	}

	struct fuse_session *se = fuse_get_session(fuse);
	projfs_set_session(fs, se);

	// TODO: defer all signal handling to user, once we remove FUSE
	if (fuse_set_signal_handlers(se) != 0) {
		res = 3;
		goto out_session;
	}

	// TODO: mount with x-gvfs-hide option and maybe others for KDE, etc.
	if (fuse_mount(fuse, fs->mountdir) != 0) {
		res = 4;
		goto out_signal;
	}

	// change to lower directory so relative paths resolve in file ops
	if (chdir(fs->lowerdir) < 0) {
		res = 5;
		goto out_unmount;
	}

	// TODO: support configs; ideally libfuse's full suite
	loop.clone_fd = 0;
	loop.max_idle_threads = 10;

	// TODO: output strsignal() only for dev purposes
	int err;
	if ((err = fuse_loop_mt(fuse, &loop)) != 0) {
		if (err > 0)
			fprintf(stderr, "projfs: %s signal\n", strsignal(err));
		res = 6;
	}

out_unmount:
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
