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
   License along with this library, in the file COPYING.LIB; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include <config.h>

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // TODO: remove unless using strsignal()
#include <errno.h>
#include <unistd.h>

#include <fuse3/fuse_opt.h>

#include "projfs.h"
#include "projfs_i.h"

#ifdef PROJFS_VFSAPI
#include "projfs_vfsapi.h"
#endif

static struct projfs *req_fs(fuse_req_t req)
{
	return (struct projfs *)fuse_req_userdata(req);
}

static struct projfs_node *ino_node(fuse_req_t req, fuse_ino_t ino)
{
	if (ino == FUSE_ROOT_ID)
		return &req_fs(req)->root;

	return (struct projfs_node *)ino;
}

static struct projfs_node *find_node(struct projfs *fs, ino_t ino, dev_t dev)
{
	struct projfs_node *node = &fs->root;
	pthread_mutex_lock(&fs->mutex);
	while (node != NULL) {
		if (node->ino == ino && node->dev == dev) {
			break;
		}
		node = node->next;
	}
	pthread_mutex_unlock(&fs->mutex);
	return node;
}

static int lookup_param(fuse_req_t req, fuse_ino_t parent, char const *name,
                        struct fuse_entry_param *e)
{

	int err;
	struct projfs_node *node;
	struct projfs *fs = req_fs(req);

	memset(e, 0, sizeof(*e));
	e->attr_timeout = 1.0;
	e->entry_timeout = 1.0;

	int fd = openat(ino_node(req, parent)->fd, name, O_PATH | O_NOFOLLOW);
	if (fd == -1) {
		return errno;
	}

	err = fstatat(fd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (err == -1) {
		err = errno;
		close(fd);
		return err;
	}

	node = find_node(fs, e->attr.st_ino, e->attr.st_dev);
	if (node) {
		close(fd);
		e->ino = (uintptr_t)node;
		return 0;
	}

	node = calloc(1, sizeof(*node));
	if (!node) {
		close(fd);
		return ENOMEM;
	}

	e->ino = (uintptr_t)node;
	node->fd = fd;
	node->ino = e->attr.st_ino;
	node->dev = e->attr.st_dev;

	pthread_mutex_lock(&fs->mutex);
	node->next = fs->root.next;
	fs->root.next = node;
	if (node->next)
		node->next->prev = node;
	node->prev = &fs->root;
	pthread_mutex_unlock(&fs->mutex);
	
	return 0;
}

static void projfs_ll_lookup(fuse_req_t req, fuse_ino_t parent,
                             const char *name)
{
	struct fuse_entry_param e;
	int res = lookup_param(req, parent, name, &e);
	if (res == 0)
		fuse_reply_entry(req, &e);
	else
		fuse_reply_err(req, res);
}

static void projfs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
                              struct fuse_file_info *fi)
{
	int fd = ino_node(req, ino)->fd;
	int res;
	struct stat attr;

	(void)fi;

	res = fstatat(fd, "", &attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	fuse_reply_attr(req, &attr, 1.0);
}

static void projfs_ll_open(fuse_req_t req, fuse_ino_t ino,
                           struct fuse_file_info *fi)
{
	int fd = ino_node(req, ino)->fd;
	char path[sizeof("/proc/self/fd/") + sizeof(int)*3];
	sprintf(path, "/proc/self/fd/%d", fd);
	fd = open(path, fi->flags & ~O_NOFOLLOW);
	if (fd == -1)
		return (void)fuse_reply_err(req, errno);

	fi->fh = fd;
	fuse_reply_open(req, fi);
}

static void projfs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                           off_t off, struct fuse_file_info *fi)
{
	(void)ino;

	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);
	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = off;
	fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
}

static void projfs_ll_release(fuse_req_t req, fuse_ino_t ino,
                              struct fuse_file_info *fi)
{
	(void)ino;
	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void projfs_ll_mkdir(fuse_req_t req, fuse_ino_t parent,
                            char const *name, mode_t mode)
{
	struct fuse_entry_param e;
	int res = mkdirat(ino_node(req, parent)->fd, name, mode);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);
	res = lookup_param(req, parent, name, &e);
	if (res == 0)
		fuse_reply_entry(req, &e);
	else
		fuse_reply_err(req, res);
}

static void projfs_ll_opendir(fuse_req_t req, fuse_ino_t ino,
                              struct fuse_file_info *fi)
{
	int err, fd;

	struct projfs_dir *d = calloc(1, sizeof(*d));
	if (!d)
		return (void)fuse_reply_err(req, errno);

