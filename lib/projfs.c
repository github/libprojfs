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

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "projfs.h"
#include "projfs_i.h"

#include <fuse3/fuse.h>

#ifdef PROJFS_VFSAPI
#include "projfs_vfsapi.h"
#endif

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

static void *projfs_op_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void)conn;

	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	return projfs_context_fs();
}

static char *lower_path(char const *path)
{
	// lower never ends in "/", path always starts with "/"
	struct projfs *fs = projfs_context_fs();
	int lower_len = strlen(fs->lowerdir);
	int path_len = strlen(path);
	char *res = malloc(lower_len + path_len + 1);
	if (!res)
		return NULL;
	strcpy(res, fs->lowerdir);
	strcat(res, path);
	return res;
}

static int projfs_op_getattr(char const *path, struct stat *attr,
                             struct fuse_file_info *fi)
{
	(void)fi;
	int res;
	if (fi)
		res = fstat(fi->fh, attr);
	else {
		char *lower = lower_path(path);
		if (lower == NULL)
			return -errno;
		res = lstat(lower, attr);
		free(lower);
	}
	return res == -1 ? -errno : 0;
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
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	if (S_ISFIFO(mode))
		res = mkfifo(lower, mode);
	else
		res = mknod(lower, mode, rdev);
	free(lower);
	return res == -1 ? -errno : 0;
}

static int projfs_op_symlink(char const *link, char const *name)
{
	char *lower_link = lower_path(link);
	if (!lower_link)
		return -errno;
	char *lower_name = lower_path(name);
	if (!lower_name) {
		free(lower_link);
		return -errno;
	}
	int res = symlink(lower_link, lower_name);
	free(lower_name);
	free(lower_link);
	return res == -1 ? -errno : 0;
}

static int projfs_op_create(char const *path, mode_t mode,
                            struct fuse_file_info *fi)
{
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	int fd = open(lower, fi->flags, mode);
	free(lower);
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
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	int fd = open(lower, fi->flags);
	free(lower);
	if (fd == -1)
		return -errno;
	fi->fh = fd;
	return 0;
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
	close(fi->fh);
	return 0;
}

static int projfs_op_unlink(char const *path)
{
	int res = projfs_fuse_perm_event(
		PROJFS_DELETE_SELF,
		path,
		NULL);
	if (res < 0)
		return res;
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	res = unlink(lower);
	free(lower);
	return res == -1 ? -errno : 0;
}

static int projfs_op_mkdir(char const *path, mode_t mode)
{
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	int res = mkdir(lower, mode);
	free(lower);
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
	char *lower = lower_path(path);
	if (!lower)
		return -errno;
	res = rmdir(lower);
	free(lower);
	return res == -1 ? -errno : 0;
}

static int projfs_op_rename(char const *src, char const *dst,
                            unsigned int flags)
{
	char *lower_src = lower_path(src);
	if (!lower_src)
		return -errno;
	char *lower_dst = lower_path(dst);
	if (!lower_dst) {
		free(lower_src);
		return -errno;
	}
	int res = syscall(
		SYS_renameat2,
		0,
		lower_src,
		0,
		lower_dst,
		flags);
	free(lower_dst);
	free(lower_src);
	return res == -1 ? -errno : 0;
}

static int projfs_op_opendir(char const *path, struct fuse_file_info *fi)
{
	struct projfs_dir *d = calloc(1, sizeof(*d));
	if (!d)
		return -ENOMEM;

	char *lower = lower_path(path);
	if (!lower) {
		free(d);
		return -errno;
	}

	d->dir = opendir(lower);
	free(lower);
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
	closedir(d->dir);
	free(d);
	return 0;
}

static int projfs_op_chmod(char const *path, mode_t mode,
                           struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = fchmod(fi->fh, mode);
	else {
		char *lower = lower_path(path);
		if (!lower)
			return -errno;
		res = chmod(lower, mode);
		free(lower);
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
		char *lower = lower_path(path);
		if (!lower)
			return -errno;
		res = chown(lower, uid, gid);
		free(lower);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_truncate(char const *path, off_t off,
                              struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = ftruncate(fi->fh, off);
	else {
		char *lower = lower_path(path);
		if (!lower)
			return -errno;
		res = truncate(lower, off);
		free(lower);
	}
	return res == -1 ? -errno : 0;
}

static int projfs_op_utimens(char const *path, const struct timespec tv[2],
                             struct fuse_file_info *fi)
{
	int res;
	if (fi)
		res = futimens(fi->fh, tv);
	else {
		char *lower = lower_path(path);
		if (!lower)
			return -errno;
		res = utimensat(0, lower, tv, AT_SYMLINK_NOFOLLOW);
		free(lower);
	}
	return res == -1 ? -errno : 0;
}


static struct fuse_operations projfs_ops = {
	.getattr	= projfs_op_getattr,
	// readlink
	.mknod		= projfs_op_mknod,
	.mkdir		= projfs_op_mkdir,
	.unlink		= projfs_op_unlink,
	.rmdir		= projfs_op_rmdir,
	.symlink	= projfs_op_symlink,
	.rename		= projfs_op_rename,
	// link
	.chmod		= projfs_op_chmod,
	.chown		= projfs_op_chown,
	.truncate	= projfs_op_truncate,
	.open		= projfs_op_open,
	// read
	// write
	// statfs
	.flush		= projfs_op_flush,
	.release	= projfs_op_release,
	.fsync		= projfs_op_fsync,
	// setxattr
	// getxattr
	// listxattr
	// removexattr
	.opendir	= projfs_op_opendir,
	.readdir	= projfs_op_readdir,
	.releasedir	= projfs_op_releasedir,
	// fsyncdir
	.init		= projfs_op_init,
	// destroy
	// access
	.create		= projfs_op_create,
	// lock
	.utimens	= projfs_op_utimens,
	// bmap
	// ioctl
	// poll
	.write_buf	= projfs_op_write_buf,
	.read_buf	= projfs_op_read_buf,
	// flock
	// fallocate
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

	struct fuse *fuse =
		fuse_new(&args, &projfs_ops, sizeof(projfs_ops), fs);
	if (fuse == NULL) {
		res = 1;
		goto out;
	}

	struct fuse_session *se = fuse_get_session(fuse);
	projfs_set_session(fs, se);

	// TODO: defer all signal handling to user, once we remove FUSE
	if (fuse_set_signal_handlers(se) != 0) {
		res = 2;
		goto out_session;
	}

	// TODO: mount with x-gvfs-hide option and maybe others for KDE, etc.
	if (fuse_mount(fuse, fs->mountdir) != 0) {
		res = 3;
		goto out_signal;
	}

	// since we're running in a thread, don't daemonize, just chdir()
	if (fuse_daemonize(1)) {
		res = 4;
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
		res = 5;
	}

out_unmount:
	fuse_session_unmount(se);
out_signal:
	fuse_remove_signal_handlers(se);
out_session:
	projfs_set_session(fs, NULL);
	fuse_session_destroy(se);
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
