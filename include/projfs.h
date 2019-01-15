/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the COPYING file distributed with this library for additional
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
   License along with this library, in the file LICENSE.md; if not,
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

// TODO: remove when not needed
#define FUSE_USE_VERSION 32
#include <fuse3/fuse_lowlevel.h>

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
			  size_t handlers_size, void *user_data);

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