	fd = openat(ino_node(req, ino)->fd, ".", O_RDONLY | O_DIRECTORY);
	if (fd == -1) {
		return (void)fuse_reply_err(req, errno);
	}

	d->dir = fdopendir(fd);
	if (!d->dir) {
		err = errno;
		close(fd);
		free(d);
		return (void)fuse_reply_err(req, err);
	}

	fi->fh = (uintptr_t)d;
	fuse_reply_open(req, fi);
}

static void projfs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                              off_t off, struct fuse_file_info *fi)
{
	(void)ino;

	struct projfs_dir *d = (struct projfs_dir *)fi->fh;
	char *buf, *p;
	size_t rem = size, entsize;

	buf = calloc(1, size);
	if (!buf)
		return (void)fuse_reply_err(req, errno);
	p = buf;

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

		struct stat attr = {
			.st_ino  = d->ent->d_ino,
			.st_mode = d->ent->d_type << 12,
		};
		entsize = fuse_add_direntry(req, p, rem, d->ent->d_name,
		                            &attr, d->ent->d_off);

		if (entsize > rem) {
			errno = 0;
			break;
		}

		p += entsize;
		rem -= entsize;

		d->loc = d->ent->d_off;
		d->ent = NULL;
	}

	if (errno && rem == size)
		fuse_reply_err(req, errno);
	else
		fuse_reply_buf(req, buf, size - rem);
	free(buf);
}

static void projfs_ll_releasedir(fuse_req_t req, fuse_ino_t ino,
                                 struct fuse_file_info *fi)
{
	(void)ino;
	struct projfs_dir *d = (struct projfs_dir *)fi->fh;
	closedir(d->dir);
	free(d);
	fuse_reply_err(req, 0);
}

static struct fuse_lowlevel_ops ll_ops = {
	.lookup		= projfs_ll_lookup,
	.getattr	= projfs_ll_getattr,
// 	.statfs		= projfs_ll_statfs,
// 	.setattr	= projfs_ll_setattr,
// 	.flush		= projfs_ll_flush,
// 	.fsync		= projfs_ll_fsync,
// 	.forget		= projfs_ll_forget,
// 	.forget_multi	= projfs_ll_forget_multi,
// 	.create		= projfs_ll_create,
	.open		= projfs_ll_open,
	.read		= projfs_ll_read,
// 	.write		= projfs_ll_write,
	.release	= projfs_ll_release,
// 	.unlink		= projfs_ll_unlink,
	.mkdir		= projfs_ll_mkdir,
// 	.rmdir		= projfs_ll_rmdir,
	.opendir	= projfs_ll_opendir,
	.readdir	= projfs_ll_readdir,
	.releasedir	= projfs_ll_releasedir
};

#ifdef PROJFS_DEBUG
#define DEBUG_ARGV "--debug",
#define DEBUG_ARGC 1
#else
#define DEBUG_ARGV
#define DEBUG_ARGC 0
#endif

void projfs_set_session(struct projfs *fs, struct fuse_session *se)
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

	fs->root.fd = open(lowerdir, O_PATH);
	if (fs->root.fd == -1) {
		fprintf(stderr, "projfs: failed to open lowerdir\n");
		goto out_handle;
	}

	fs->lowerdir = strdup(lowerdir);
	if (fs->lowerdir == NULL) {
		fprintf(stderr, "projfs: failed to allocate lower path\n");
		goto out_fd;
	}

	fs->mountdir = strdup(mountdir);
	if (fs->mountdir == NULL) {
		fprintf(stderr, "projfs: failed to allocate mount path\n");
		goto out_lower;
	}

	if (handlers != NULL)
		memcpy(&fs->handlers, handlers, handlers_size);

	fs->user_data = user_data;

	pthread_mutex_init(&fs->mutex, NULL);

	return fs;

out_lower:
	free(fs->lowerdir);
out_fd:
	close(fs->root.fd);
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
	struct fuse_session *se;
	struct fuse_loop_config loop;
	int err;
	int res = 0;

	// TODO: pass a real userdata structure here, not fs
	se = fuse_session_new(&args, &ll_ops, sizeof(ll_ops), fs);
	if (se == NULL) {
		res = 1;
		goto out;
	}

	projfs_set_session(fs, se);

	// TODO: defer all signal handling to user, once we remove FUSE
	if (fuse_set_signal_handlers(se) != 0) {
		res = 2;
		goto out_session;
	}

	// TODO: mount with x-gvfs-hide option and maybe others for KDE, etc.
	if (fuse_session_mount(se, fs->mountdir) != 0) {
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
	if ((err = fuse_session_loop_mt(se, &loop)) != 0)
	{
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

