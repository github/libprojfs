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
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <inttypes.h>
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

static int projfs_fuse_send_event(fuse_req_t req,
                                  projfs_handler_t handler,
                                  uint64_t mask,
                                  const char *base_path,
                                  const char *name,
                                  const char *target_path,
                                  int fd,
                                  int perm)
{
	if (handler == NULL)
		return 0;

	const char *path;
	char *full_path = NULL;
	if (base_path) {
		full_path = malloc(strlen(base_path) + 1 + strlen(name) + 1);
		strcpy(full_path, base_path);
		strcat(full_path, "/");
		strcat(full_path, name);
		path = full_path;
	} else {
		path = name;
	}

	struct projfs_event event;
	event.fs = req_fs(req);
	event.mask = mask;
	event.pid = fuse_req_ctx(req)->pid;
	event.path = path;
	event.target_path = target_path;
	event.fd = fd;

	int err = handler(&event);
	if (err < 0) {
		fprintf(stderr, "projfs: event handler failed: %s; "
		                "event mask 0x%04" PRIx64 "-%08" PRIx64 ", "
		                "pid %d\n",
		        strerror(-err), mask >> 32, mask & 0xFFFFFFFF,
		        event.pid);
	}
	else if (!fd && perm)
		err = (err == PROJFS_ALLOW) ? 0 : -EPERM;

	if (full_path)
		free(full_path);

	return err;
}

static int projfs_fuse_proj_event(fuse_req_t req, uint64_t mask,
                                  const char *base_path,
                                  const char *name,
                                  int fd)
{
	projfs_handler_t handler =
		req_fs(req)->handlers.handle_proj_event;

	return projfs_fuse_send_event(
		req, handler, mask, base_path, name, NULL, fd, 0);
}

static int projfs_fuse_notify_event(fuse_req_t req, uint64_t mask,
                                    const char *base_path,
                                    const char *name,
                                    const char *target_path)
{
	projfs_handler_t handler =
		req_fs(req)->handlers.handle_notify_event;

	return projfs_fuse_send_event(
		req, handler, mask, base_path, name, target_path, 0, 0);
}

static int projfs_fuse_perm_event(fuse_req_t req, uint64_t mask,
                                  const char *base_path,
                                  const char *name,
                                  const char *target_path)
{
	projfs_handler_t handler =
		req_fs(req)->handlers.handle_perm_event;

	return projfs_fuse_send_event(
		req, handler, mask, base_path, name, target_path, 0, 1);
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
		if (node->ino == ino && node->dev == dev)
			break;
		node = node->next;
	}
	pthread_mutex_unlock(&fs->mutex);
	return node;
}

static int lookup_param(fuse_req_t req, fuse_ino_t parent, char const *name,
                        struct fuse_entry_param *e)
{
	struct projfs_node *parent_node = ino_node(req, parent);
	int fd = openat(parent_node->fd, name, O_PATH | O_NOFOLLOW);
	if (fd == -1)
		return errno;

