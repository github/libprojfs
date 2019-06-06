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

#ifndef PROJFS_H
#define PROJFS_H

/** @file
 *
 * This file defines the library interface of ProjFS
 */

#include <stdint.h>			/* for uint64_t */

#include "projfs_notify.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Handle for a projfs filesystem */
struct projfs;

/** Filesystem event */
struct projfs_event {
	struct projfs *fs;
	uint64_t mask;			/* type flags; see projfs_notify.h */
	pid_t pid;
	const char *path;
	const char *target_path;	/* move destination or link target */
	int fd;				/* file descriptor for projection */
};

/** File projection attribute */
struct projfs_attr {
	const char *name;		/* alphanumeric plus internal punct */
	void *value;			/* binary data, or NULL */
	ssize_t size;			/* length of the value data, or -1 */
};

/**
 * Filesystem event handlers
 *
 * Error codes should be returned as negated errno values.
 */
struct projfs_handlers {
	/**
	 * Handle projection request for a file or directory.
	 *
	 * @param event Filesystem projection event.
	 * @return Zero on success or a negated errno(3) code on failure.
	 * @note When event->mask contains PROJFS_ONDIR, the file
	 *       descriptor in event->fd will be NULL.
	 */
	int (*handle_proj_event) (struct projfs_event *event);

	/**
	 * Handle notification of a file or directory event.
	 *
	 * @param event Filesystem notification event.
	 * @return Zero on success or a negated errno(3) code on failure.
	 * @note If event->target_path is non-NULL, the event was a
	 *       rename(2) or link(2) filesystem operation.
	 */
	int (*handle_notify_event) (struct projfs_event *event);

	/**
	 * Handle permission request for file or directory event.
	 *
	 * @param event Filesystem permission request event.
	 * @return PROJFS_ALLOW if the event is permitted,
	 *         PROJFS_DENY if the event is denied, or a
	 *         a negated errno(3) code on failure.
	 * @note If event->target_path is non-NULL, the event is a
	 *       rename(2) or link(2) filesystem operation.
	 */
	int (*handle_perm_event) (struct projfs_event *event);
};

/**
 * Create a new projfs filesystem.
 * TODO: doxygen
 */
struct projfs *projfs_new(const char *lowerdir, const char *mountdir,
			  const struct projfs_handlers *handlers,
			  size_t handlers_size, void *user_data,
			  int argc, const char **argv);

/**
 * Retrieve the private user data from a projfs handle.
 *
 * @param[in] fs Projected filesystem handle.
 * @return The user_data reference as passed to \p projfs_new().
 */
void *projfs_get_user_data(struct projfs *fs);

/**
 * Start a projfs filesystem.
 * TODO: doxygen
 */
int projfs_start(struct projfs *fs);

/**
 * Stop a projfs filesystem.
 * TODO: doxygen
 */
void *projfs_stop(struct projfs *fs);

/**
 * Create a directory whose contents will be projected until written.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of new directory under projfs mount point.
 * @param[in] mode File mode with which to create the new projected directory.
 * @param[in] attrs Array of user-defined projection attributes to be stored
 *                  with the new directory; may be NULL if nattrs is zero.
 * @param[in] nattrs Number of items in the attrs array.
 * @return Zero on success or an \p errno(3) code on failure.
 */
int projfs_create_proj_dir(struct projfs *fs, const char *path, mode_t mode,
			   struct projfs_attr *attrs, unsigned int nattrs);

/**
 * Create a file whose contents will be projected until written.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of new file under projfs mount point.
 * @param[in] size File size to be projected until file is written.
 * @param[in] mode File mode with which to create the new projected file.
 * @param[in] attrs Array of user-defined projection attributes to be stored
 *                  with the new file; may be NULL if nattrs is zero.
 * @param[in] nattrs Number of items in the attrs array.
 * @return Zero on success or an \p errno(3) code on failure.
 */
int projfs_create_proj_file(struct projfs *fs, const char *path, off_t size,
			    mode_t mode, struct projfs_attr *attrs,
			    unsigned int nattrs);

/**
 * Create a symlink with the given target.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of new symlink under projfs mount point.
 * @param[in] target The target of the symlink.
 * @return Zero on success or an \p errno(3) code on failure.
 */
int projfs_create_proj_symlink(struct projfs *fs, const char *path,
			       const char *target);

/**
 * Read projection attributes of a file or directory.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of the file or directory.
 * @param[in] attrs Array of requested projection attributes to be read;
 *                  for each item, name must be defined, and the value and
 *                  size fields must specify a writable buffer and its length.
 * @param[in] nattrs Number of items in the attrs array.
 * @return Zero on success or an \p errno(3) code on failure.
 * @note When a requested attribute name matches a defined attribute of the
 *       file or directory, the attribute's value will be written into the
 *       corresponding value field and its length recorded in the size field.
 *       If a provided value buffer is insufficient to store the full
 *       attribute value, ERANGE will be returned.
 *       When a requested attribute name matches no defined attributes,
 *       the value buffer will be left unchanged, and the corresponding
 *       size field will be set to -1.
 */
int projfs_get_attrs(struct projfs *fs, const char *path,
		     struct projfs_attr *attrs, unsigned int nattrs);

/**
 * Write or remove projection attributes of a file or directory.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of the file or directory.
 * @param[in] attrs Array of projection attributes to be written;
 *                  for each item, name must be defined, and the value and
 *                  size fields must specify the value data and its length,
 *                  or be set to NULL and 0 to delete the attribute.
 * @param[in] nattrs Number of items in the attrs array.
 * @return Zero on success or an \p errno(3) code on failure.
 * @note When a requested attribute name matches an existing attribute of the
 *       file or directory, the supplied value will replace the prior one,
 *       unless the supplied value is NULL or its size is 0, in which case
 *       the attribute will be removed.
 *       However, if the requested attribute name matches a reserved name,
 *       EINVAL will be returned.
 *       When a requested attribute name matches no defined attributes, and
 *       the supplied value is non-NULL and its size non-zero, a new attribute
 *       will be created; otherwise, the size field will be set to -1.
 *       In all cases, the supplied value data will not be altered.
 */
int projfs_set_attrs(struct projfs *fs, const char *path,
		     struct projfs_attr *attrs, unsigned int nattrs);

#ifdef __cplusplus
}
#endif


/*
 * This interface uses 64 bit off_t.
 *
 * On 32bit systems please add -D_FILE_OFFSET_BITS=64 to your compile flags!
 */

// from https://github.com/libfuse/libfuse/commit/d8f3ab7
#if defined(__GNUC__) &&						\
	(__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 6) &&	\
	!defined __cplusplus
_Static_assert(sizeof(off_t) == 8, "projfs: off_t must be 64bit");
#else
struct _projfs_off_t_must_be_64bit_dummy_struct {
	unsigned _projfs_off_t_must_be_64bit:((sizeof(off_t) == 8) ? 1 : -1);
};
#endif

#endif /* PROJFS_H */