	memset(e, 0, sizeof(*e));
	int err = fstatat(
		fd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (err == -1) {
		err = errno;
		close(fd);
		return err;
	}

	struct projfs *fs = req_fs(req);
	struct projfs_node *node = find_node(
		fs, e->attr.st_ino, e->attr.st_dev);
	if (node) {
		++node->nlookup;
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
	if (parent_node->path) {
		// TODO: ENOMEM check
		node->path = malloc(
			strlen(parent_node->path) + 1 + strlen(name) + 1);
		strcpy(node->path, parent_node->path);
		strcat(node->path, "/");
		strcat(node->path, name);
	} else {
		// TODO: ENOMEM check
		node->path = strdup(name);
	}

	node->ino = e->attr.st_ino;
	node->dev = e->attr.st_dev;
	node->nlookup = 1;

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
	(void)fi;

	int fd = ino_node(req, ino)->fd;

	struct stat attr;
	int res = fstatat(fd, "", &attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	fuse_reply_attr(req, &attr, 0.0);
}

static void projfs_ll_setattr(fuse_req_t req, fuse_ino_t ino,
                              struct stat *attr, int to_set,
                              struct fuse_file_info *fi)
{
	int res;
	char path[sizeof("/proc/self/fd/") + sizeof(int)*3];
	struct projfs_node *node = ino_node(req, ino);

	if (to_set & FUSE_SET_ATTR_MODE) {
		if (fi)
			res = fchmod(fi->fh, attr->st_mode);
		else {
			sprintf(path, "/proc/self/fd/%d", node->fd);
			res = chmod(path, attr->st_mode);
		}
		if (res == -1)
			return (void)fuse_reply_err(req, errno);
	}

	if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
		uid_t uid = (to_set & FUSE_SET_ATTR_UID) ?
			attr->st_uid : (uid_t)-1;
		gid_t gid = (to_set & FUSE_SET_ATTR_GID) ?
			attr->st_gid : (gid_t)-1;

		res = fchownat(node->fd, "", uid, gid,
		               AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
		if (res == -1)
			return (void)fuse_reply_err(req, errno);
	}

	if (to_set & FUSE_SET_ATTR_SIZE) {
		if (fi)
			res = ftruncate(fi->fh, attr->st_size);
		else {
			sprintf(path, "/proc/self/fd/%i", node->fd);
			res = truncate(path, attr->st_size);
		}
		if (res == -1)
			return (void)fuse_reply_err(req, errno);
	}

	if (to_set & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) {
		struct timespec times[2];

		times[0].tv_sec = 0;
		times[1].tv_sec = 0;
		times[0].tv_nsec = UTIME_OMIT;
		times[1].tv_nsec = UTIME_OMIT;

		if (to_set & FUSE_SET_ATTR_ATIME_NOW)
			times[0].tv_nsec = UTIME_NOW;
		else if (to_set & FUSE_SET_ATTR_ATIME)
			times[0] = attr->st_atim;

		if (to_set & FUSE_SET_ATTR_MTIME_NOW)
			times[1].tv_nsec = UTIME_NOW;
		else if (to_set & FUSE_SET_ATTR_MTIME)
			times[1] = attr->st_mtim;

		if (fi)
			res = futimens(fi->fh, times);
		else {
			sprintf(path, "/proc/self/fd/%i", node->fd);
			res = utimensat(0, path, times, 0);
		}

		if (res == -1)
			return (void)fuse_reply_err(req, errno);
	}

	struct stat ret;
	res = fstatat(node->fd, "", &ret, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	fuse_reply_attr(req, &ret, 0.0);
}

static void projfs_ll_flush(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info *fi)
{
	(void)ino;
	int res = close(dup(fi->fh));
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void projfs_ll_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                            struct fuse_file_info *fi)
{
	(void)ino;
	int res;
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void projfs_ll_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
	struct projfs *fs = req_fs(req);
	struct projfs_node *node = ino_node(req, ino);
	assert(node->nlookup >= nlookup);
	node->nlookup -= nlookup;
	if (node->nlookup == 0) {
		pthread_mutex_lock(&fs->mutex);
		if (node->prev)
			node->prev->next = node->next;
		if (node->next)
			node->next->prev = node->prev;
		free(node->path);
		free(node);
		pthread_mutex_unlock(&fs->mutex);
	}
	fuse_reply_none(req);
}

static void projfs_ll_forget_multi(fuse_req_t req, size_t count,
                                   struct fuse_forget_data *forgets)
{
	struct projfs *fs = req_fs(req);
	pthread_mutex_lock(&fs->mutex);
	for (size_t i = 0; i < count; ++i) {
		struct projfs_node *node = ino_node(req, forgets[i].ino);
		uint64_t nlookup = forgets[i].nlookup;
		assert(node->nlookup >= nlookup);
		node->nlookup -= nlookup;
		if (node->nlookup == 0) {
			if (node->prev)
				node->prev->next = node->next;
			if (node->next)
				node->next->prev = node->prev;
			free(node->path);
			free(node);
		}
	}
	pthread_mutex_unlock(&fs->mutex);
	fuse_reply_none(req);
}

static void projfs_ll_mknod(fuse_req_t req, fuse_ino_t parent,
                            char const *name, mode_t mode,
                            dev_t rdev)
{
	int res = mknodat(ino_node(req, parent)->fd, name, mode, rdev);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	struct fuse_entry_param e;
	res = lookup_param(req, parent, name, &e);

	if (res == 0)
		fuse_reply_entry(req, &e);
	else
		fuse_reply_err(req, res);
}

static void projfs_ll_symlink(fuse_req_t req, char const *link,
                             fuse_ino_t parent, char const *name)
{
	int res = symlinkat(link, ino_node(req, parent)->fd, name);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	struct fuse_entry_param e;
	res = lookup_param(req, parent, name, &e);

	if (res == 0)
		fuse_reply_entry(req, &e);
	else
		fuse_reply_err(req, res);
}

static void projfs_ll_create(fuse_req_t req, fuse_ino_t parent,
                             char const *name, mode_t mode,
                             struct fuse_file_info *fi)
{
	struct projfs_node *node = ino_node(req, parent);

	int fd = openat(node->fd,
	                name,
	                (fi->flags | O_CREAT) & ~O_NOFOLLOW,
	                mode);
	if (fd == -1)
		return (void)fuse_reply_err(req, errno);

	fi->fh = fd;

	struct fuse_entry_param e;
	int res = lookup_param(req, parent, name, &e);

	// XXX: do we want to notify even if res != 0 ?
	int err = projfs_fuse_notify_event(
		req,
		PROJFS_CREATE_SELF,
		node->path,
		name,
		NULL);

	if (err < 0 && res == 0)
		res = -err;

	if (res == 0)
		fuse_reply_create(req, &e, fi);
	else
		fuse_reply_err(req, res);
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

static void projfs_ll_write_buf(fuse_req_t req, fuse_ino_t ino,
                                struct fuse_bufvec *bufv, off_t off,
                                struct fuse_file_info *fi)
{
	(void)ino;

	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(fuse_buf_size(bufv));
	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = off;

	ssize_t res = fuse_buf_copy(&buf, bufv, 0);
	if (res < 0)
		fuse_reply_err(req, -res);
	else
		fuse_reply_write(req, (size_t)res);
}

static void projfs_ll_release(fuse_req_t req, fuse_ino_t ino,
                              struct fuse_file_info *fi)
{
	(void)ino;

	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void projfs_ll_unlink(fuse_req_t req, fuse_ino_t parent,
                             char const *name)
{
	struct projfs_node *node = ino_node(req, parent);
	int res = projfs_fuse_perm_event(req, PROJFS_DELETE_SELF, node->path,
	                                 name, NULL);
	if (res < 0)
		return (void)fuse_reply_err(req, -res);

	res = unlinkat(node->fd, name, 0);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void projfs_ll_mkdir(fuse_req_t req, fuse_ino_t parent,
                            char const *name, mode_t mode)
{
	struct projfs_node *node = ino_node(req, parent);
	int res = mkdirat(node->fd, name, mode);
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	struct fuse_entry_param e;
	res = lookup_param(req, parent, name, &e);

	// TODO: we're notifying even on error -- do we want to?
	int err = projfs_fuse_notify_event(
		req,
		PROJFS_CREATE_SELF | PROJFS_ONDIR,
		node->path,
		name,
		NULL);

	if (err < 0 && res == 0)
		res = -err;

	if (res == 0)
		fuse_reply_entry(req, &e);
	else
		fuse_reply_err(req, res);
}

static void projfs_ll_rmdir(fuse_req_t req, fuse_ino_t parent,
                            char const *name)
{
	struct projfs_node *node = ino_node(req, parent);
	int res = projfs_fuse_perm_event(
		req,
		PROJFS_DELETE_SELF | PROJFS_ONDIR,
		node->path,
		name,
		NULL);
	if (res < 0)
		return (void)fuse_reply_err(req, -res);
	res = unlinkat(ino_node(req, parent)->fd, name, AT_REMOVEDIR);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void projfs_ll_opendir(fuse_req_t req, fuse_ino_t ino,
                              struct fuse_file_info *fi)
{
	struct projfs_dir *d = calloc(1, sizeof(*d));
	if (!d)
		return (void)fuse_reply_err(req, errno);

	int fd = openat(ino_node(req, ino)->fd, ".", O_RDONLY | O_DIRECTORY);
	if (fd == -1)
		return (void)fuse_reply_err(req, errno);

	d->dir = fdopendir(fd);
	if (!d->dir) {
		int err = errno;
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
	size_t rem = size;

	char *buf = calloc(1, size);
	if (!buf)
		return (void)fuse_reply_err(req, errno);
	char *p = buf;

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
		size_t entsize = fuse_add_direntry(
			req, p, rem, d->ent->d_name, &attr, d->ent->d_off);

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
	.setattr	= projfs_ll_setattr,
	.flush		= projfs_ll_flush,
	.fsync		= projfs_ll_fsync,
	.forget		= projfs_ll_forget,
	.forget_multi	= projfs_ll_forget_multi,
	.mknod		= projfs_ll_mknod,
	.symlink	= projfs_ll_symlink,
	.create		= projfs_ll_create,
	.open		= projfs_ll_open,
	.read		= projfs_ll_read,
	.write_buf	= projfs_ll_write_buf,
	.release	= projfs_ll_release,
	.unlink		= projfs_ll_unlink,
	.mkdir		= projfs_ll_mkdir,
	.rmdir		= projfs_ll_rmdir,
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

	fs->root.nlookup = 2;
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
